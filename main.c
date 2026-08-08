// apps/wilicankit — general-purpose, user-configurable CAN device driver/GUI
// app. Board bring-up + OneWili FwGUI link + USB storage + LVGL super-loop.
// v1 smoke test: renders a placeholder screen and confirms the OneWili link
// to the MAIN CPU comes up (CAN TX only exists via OneWili commands to MAIN —
// see AGENTS.md; this app never touches MAIN CPU firmware).
#include "fw2.h"
#include "platform/diag.h"
#include "platform/spi_bus.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/resets.h"
#include "hardware/structs/qmi.h"
#include "hardware/vreg.h"
#include "pico/runtime.h"
#include "pico/runtime_init.h"
#include "pico/stdlib.h"
#include "lvgl.h"
#include "lvgl_port.h"
#include "ow_link.h"
#include "can_link.h"
#include "device_leds.h"
#include "dvi_mirror.h"
#include "ui_shell.h"
#include "app_state.h"

// Strong override of the SDK's __weak version, which resets IO_BANK0 and
// PADS_BANK0. It already holds the QSPI bank out "as this is fatal if running
// from flash"; this app runs from PSRAM, whose chip select is XIP_CS1n on
// GPIO 47 in bank 0, so the stock version strips the CS function and the very
// next instruction fetch takes an IBUSERR. Body is otherwise verbatim.
void runtime_init_early_resets(void) {
    reset_block_mask(~(
            (1u << RESET_IO_QSPI) |
            (1u << RESET_PADS_QSPI) |
            (1u << RESET_IO_BANK0) |
            (1u << RESET_PADS_BANK0) |
            (1u << RESET_PLL_USB) |
            (1u << RESET_USBCTRL) |
            (1u << RESET_SYSCFG) |
            (1u << RESET_PLL_SYS)
    ));

    unreset_block_mask_wait_blocking(RESETS_RESET_BITS & ~(
            (1u << RESET_HSTX) |
            (1u << RESET_ADC) |
            (1u << RESET_SPI0) |
            (1u << RESET_SPI1) |
            (1u << RESET_UART0) |
            (1u << RESET_UART1) |
            (1u << RESET_USBCTRL)
    ));
}

// The PSRAM loader stub (Fw2PsramStub launchPsramApp) branches to vector word 1
// with `cpsid i` still in force, and crt0 never issues `cpsie i` because a
// bootrom handover always arrives with interrupts already enabled. Left masked,
// the first sleep_ms() waits forever on a timer IRQ that can never fire. The
// NVIC sweep drops anything the stub left enabled or pending, so re-enabling
// can't immediately vector into __unhandled_user_irq; EARLIEST keeps it ahead
// of the default alarm pool, which claims its own IRQ later at "11000".
static void psram_stub_irq_handover(void) {
    for (uint i = 0; i < NUM_IRQS; i++) {
        irq_set_enabled(i, false);
        irq_clear(i);
    }
    __asm volatile ("cpsie i" ::: "memory");
}
PICO_RUNTIME_INIT_FUNC_RUNTIME(psram_stub_irq_handover, PICO_RUNTIME_INIT_EARLIEST);

// Fields common to the 150 and 250 MHz APS6404L timings. MIN_DESELECT=4
// matches the SDK's own psram_configure_params() reference value at 250 MHz
// clk_sys (wilibsp docs/hardware/facts.md — same APS6404L chip/CS pin as
// adafruit_fruit_jam) and is also legal at 150 MHz, so it's reproduced by
// hand once here because psram_reinitialize() can't run from PSRAM.
// MIN_DESELECT=4 holds CS# high 20 ns at 250 MHz, clearing the 18 ns tCPH
// minimum (33.3 ns at 150 MHz).
//
// RXDELAY is NOT included here: it compensates a fixed physical round-trip
// delay expressed in clk_sys *cycles*, so the correct value scales with
// clk_sys frequency (2 at 150/200 MHz, 3 at 250 MHz per the SDK reference).
// Baking the 250 MHz-tuned RXDELAY=3 into a value written while clk_sys is
// still 150 MHz corrupts PSRAM read data capture for the code this function
// itself executes from PSRAM, producing an immediate garbage instruction
// fetch -> HardFault -> lockup. It must be staged like MAX_SELECT below:
// the pre-load write keeps the proven-safe low-clock value, and only the
// post-raise write bumps it to the 250 MHz-tuned value.
#define QMI_M1_TIMING_BASE (                                                   \
      ((uint32_t)1u << QMI_M1_TIMING_COOLDOWN_LSB)                             \
    | ((uint32_t)QMI_M1_TIMING_PAGEBREAK_VALUE_1024 << QMI_M1_TIMING_PAGEBREAK_LSB) \
    | ((uint32_t)4u << QMI_M1_TIMING_MIN_DESELECT_LSB)                         \
    | ((uint32_t)2u << QMI_M1_TIMING_CLKDIV_LSB))

// The loader hands apps over at the SDK default 150 MHz. Raising clk_sys
// shortens SCK and CS# high time the instant it takes effect, so the QMI window
// this code fetches instructions from would be out of spec between the clock
// change and the re-time. MAX_SELECT counts 64 clk_sys periods against an 8 us
// tCEM, which allows 18 at 150 MHz and 31 at 250 — so 18 is legal at both, and
// pre-loading it means there is never an invalid window to run through.
static void psram_clock_raise_250(void) {
    vreg_set_voltage(VREG_VOLTAGE_1_25);
    sleep_ms(10);

    qmi_hw->m[1].timing = QMI_M1_TIMING_BASE
        | (2u << QMI_M1_TIMING_RXDELAY_LSB)
        | (18u << QMI_M1_TIMING_MAX_SELECT_LSB);
    set_sys_clock_khz(250000, true);
    qmi_hw->m[1].timing = QMI_M1_TIMING_BASE
        | (3u << QMI_M1_TIMING_RXDELAY_LSB)
        | (31u << QMI_M1_TIMING_MAX_SELECT_LSB);
}

// Mirrors wilibsp's board_init_clk() with its clock/PSRAM prologue removed.
// psram_clock_raise_250() above already handled clk_sys, and psram_reinitialize()
// is documented unsafe from PSRAM (the loader's stub configured the chip).
// Everything below is the tail of board_init_clk() verbatim; it belongs upstream
// in wilibsp eventually.
static void board_init_psram_resident(void) {
    uint32_t f = clock_get_hz(clk_sys);
    clock_configure(clk_peri, 0, CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS, f, f);

    spi_bus_init();

    gpio_init(PIN_CC1101_CS);
    gpio_set_dir(PIN_CC1101_CS, GPIO_OUT);
    gpio_put(PIN_CC1101_CS, 1);

    gpio_init(PIN_LCD_BL);
    gpio_set_dir(PIN_LCD_BL, GPIO_OUT);
    board_backlight_set(0);

    board_i2c1_init();
    ioexp_init();
}

int main(void) {
    // Before board init: clk_peri and the SPI baud rates derive from clk_sys.
    psram_clock_raise_250();
    board_init_psram_resident();
    DIAG("wilicankit: PSRAM-resident, clk_sys=%u Hz\n", clock_get_hz(clk_sys));
    st7796_init();
    board_backlight_set(1);
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
        device_leds_task();
        sleep_ms(2);
    }
}
