// apps/wilicankit/can_link.c — see can_link.h.
#include "can_link.h"
#include "onewili.h"
#include "onewili_fwgui.h"
#include "platform/diag.h"
#include "pico/stdlib.h"
#include "app_state.h"

// ~37 KB of buffers (per toggleled) — far too big for the 2 KB stack, and
// too big for SRAM's fixed 512 KB copy_to_ram budget too (was 45% of all
// .bss). Only touched by blocking, low-frequency serial calls (open, CAN
// writes, GPIO reads) -- no DMA/ISR, so PSRAM's extra QSPI latency doesn't
// matter here the way it does for the LVGL draw buffers.
static ow_device __uninitialized_psram("wilicankit_ow_device") s_dev;
static bool s_open = false;

// Global TX gate — see can_link.h. Defaults offline; user must opt in.
static bool s_online = false;

// Consecutive-fault counter for the auto-offline gate below: a single
// rejected send can be a transient bus blip (see 2026-08-03 findings doc),
// so only a run of CAN_LINK_OFFLINE_FAULT_THRESHOLD in a row (no success in
// between) actually drops us offline.
#define CAN_LINK_OFFLINE_FAULT_THRESHOLD 25
static uint8_t s_consecutive_faults;
static uint8_t s_max_consecutive_faults;   // highest streak ever seen, for diagnostics

// Send-once is a counted, capped queue (not a bool): each button press is a
// distinct user action and must produce its own frame, unlike the periodic
// flag below, where re-applying the current enable/period is idempotent so
// coalescing repeated requests into one is correct. Capped so a burst of
// presses during a slow/stuck link can't queue unboundedly.
#define CAN_LINK_SEND_ONCE_MAX_PENDING 4
static uint8_t s_pending_send_once[CAN_MAX_MESSAGES];
static bool    s_pending_periodic[CAN_MAX_MESSAGES];

// ── Display-CPU Periodic Timer State ────────────────────────────────────
// Per-message timer state for display-CPU-resident periodic sends.
struct {
    uint32_t period_ms;      // Periodic rate in milliseconds (0 = not set)
    uint32_t last_tx_ms;     // Timestamp of last transmission (milliseconds)
    bool     enabled;        // Is this message's periodic timer running?
} static s_periodic[CAN_MAX_MESSAGES];

// ── TX stoppage attribution diagnostics ─────────────────────────────────
// Added to attribute a "CAN just stops after a minute or two" report to
// display-side logic vs. MAIN's OneWili link vs. MAIN's CAN controller/bus.
// Kept shallow (only called from can_link_send_once() and a throttled poll
// driven from can_link_periodic_poll(), never from inside the FwGUI RX pump
// path) and rate-limited (DIAG only once per heartbeat period) — see repo
// memory notes on prior naive-diagnostic instability in this app family.
#define CAN_LINK_HEARTBEAT_PERIOD_MS 2000
static uint32_t s_tx_attempts[CAN_MAX_MESSAGES];
static uint32_t s_tx_ok[CAN_MAX_MESSAGES];
static int32_t  s_tx_last_err[CAN_MAX_MESSAGES]; // ow_status of most recent failure, -1 = none yet
static uint32_t s_health_attempts;   // non-CAN OneWili calls attempted (link/MAIN liveness probe)
static uint32_t s_health_ok;
static int32_t  s_health_last_err = -1;
static uint32_t s_heartbeat_last_ms;

// Real millisecond timestamp (since boot), not a per-call tick assumption —
// the main loop's iteration time is NOT constant (LVGL render load, USB
// storage task, etc. all vary it), so periodic rates must be measured
// against wall-clock time. Wraps every ~49 days; overflow-safe as long as
// we always use delta comparisons (elapsed = now - last).
static uint32_t now_ms(void) {
    return to_ms_since_boot(get_absolute_time());
}

void can_link_open(void) {
    while (ow_open_fwgui(&s_dev) != OW_OK) {
        DIAG("can_link: FwGUI link open failed (is the main CPU running stock fw?), retry in 1 s\n");
        sleep_ms(1000);
    }
    s_open = true;
    DIAG("can_link: link up\n");
}

void can_link_notify_power_zones(uint32_t zone_mask) {
    ow_fwgui_send_power_zones(zone_mask);
}

bool can_link_is_open(void) {
    return s_open;
}

bool can_link_is_online(void) {
    return s_online;
}

void can_link_set_online(bool online) {
    s_online = online;
    s_consecutive_faults = 0;   // start any new online session with a clean streak
}

