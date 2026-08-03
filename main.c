// apps/wilicankit — general-purpose, user-configurable CAN device driver/GUI
// app. Board bring-up + OneWili FwGUI link + USB storage + LVGL super-loop.
// v1 smoke test: renders a placeholder screen and confirms the OneWili link
// to the MAIN CPU comes up (CAN TX only exists via OneWili commands to MAIN —
// see AGENTS.md; this app never touches MAIN CPU firmware).
#include "fw2.h"
#include "platform/diag.h"
#include "pico/stdlib.h"
#include "lvgl.h"
#include "lvgl_port.h"
#include "can_link.h"
#include "ui_shell.h"
#include "app_state.h"

int main(void) {
    board_init();
    st7796_init();
    board_backlight_set(1);
    ft6336_init();
    // Must run before anything draws: agentio_init() zeroes the shadow
    // framebuffer that `fw screenshot`/`fw touch` read from (see
    // docs/drivers/agentio.md, "three app calls").
    agentio_init();
    // g_signals/g_messages/g_controls live in PSRAM (__uninitialized_psram),
    // which is NOT zeroed at boot like normal .bss — without this, every
    // slot starts as power-on PSRAM garbage instead of !in_use.
    app_state_reset_all();

    lvgl_port_init();
    lv_fs_fatfs_init();  // registers the "F:" driver so ui_config.c's file explorer can browse "0:/wilicankit"
    ui_shell_create();

    // The CAN rail may be off at boot: request it and wait for the apply
    // (docs/drivers/power.md). On firmware with rails already on, first status
    // frame proves it; on firmware with no status frames, nothing to wait for.
    uartkbd_init();
    picpwr_keep_awake(picpwr_zone_bit(PICPWR_ZONE_CAN));
    {
        absolute_time_t give_up   = make_timeout_time_ms(10000);
        absolute_time_t no_frames = make_timeout_time_ms(4000);
        uint32_t rails;
        while (!time_reached(give_up)) {
            uartkbd_task();
            picpwr_task();
            if (picpwr_rails(&rails)) {
                if (rails & picpwr_zone_bit(PICPWR_ZONE_CAN)) break;
            } else if (time_reached(no_frames)) {
                break;
            }
            sleep_ms(25);
        }
        DIAG("picpwr: CAN controller's rail %s\n",
             (picpwr_rails(&rails) && (rails & picpwr_zone_bit(PICPWR_ZONE_CAN)))
                 ? "up" : "state unknown");
    }

    can_link_open();   // blocks with retry DIAGs until the MAIN CPU link is up
    can_link_periodic_init();  // initialize display-CPU periodic timer state

    uint32_t zone_mask = 0;
    picpwr_rails(&zone_mask);
    can_link_notify_power_zones(zone_mask);  // MAIN re-inits CAN once it hears the rail is up; also resent periodically below in case MAIN reboots independently

    usb_store_init();

    // Periodic resend (not just on rail-mask change): MAIN can reboot
    // independently of DISPLAY in the field (watchdog reset, brownout,
    // reflash) with no way for DISPLAY to detect it, so a boot-time-only
    // burst can silently miss a fresh MAIN instance entirely. Idempotent on
    // MAIN, same as the existing periodic CAN messages.
    #define ZONE_NOTIFY_PERIOD_MS 2000
    absolute_time_t zone_notify_next = make_timeout_time_ms(ZONE_NOTIFY_PERIOD_MS);

    for (;;) {
        uartkbd_task();
        agentio_task();
        picpwr_task();
        {
            uint32_t rails;
            if (picpwr_rails(&rails) && rails != zone_mask) {
                zone_mask = rails;
                can_link_notify_power_zones(zone_mask);
                zone_notify_next = make_timeout_time_ms(ZONE_NOTIFY_PERIOD_MS);
            } else if (time_reached(zone_notify_next)) {
                can_link_notify_power_zones(zone_mask);
                zone_notify_next = make_timeout_time_ms(ZONE_NOTIFY_PERIOD_MS);
            }
        }
        lv_timer_handler();
        can_link_poll();           // deferred CAN TX I/O — see can_link.h; runs after
                                    // the render/flush pass so switches/buttons never
                                    // appear to freeze during the FwGUI round-trip
        can_link_periodic_poll();  // check and fire periodic message timers
        ui_shell_poll();            // reflect any auto-offline (send failure) in the UI
        usb_store_task();
        sleep_ms(2);
    }
}
