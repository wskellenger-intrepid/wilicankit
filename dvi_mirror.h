// apps/wilicankit/dvi_mirror.h — mirrors the LVGL LCD UI out over the FreeWili
// 2's DVI/HSTX port (GPIO 12-19), reusing wilibsp's zero-IRQ plain-DVI scanout
// driver (display/hstx_dvi.h). Call dvi_mirror_init() once after clk_sys is at
// its final value (main.c, after psram_clock_raise_250()); call
// dvi_mirror_flush() from lvgl_port.c's flush callback, before any byte-swap
// (HSTX wants the same native little-endian RGB565 LVGL renders internally).
#ifndef WILICANKIT_DVI_MIRROR_H
#define WILICANKIT_DVI_MIRROR_H
#include "lvgl.h"

// Brings up the HSTX DVI scanout at the LCD's native 480x320 resolution
// (letterboxed in the fixed 640x480p60 DVI frame) and enables mirroring.
void dvi_mirror_init(void);

// Copies the just-rendered chunk into the DVI framebuffer. No-op while
// disabled. `px_map` must still be native little-endian RGB565 (call before
// lv_draw_sw_rgb565_swap()).
void dvi_mirror_flush(const lv_area_t *area, const uint16_t *px_map);

// Blanks/unblanks the DVI DATA lanes (CLK lane stays live so a monitor keeps
// sync). Forces a full-screen redraw on re-enable so the mirror doesn't show
// stale content from while it was off.
void dvi_mirror_set_enabled(bool on);

bool dvi_mirror_enabled(void);

#endif // WILICANKIT_DVI_MIRROR_H