bool can_link_send_once(const can_message_t *msg, const can_signal_t *signals) {
    if (!s_open) return false;
    if (!s_online) return false;   // offline: never touch MAIN, avoids the blocking round-trip
    uint8_t buf[8];
    can_message_build(msg, signals, buf);
    ow_status st = ow_io_canfd_write_canfd(&s_dev, msg->channel, msg->can_id, 0,
                                            msg->extended_id ? 1 : 0, buf, msg->dlc);
    if (st != OW_OK) {
        DIAG("can_link: send_once failed, status %d\n", (int)st);
        if (++s_consecutive_faults > s_max_consecutive_faults) s_max_consecutive_faults = s_consecutive_faults;
        if (s_consecutive_faults >= CAN_LINK_OFFLINE_FAULT_THRESHOLD) {
            s_online = false;   // sustained failure means the bus is not healthy
            s_consecutive_faults = 0;
        }
    } else {
        s_consecutive_faults = 0;   // any success clears the streak
    }

    // Attribution counters: msg always points at an element of g_messages[]
    // (the only array can_link_send_once() is ever called against), so a
    // plain pointer offset gives a safe per-message index.
    int idx = (int)(msg - g_messages);
    if (idx >= 0 && idx < CAN_MAX_MESSAGES) {
        s_tx_attempts[idx]++;
        if (st == OW_OK) s_tx_ok[idx]++;
        else s_tx_last_err[idx] = (int32_t)st;
    }
    return st == OW_OK;
}

bool can_link_arm_periodic(uint8_t slot_index, const can_message_t *msg, const can_signal_t *signals) {
    if (!s_open) return false;
    uint8_t buf[8];
    can_message_build(msg, signals, buf);
    ow_status st = ow_io_canfd_write_canfd_periodic(&s_dev, slot_index, 1, (int32_t)msg->period_us,
                                                      msg->channel, msg->can_id, 0,
                                                      msg->extended_id ? 1 : 0, buf, msg->dlc);
    if (st != OW_OK) DIAG("can_link: arm_periodic(%d) failed, status %d\n", (int)slot_index, (int)st);
    return st == OW_OK;
}

bool can_link_disable_periodic(uint8_t slot_index, const can_message_t *msg) {
    if (!s_open) return false;
    uint8_t buf[8] = {0};
    ow_status st = ow_io_canfd_write_canfd_periodic(&s_dev, slot_index, 0, 0,
                                                      msg->channel, msg->can_id, 0,
                                                      msg->extended_id ? 1 : 0, buf, msg->dlc);
    if (st != OW_OK) DIAG("can_link: disable_periodic(%d) failed, status %d\n", (int)slot_index, (int)st);
    return st == OW_OK;
}

uint32_t can_link_dropped_frames(void) {
    return ow_fwgui_dropped_frames();
}

uint32_t can_link_tx_attempts(uint8_t msg_index) {
    return (msg_index < CAN_MAX_MESSAGES) ? s_tx_attempts[msg_index] : 0;
}

uint32_t can_link_tx_ok(uint8_t msg_index) {
    return (msg_index < CAN_MAX_MESSAGES) ? s_tx_ok[msg_index] : 0;
}

void can_link_health_stats(uint32_t *attempts, uint32_t *ok, int32_t *last_err) {
    if (attempts) *attempts = s_health_attempts;
    if (ok) *ok = s_health_ok;
    if (last_err) *last_err = s_health_last_err;
}

uint8_t can_link_fault_streak(void) {
    return s_consecutive_faults;
}

uint8_t can_link_max_fault_streak(void) {
    return s_max_consecutive_faults;
}

uint8_t can_link_fault_threshold(void) {
    return CAN_LINK_OFFLINE_FAULT_THRESHOLD;
}

uint32_t can_link_rx_frames(void) {
    return 0;
}

// ── TX stoppage attribution diagnostics: helpers ────────────────────────

// Trivial, read-only, non-CAN OneWili call used purely as a link/MAIN
// liveness probe (same pattern as apps/toggleled's GPIO call, but read-only
// so it has no side effect on hardware). If this keeps succeeding after CAN
// sends start failing, the fault is CAN-specific on MAIN (its CAN
// controller/bus), not the whole OneWili bridge; if this also fails at the
// same moment, MAIN/the link itself is down.
static void can_link_health_check(void) {
    if (!s_open) return;
    uint32_t gpiostate = 0;
    ow_status st = ow_io_gpio_read_all(&s_dev, &gpiostate);
    s_health_attempts++;
    if (st == OW_OK) s_health_ok++;
    else s_health_last_err = (int32_t)st;
}

// Throttled (once per CAN_LINK_HEARTBEAT_PERIOD_MS) summary of the counters
// above, plus the health-check probe. Deliberately shallow (called only
// from can_link_periodic_poll(), itself called directly from main()'s super
// loop — never from inside the FwGUI RX pump) and cheap (a handful of
// short DIAG lines every 2 s, not per-send) to avoid repeating prior
// documented instability from naive diagnostics in this app family.
static void can_link_heartbeat_poll(void) {
    uint32_t now = now_ms();
    if (now - s_heartbeat_last_ms < CAN_LINK_HEARTBEAT_PERIOD_MS) return;
    s_heartbeat_last_ms = now;

    can_link_health_check();

    DIAG("clhb t=%u health=%u/%u err=%d\n", (unsigned)now,
         (unsigned)s_health_ok, (unsigned)s_health_attempts, (int)s_health_last_err);
    for (uint8_t i = 0; i < CAN_MAX_MESSAGES; i++) {
        if (!g_messages[i].in_use) continue;
        DIAG("clhb m%u a=%u ok=%u err=%d\n", (unsigned)i,
             (unsigned)s_tx_attempts[i], (unsigned)s_tx_ok[i], (int)s_tx_last_err[i]);
    }
}

