// apps/wilicankit/lvgl_port.c — see lvgl_port.h.
#include "lvgl_port.h"
#include "lvgl.h"

#include "fw2.h"
#include "platform/psram.h"
#include "platform/diag.h"
#include "pico/stdlib.h"   // time_us_64

// Partial-render draw buffers (double-buffered), in SRAM rather than PSRAM.
// LVGL's CPU-side software rendering into a PSRAM buffer measured ~2x
// slower than into SRAM (confirmed on-hardware: a 40-row PSRAM buffer cost
// ~95 ms per full tab-content redraw vs. ~55-63 ms for a 2-row SRAM buffer)
// -- PSRAM's per-access QSPI latency dominates over the extra flush-call
// overhead from smaller chunks, so the buffers stay in SRAM. Moving LVGL's
// object heap (LV_MEM_SIZE) into a PSRAM pool (lv_conf.h) freed ~88 KB of
// SRAM that used to hold that pool, so the row count grew from 2 to 40 --
// fewer, bigger flush/DMA chunks means less fixed per-chunk overhead
// (CASET/RASET reissue + CS/DC guard timing in st7796.c) per scroll redraw.
// Trimmed 40 -> 24 rows (2026-07-31, sidebar-menu nav restructure): enabling
// LV_USE_MENU (+ its hard LV_USE_IMAGE dependency, lv_conf.h) added ~18 KB of
// new copy_to_ram code, overflowing the fixed 512 KB SRAM budget by ~1 KB.
// Trimmed 24 -> 8 rows then back to 24 (LVGL v9.2.2 -> v9.3.0 bump, for
// native SVG support): the new LVGL release added enough copy_to_ram code to
// overflow RAM by 26140 bytes. LVGL's own static/global data footprint is
// negligible (its v9.3.0 growth is almost entirely .text), so there was no
// lv_conf.h LV_USE_* flag to flip off. Rather than permanently degrading
// this buffer further, moved can_link.c's 36.9 KB `s_dev` (a pure line-
// buffer struct, no DMA/ISR) to PSRAM instead, which freed enough to restore
// the original 24-row count with ~10 KB of headroom to spare. Re-check
// headroom via arm-none-eabi-size -A / the linker map after any future LVGL
// or feature changes.
#define LVGL_PORT_BUF_ROWS 24
#define LVGL_PORT_BUF_PX   (ST7796_W * LVGL_PORT_BUF_ROWS)
#define LVGL_PORT_BUF_BYTES ((size_t)LVGL_PORT_BUF_PX * sizeof(uint16_t))
static uint16_t s_buf1[LVGL_PORT_BUF_PX];
static uint16_t s_buf2[LVGL_PORT_BUF_PX];

static lv_display_t *s_disp;
static lvgl_port_button_handler_t s_button_handler;


static uint32_t tick_get_cb(void) {
    return (uint32_t)(time_us_64() / 1000);
}

static void flush_done_cb(void) {
    lv_display_flush_ready(s_disp);
}

static void disp_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    (void)disp;
    // LVGL renders RGB565 in native (little-endian) byte order; the ST7796
    // driver's blit/flush API wants big-endian (wire order) 16-bit words.
    int32_t px_count = lv_area_get_width(area) * lv_area_get_height(area);
    lv_draw_sw_rgb565_swap(px_map, (uint32_t)px_count);

    st7796_flush_async((uint16_t)area->x1, (uint16_t)area->y1,
                        (uint16_t)area->x2, (uint16_t)area->y2,
                        (const uint16_t *)px_map, flush_done_cb);
}

static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data) {
    (void)indev;
    uint16_t x, y;
    if (ft6336_poll(&x, &y)) {
        data->point.x = x;
        data->point.y = y;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

// DPAD = uartkbd's 5-way nav cluster (UP/DOWN/LEFT/RIGHT/CENTER). The other
// uartkbd buttons (colors, HOME, OK, CANCEL, PAGE) aren't part of the pad and
// are left for a future indev/shortcut if this app ever needs them.
//
// A LVGL keypad indev only special-cases LV_KEY_NEXT/PREV/ENTER at the core
// dispatch level (lv_indev.c indev_keypad_proc) to move group focus -- plain
// LV_KEY_LEFT/RIGHT are just forwarded to whatever's *currently* focused as
// a raw key event instead. UP/DOWN drive list navigation; LEFT/RIGHT pass
// through so sliders/switches/dropdowns (ui_controls.c, ui_messages.c,
// ui_transmit.c) can use their own built-in LEFT/RIGHT handling to adjust
// values instead of stealing the same prev/next axis as UP/DOWN.
static uint32_t dpad_key(uartkbd_btn_t btn) {
    switch (btn) {
        case UARTKBD_BTN_NAV_UP:     return LV_KEY_PREV;
        case UARTKBD_BTN_NAV_DOWN:   return LV_KEY_NEXT;
        case UARTKBD_BTN_NAV_LEFT:   return LV_KEY_LEFT;
        case UARTKBD_BTN_NAV_RIGHT:  return LV_KEY_RIGHT;
        case UARTKBD_BTN_NAV_CENTER: return LV_KEY_ENTER;
        default:                    return 0;
    }
}

static void dpad_read_cb(lv_indev_t *indev, lv_indev_data_t *data) {
    (void)indev;
    uartkbd_event_t ev;
    // Report one edge per read; any events left queued behind it just wait
    // for the indev's next ~30 ms poll (uartkbd's ring holds 8, plenty for
    // human button timing).
    while (uartkbd_next_event(&ev)) {
        uint32_t key = dpad_key(ev.btn);
        if (!key) {
            if (s_button_handler) s_button_handler(ev.btn, ev.pressed);
            continue;
        }
        data->key = key;
        data->state = ev.pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
        return;
    }
}

void lvgl_port_set_button_handler(lvgl_port_button_handler_t handler) {
    s_button_handler = handler;
}

void lvgl_port_init(void) {
    size_t psram_bytes = psram_init();
    DIAG("lvgl_port: psram=%u bytes (need %u for draw buffers)\n",
         (unsigned)psram_bytes, (unsigned)(2 * LVGL_PORT_BUF_BYTES));

    lv_init();
    lv_tick_set_cb(tick_get_cb);

    s_disp = lv_display_create(ST7796_W, ST7796_H);
    lv_display_set_color_format(s_disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(s_disp, disp_flush_cb);
    lv_display_set_buffers(s_disp, s_buf1, s_buf2, LVGL_PORT_BUF_BYTES,
                            LV_DISPLAY_RENDER_MODE_PARTIAL);

    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touch_read_cb);

    // Default group: every focusable widget created from here on (buttons,
    // list rows, menu rows, the file explorer's table/buttonmatrix, ...)
    // auto-joins it (lv_obj_class_init_obj). lv_menu parks inactive pages
    // under a LV_OBJ_FLAG_HIDDEN storage container, and lv_group skips any
    // object with a hidden ancestor, so DPAD focus naturally stays confined
    // to whatever sidebar rows + content page are actually on screen.
    lv_group_t *group = lv_group_create();
    lv_group_set_default(group);

    lv_indev_t *dpad = lv_indev_create();
    lv_indev_set_type(dpad, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(dpad, dpad_read_cb);
    lv_indev_set_group(dpad, group);
}
