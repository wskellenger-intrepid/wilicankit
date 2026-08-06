// apps/wilicankit/device_leds.c — see device_leds.h.
#include "device_leds.h"
#include "input/picpwr.h"
#include "input/uartkbd.h"
#include "platform/diag.h"

// Two DISTINCT status frames must agree before a mask is echoed back as a
// command: "a snapshot echoed back switches off every rail it failed to
// report" (wilibsp docs/drivers/power.md). picpwr.c gates on the frame
// counter for the same reason; this is the non-blocking equivalent.
static bool rails_agree(uint32_t *out) {
    static uint32_t last_frame, last_rails;
    static bool have_last;

    uint32_t f = uartkbd_frames();
    if (f == last_frame) return false;   // no fresh frame since the last check
    uint32_t rails;
    if (!picpwr_rails(&rails)) return false;
    last_frame = f;

    bool agreed = have_last && rails == last_rails;
    last_rails = rails;
    have_last = true;
    if (!agreed) return false;
    *out = rails;
    return true;
}

void device_leds_task(void) {
    static bool blanked, done;
    if (done) return;

    uint32_t rails;
    if (!rails_agree(&rails)) return;

    const uint32_t led_bit = picpwr_zone_bit(PICPWR_ZONE_RGB_LEDS);
    if (!blanked) {
        if (rails & led_bit) {
            // Belt-and-braces on top of rails_agree(): omitting a zone from the
            // mask switches it off, and cutting zone 7 out from under the SD card
            // costs a power cycle. Never let these drop, whatever the frame said.
            const uint32_t keep = picpwr_zone_bit(PICPWR_ZONE_DISPLAY) |
                                  picpwr_zone_bit(PICPWR_ZONE_SDCARD)  |
                                  picpwr_zone_bit(PICPWR_ZONE_CAN);
            // sleep/wake stay zero: already this app's standing config, since
            // picpwr_keep_awake() is the only other sender here and never sets them.
            picpwr_cfg_t cfg = { .awake = (rails | keep) & ~led_bit };
            picpwr_send(&cfg);   // rate-limited internally; retried on later passes
            return;
        }
        blanked = true;   // rail confirmed down, so the LEDs have lost their state
        return;
    }

    // picpwr_send()'s 1500 ms spacing keeps the rail down well past the
    // discharge the WS2812s need before this re-powers them.
    if (picpwr_ensure_awake(led_bit)) {
        done = true;
        DIAG("device_leds: RGB LED rail power-cycled\n");
    }
}