// ── Display-CPU Periodic Timer Functions ────────────────────────────────

void can_link_periodic_init(void) {
    for (uint8_t i = 0; i < CAN_MAX_MESSAGES; i++) {
        s_periodic[i].period_ms = 0;
        s_periodic[i].last_tx_ms = 0;
        s_periodic[i].enabled = false;
    }
}

void can_link_periodic_set_rate(uint8_t msg_index, uint32_t period_ms) {
    if (msg_index >= CAN_MAX_MESSAGES) return;
    s_periodic[msg_index].period_ms = period_ms;
    // Reset timer when rate is updated: next send will be at period_ms from now
    s_periodic[msg_index].last_tx_ms = now_ms();
}

void can_link_periodic_set_enabled(uint8_t msg_index, bool enabled) {
    if (msg_index >= CAN_MAX_MESSAGES) return;
    if (enabled && !s_periodic[msg_index].enabled) {
        // Enabling: reset timer so it fires after one full period
        s_periodic[msg_index].last_tx_ms = now_ms();
        s_periodic[msg_index].enabled = true;
    } else if (!enabled && s_periodic[msg_index].enabled) {
        // Disabling: stop the timer but keep the rate
        s_periodic[msg_index].enabled = false;
    }
}

void can_link_periodic_poll(void) {
    can_link_heartbeat_poll();

    // Measured against a real timestamp — the main loop's iteration time is
    // not constant, so a fixed per-call increment would drift the actual
    // TX rate away from the configured period_ms (see can_link.h).
    uint32_t now = now_ms();

    for (uint8_t i = 0; i < CAN_MAX_MESSAGES; i++) {
        if (!s_periodic[i].enabled || !g_messages[i].in_use) continue;
        if (s_periodic[i].period_ms == 0) continue;

        uint32_t elapsed = now - s_periodic[i].last_tx_ms;
        if (elapsed >= s_periodic[i].period_ms) {
            // Time to send: rebuild message with current signal values and TX.
            // Always advance last_tx_ms, success or failure. Confirmed
            // on-hardware 2026-07-27: NOT advancing it on failure made a
            // sustained MAIN-side rejection (OW_ERR_FAILED) retry on every
            // main-loop tick (~2 ms) instead of the configured period,
            // flooding RTT with "send_once failed" lines and potentially
            // keeping MAIN's CAN controller too busy to attempt any
            // bus-off-style auto-recovery. Retrying at the configured rate
            // is still a retry, just not an unthrottled one.
            can_link_send_once(&g_messages[i], g_signals);
            s_periodic[i].last_tx_ms = now;
        }
    }
}

void can_link_request_send_once(uint8_t msg_index) {
    if (msg_index >= CAN_MAX_MESSAGES) return;
    if (s_pending_send_once[msg_index] >= CAN_LINK_SEND_ONCE_MAX_PENDING) {
        DIAG("can_link: send_once request dropped, queue full for msg %u\n", (unsigned)msg_index);
        return;
    }
    s_pending_send_once[msg_index]++;
}

void can_link_request_periodic_update(uint8_t msg_index) {
    if (msg_index >= CAN_MAX_MESSAGES) return;
    // Queue a periodic state update to be processed by can_link_poll()
    s_pending_periodic[msg_index] = true;
}

void can_link_resync_periodic_all(void) {
    for (uint8_t i = 0; i < CAN_MAX_MESSAGES; i++) {
        s_pending_periodic[i] = true;
    }
}

void can_link_poll(void) {
    // Handle one-shot send requests
    for (uint8_t i = 0; i < CAN_MAX_MESSAGES; i++) {
        while (s_pending_send_once[i] > 0) {
            s_pending_send_once[i]--;
            if (g_messages[i].in_use) can_link_send_once(&g_messages[i], g_signals);
        }
    }

    // Handle periodic rate/enable state changes queued by UI layer
    for (uint8_t i = 0; i < CAN_MAX_MESSAGES; i++) {
        if (!s_pending_periodic[i]) continue;
        s_pending_periodic[i] = false;

        if (!g_messages[i].in_use) continue;

        can_message_t *m = &g_messages[i];
        if (m->enabled) {
            // Enable periodic: convert microseconds to milliseconds
            uint32_t period_ms = (m->period_us + 999) / 1000;  // Round up
            can_link_periodic_set_rate(i, period_ms);
            can_link_periodic_set_enabled(i, true);
        } else {
            // Disable periodic
            can_link_periodic_set_enabled(i, false);
        }
    }
}
