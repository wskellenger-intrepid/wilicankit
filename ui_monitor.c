// apps/wilicankit/ui_monitor.c — Monitor area's Health leaf: live CAN-link
// stats drawn onto an lv_canvas, refreshed on a timer (see ui_monitor.h).
// Canvas (not lv_label) is deliberate here, ahead of strict need for Health
// alone — it's the first use of the canvas machinery a later live/scrolling
// message monitor view (stacked-by-ID, changed-byte highlighting) will build on.
#include "ui_monitor.h"
#include "can_link.h"
#include "can_model.h"
#include "app_state.h"

// Sized to fit inside the Health tab page's content box (480x320 screen,
// minus app bar/bottom bar/nested pad_all(6) chrome — see ui_shell.c);
// verify on-device and adjust if clipped.
#define MON_CANVAS_W 448
#define MON_CANVAS_H 192

static lv_obj_t *s_canvas;
static lv_obj_t *s_page; // Health tab page — gates the refresh timer while hidden

static void refresh_timer_cb(lv_timer_t *timer) {
    (void)timer;
    if (lv_obj_has_flag(s_page, LV_OBJ_FLAG_HIDDEN)) return;
    ui_monitor_refresh();
}

lv_obj_t *ui_monitor_create(lv_obj_t *parent) {
    s_page = parent;
    lv_obj_set_scrollbar_mode(parent, LV_SCROLLBAR_MODE_OFF);

    s_canvas = lv_canvas_create(parent);
    lv_draw_buf_t *buf = lv_draw_buf_create(MON_CANVAS_W, MON_CANVAS_H, LV_COLOR_FORMAT_RGB565, LV_STRIDE_AUTO);
    lv_canvas_set_draw_buf(s_canvas, buf);

    lv_timer_create(refresh_timer_cb, 250, NULL);
    ui_monitor_refresh();
    return parent;
}

void ui_monitor_refresh(void) {
    if (!s_canvas) return;
    lv_canvas_fill_bg(s_canvas, lv_color_black(), LV_OPA_COVER);

    lv_layer_t layer;
    lv_canvas_init_layer(s_canvas, &layer);

    lv_draw_label_dsc_t dsc;
    lv_draw_label_dsc_init(&dsc);
    int32_t line_h = lv_font_get_line_height(dsc.font) + 2;

    // lv_draw_label() only queues the draw task; text isn't rendered until
    // lv_canvas_finish_layer() below, so each row needs its own buffer --
    // a single shared buffer left every row showing the last sprintf's text.
    static char row_bufs[32][64];
    int32_t y = 0;
    int row_idx = 0;

// Each call draws one row and advances y; kept as a macro since every row
// shares the same coords-from-y/dsc-mutate/draw/advance sequence.
#define DRAW_LINE(color_val, ...) \
    do { \
        char *rowbuf = row_bufs[row_idx++]; \
        lv_snprintf(rowbuf, sizeof row_bufs[0], __VA_ARGS__); \
        dsc.text = rowbuf; \
        dsc.color = (color_val); \
        lv_area_t coords = { 0, y, MON_CANVAS_W - 1, y + line_h - 1 }; \
        lv_draw_label(&layer, &dsc, &coords); \
        y += line_h; \
    } while (0)

    bool online = can_link_is_online();
    DRAW_LINE(online ? lv_palette_main(LV_PALETTE_GREEN) : lv_palette_main(LV_PALETTE_RED),
              "Link: %s", online ? "Online" : "Offline");

    DRAW_LINE(lv_color_white(), "Fault streak: %u/%u",
              (unsigned)can_link_fault_streak(), (unsigned)can_link_fault_threshold());

    uint32_t h_att, h_ok;
    int32_t h_err;
    can_link_health_stats(&h_att, &h_ok, &h_err);
    DRAW_LINE(lv_color_white(), "Health probe: %u/%u (err %d)", (unsigned)h_ok, (unsigned)h_att, (int)h_err);

    DRAW_LINE(lv_color_white(), "Dropped frames: %u", (unsigned)can_link_dropped_frames());
    DRAW_LINE(lv_color_white(), "Messages RX: %u (not yet implemented)", (unsigned)can_link_rx_frames());

    uint32_t tx_att_total = 0, tx_ok_total = 0;
    int total_in_use = 0;
    for (int i = 0; i < CAN_MAX_MESSAGES; i++) {
        if (!g_messages[i].in_use) continue;
        tx_att_total += can_link_tx_attempts((uint8_t)i);
        tx_ok_total += can_link_tx_ok((uint8_t)i);
        total_in_use++;
    }
    DRAW_LINE(lv_color_white(), "TX total: %u/%u", (unsigned)tx_ok_total, (unsigned)tx_att_total);

    // Per-message breakdown, capped to whatever fits — no scrolling here yet.
    int32_t remaining_rows = (MON_CANVAS_H - y) / line_h;
    bool needs_more_row = total_in_use > remaining_rows;
    int32_t shown_cap = needs_more_row ? remaining_rows - 1 : remaining_rows;
    int shown = 0;
    for (int i = 0; i < CAN_MAX_MESSAGES && shown < shown_cap; i++) {
        if (!g_messages[i].in_use) continue;
        DRAW_LINE(lv_color_white(), "%s: %u/%u", g_messages[i].name,
                  (unsigned)can_link_tx_ok((uint8_t)i), (unsigned)can_link_tx_attempts((uint8_t)i));
        shown++;
    }
    if (total_in_use > shown) {
        DRAW_LINE(lv_color_white(), "+%d more", total_in_use - shown);
    }

#undef DRAW_LINE

    lv_canvas_finish_layer(s_canvas, &layer);
}
