// apps/wilicankit — general-purpose, user-configurable CAN device driver/GUI
// app. Board bring-up + OneWili FwGUI link + USB storage + LVGL super-loop.
// v1 smoke test: renders a placeholder screen and confirms the OneWili link
// to the MAIN CPU comes up (CAN TX only exists via OneWili commands to MAIN —
// see AGENTS.md; this app never touches MAIN CPU firmware).
#include "fw2.h"
#include "platform/diag.h"
#include "hardware/clocks.h"
#include "pico/stdlib.h"
#include "input/app_recovery_onewili.h"
#include "lvgl.h"
#include "lvgl_port.h"
#include "ow_link.h"
#include "can_link.h"
#include "device_leds.h"
#include "dvi_mirror.h"
#include "ui_shell.h"
#include "app_state.h"

int main(void) {
    // fw2_psram_bootstrap() (wilibsp bsp/app/psram_bootstrap.c, run from a
    // dedicated SRAM section before this app's PSRAM-resident code ever
    // executes) already called board_init_psram(): clk_sys/QMI are at their
    // final 250 MHz timing and inherited peripherals (SPI/I2C/ioexp/backlight
    // off) are ready. A PSRAM app does not call board_init() itself.
    DIAG("wilicankit: PSRAM-resident, clk_sys=%u Hz\n", clock_get_hz(clk_sys));
    st7796_init();
    ft6336_init();
    dvi_mirror_init();  // clk_sys is already final; mirrors the LCD 1:1 over DVI
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
    // FW2App contract: establish the whole 480x320 surface before enabling
    // the backlight — a loadable app inherits the previous firmware's panel
    // RAM. ui_shell_create() invalidates the full screen, so one
    // lv_timer_handler() pass renders and flushes a complete first frame.
    lv_timer_handler();
    board_backlight_set(1);

    // fw2_app_recovery_init() requests+waits for the declared POWER_ZONES
    // (DISPLAY, SDCARD, CAN — see CMakeLists.txt) and starts servicing HOME
    // (5 s hold -> watchdog reboot to the recovery loader) and PAGE (5 s hold
    // -> About screen) on every fw2_app_recovery_task() call below.
    fw2_app_recovery_init();
    fw2_app_recovery_wrap_sd();  // service recovery during blocking ow_sd_* calls (storage.c)

    ow_link_open();    // blocks with retry DIAGs until the MAIN CPU link is up
    can_link_attach(ow_link_device());
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
        fw2_app_recovery_task();  // uartkbd_task() + picpwr_task() + About/HOME handling
        agentio_task();
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
        device_leds_task();
        sleep_ms(2);
    }
}
