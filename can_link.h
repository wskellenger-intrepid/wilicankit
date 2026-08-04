// apps/wilicankit/can_link.h — thin OneWili wrapper for CAN TX. This is the
// ONLY place in the app that talks to the MAIN CPU, and it only ever calls
// existing generated onewili/onewili_fwgui API — never anything MAIN-side.
// The RP2350B display processor has no native CAN controller (see AGENTS.md).
#ifndef CAN_LINK_H_
#define CAN_LINK_H_
#include <stdbool.h>
#include <stdint.h>
#include "can_model.h"

// Opens the FwGUI link to the MAIN CPU. Blocks, retrying forever (with a
// DIAG message per attempt) until the link comes up. Call once at startup,
// after board_init().
void can_link_open(void);

// True once can_link_open() has completed successfully.
bool can_link_is_open(void);

// Global TX gate, independent of each message's own enabled/periodic state.
// Defaults to false (offline) at boot: with a bad/absent CAN bus, MAIN can
// stall acking each frame for up to OW_DEFAULT_TIMEOUT_MS, and since every
// send is a blocking call from the single-threaded main loop, a config with
// several periodic messages enabled can freeze the whole UI for tens of
// seconds (see docs/superpowers/findings/ for the reproduction). Going
// online is an explicit user action (bottom-bar button in ui_shell.c).
// can_link_send_once() auto-drops back offline after CAN_LINK_OFFLINE_FAULT_THRESHOLD
// consecutive send failures (a single rejection can be a transient bus blip,
// see docs/superpowers/findings/2026-08-03-wilicankit-auto-offline-e2e.md;
// any success in between resets the streak); re-entry is manual only (no
// auto-retry), call can_link_set_online(true) again to resume.
bool can_link_is_online(void);
void can_link_set_online(bool online);

// Transmits `msg` once (ow_io_canfd_write_canfd), building its payload from
// the current physical values of the signals placed in it. No-op (returns
// false immediately, without touching MAIN) while offline.
bool can_link_send_once(const can_message_t *msg, const can_signal_t *signals);

// Arms (or updates, if already armed) message `slot_index`'s periodic
// transmission on MAIN (ow_io_canfd_write_canfd_periodic), rebuilding the
// payload from current signal values. `slot_index` is reused directly as
// the OneWili periodic slot index — NOTE: re-arming an in-flight slot with
// fresh data to live-update it is an assumption, unverified on hardware
// (see AGENTS.md/session plan notes on this risk).
bool can_link_arm_periodic(uint8_t slot_index, const can_message_t *msg, const can_signal_t *signals);

// Disables message `slot_index`'s periodic transmission.
bool can_link_disable_periodic(uint8_t slot_index, const can_message_t *msg);

// Binary-stream frames dropped (diagnostics only, see onewili_fwgui.h).
uint32_t can_link_dropped_frames(void);

// ── Health page stats (ui_monitor.c) ────────────────────────────────────
// Per-message send-attempt/success counts, since can_link_open(). Bounds-
// checked against CAN_MAX_MESSAGES; out-of-range msg_index returns 0.
uint32_t can_link_tx_attempts(uint8_t msg_index);
uint32_t can_link_tx_ok(uint8_t msg_index);

// Non-CAN OneWili liveness-probe counts (see can_link_health_check()) and
// the ow_status of its most recent failure (-1 = none yet).
void can_link_health_stats(uint32_t *attempts, uint32_t *ok, int32_t *last_err);

// Consecutive send-failure streak and the threshold that triggers
// auto-offline (CAN_LINK_OFFLINE_FAULT_THRESHOLD) — see can_link_send_once().
uint8_t can_link_fault_streak(void);
// Highest streak value ever reached (persists across auto-offline resets).
uint8_t can_link_max_fault_streak(void);
uint8_t can_link_fault_threshold(void);

// Placeholder: this app has no CAN RX path yet (display CPU has no native
// CAN controller, see AGENTS.md) — always 0 until that lands.
uint32_t can_link_rx_frames(void);

// Sends FWGUI_EVENT_POWER_ZONES with the live picpwr_rails() mask so MAIN
// re-inits its CAN controller once the CAN rail is up. Idempotent — call on
// open, on mask change, AND periodically (main.c does all three): MAIN can
// reboot independently of DISPLAY (watchdog, brownout, reflash) with no way
// for DISPLAY to detect it, so a one-shot send can silently miss a fresh
// MAIN instance.
void can_link_notify_power_zones(uint32_t zone_mask);

// ── Deferred requests ────────────────────────────────────────────────────
// can_link_send_once/arm_periodic/disable_periodic each block on a FwGUI
// UART round-trip (up to OW_DEFAULT_TIMEOUT_MS). Calling them directly from
// an LVGL event callback stalls lv_timer_handler() before it reaches its
// render/flush pass, so the widget that triggered the action (switch,
// button, slider) appears frozen for the full round-trip even though the
// command already reached MAIN. UI code must use these instead, and the
// main loop must call can_link_poll() once per iteration, after
// lv_timer_handler(), to actually perform the queued I/O.

// Queues a one-shot transmission of message `msg_index`'s current payload.
// Each call queues its own send (counted, not coalesced — every button
// press is a distinct action) up to a small internal cap; calls beyond the
// cap are dropped (with a DIAG) rather than queued indefinitely.
void can_link_request_send_once(uint8_t msg_index);

// ── Display-CPU Periodic Timer (NEW) ────────────────────────────────────
// The display CPU now owns periodic message transmission, using a simple
// per-message elapsed-time timer. Each message's rate is independent; when
// a message is due (elapsed >= period_ms), its payload is rebuilt with
// current signal values and sent via can_link_send_once(). This gives
// real-time signal updates during periodic transmission.

// Initializes all per-message timers (zeroed, disabled). Call once at startup.
void can_link_periodic_init(void);

// Sets message `msg_index`'s periodic rate (in milliseconds) and resets
// its timer. If the message is already enabled, the new rate takes effect
// on the next poll.
void can_link_periodic_set_rate(uint8_t msg_index, uint32_t period_ms);

// Enables or disables periodic transmission for message `msg_index`. When
// enabled, the message's timer is reset (will fire after the next period).
// When disabled, the periodic timer is stopped but retains its rate setting.
void can_link_periodic_set_enabled(uint8_t msg_index, bool enabled);

// Called once per main-loop iteration to check each enabled message's timer
// and fire periodic sends. This is the "engine" that makes display-CPU
// periodic work — rebuilds payload with current signal values and calls
// can_link_send_once() when a message is due.
void can_link_periodic_poll(void);

// Queues a periodic send rate/enable update for message `msg_index`.
// Now uses display-CPU timer instead of OneWili periodic API.
void can_link_request_periodic_update(uint8_t msg_index);

// Queues a periodic update request for every message slot. Call after
// loading a config: g_messages is overwritten directly (storage.c), which
// leaves s_periodic's per-message enabled/rate state stale (still whatever
// it was before the load, usually all-disabled) until each row's UI is
// touched — this forces every slot back in sync with its loaded state.
void can_link_resync_periodic_all(void);

// Performs any requests queued since the last call. Call once per
// main-loop iteration, after lv_timer_handler() — never from inside an
// LVGL event callback.
void can_link_poll(void);

#endif // CAN_LINK_H_
