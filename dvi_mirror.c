// apps/wilicankit/dvi_mirror.c — see dvi_mirror.h.
#include "dvi_mirror.h"
#include "fw2.h"
#include <string.h>

static bool s_enabled = true;

void dvi_mirror_init(void) {
    // ST7796_W x ST7796_H (480x320) is exactly HSTX_VID_W_MAX x HSTX_VID_H_MAX
    // -- no scaling, the whole LCD image is centered as-is in the 640x480 frame.
    hstx_dvi_init(ST7796_W, ST7796_H);
}

void dvi_mirror_flush(const lv_area_t *area, const uint16_t *px_map) {
    if (!s_enabled) return;

    int stride = hstx_dvi_video_stride();
    uint16_t *base = hstx_dvi_video_base();
    int32_t w = lv_area_get_width(area);
    for (int32_t row = 0; row <= area->y2 - area->y1; row++) {
        uint16_t *dst = base + (size_t)(area->y1 + row) * stride + area->x1;
        const uint16_t *src = px_map + (size_t)row * w;
        memcpy(dst, src, (size_t)w * sizeof(uint16_t));
    }
}

void dvi_mirror_set_enabled(bool on) {
    s_enabled = on;
    hstx_dvi_enable(on);
    if (on) {
        // The mirror copy was skipped while off; resync the whole frame
        // instead of waiting for the next partial redraw to touch every row.
        lv_obj_invalidate(lv_screen_active());
    }
}

bool dvi_mirror_enabled(void) {
    return s_enabled;
}
