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

// Fixed rows always drawn before the per-message rows (keep in sync with
// the DRAW_LINE calls at the top of ui_monitor_refresh()).
#define MON_HEADER_ROWS 6

static lv_obj_t *s_canvas;
static lv_obj_t *s_scroll;  // transparent overlay: just reuses LVGL's native drag/scrollbar handling
static lv_obj_t *s_spacer;  // sized to total content height so s_scroll knows how far it can scroll
static lv_obj_t *s_page;   // Health tab page — gates the refresh timer while hidden

static void refresh_timer_cb(lv_timer_t *timer) {
    (void)timer;
    if (lv_obj_has_flag(s_page, LV_OBJ_FLAG_HIDDEN)) return;
    ui_monitor_refresh();
}

static void scroll_event_cb(lv_event_t *e) {
    (void)e;
    ui_monitor_refresh();
}

lv_obj_t *ui_monitor_create(lv_obj_t *parent) {
    s_page = parent;
    lv_obj_set_scrollbar_mode(parent, LV_SCROLLBAR_MODE_OFF);

    s_canvas = lv_canvas_create(parent);
    lv_draw_buf_t *buf = lv_draw_buf_create(MON_CANVAS_W, MON_CANVAS_H, LV_COLOR_FORMAT_RGB565, LV_STRIDE_AUTO);
    lv_canvas_set_draw_buf(s_canvas, buf);

    // Scroll position picks which window of rows gets drawn into the canvas
    // (virtual scrolling) — rows are never real widgets, so this stays cheap
    // no matter how long the message list gets. Sits on top of the canvas,
    // fully transparent, purely to reuse LVGL's drag/scrollbar/momentum code.
    s_scroll = lv_obj_create(parent);
    lv_obj_set_size(s_scroll, MON_CANVAS_W, MON_CANVAS_H);
    lv_obj_set_pos(s_scroll, 0, 0);
    lv_obj_set_style_pad_all(s_scroll, 0, 0);
    lv_obj_set_style_border_width(s_scroll, 0, 0);
    lv_obj_set_style_radius(s_scroll, 0, 0);
    lv_obj_set_style_bg_opa(s_scroll, LV_OPA_TRANSP, 0);
    lv_obj_set_scroll_dir(s_scroll, LV_DIR_VER);
    lv_obj_add_event_cb(s_scroll, scroll_event_cb, LV_EVENT_SCROLL, NULL);

    s_spacer = lv_obj_create(s_scroll);
    lv_obj_set_size(s_spacer, 1, MON_CANVAS_H);
    lv_obj_set_pos(s_spacer, 0, 0);
    lv_obj_set_style_bg_opa(s_spacer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_spacer, 0, 0);
    lv_obj_remove_flag(s_spacer, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    lv_timer_create(refresh_timer_cb, 250, NULL);
    ui_monitor_refresh();
    return parent;
}

void ui_monitor_refresh(void) {
    if (!s_canvas) return;

    lv_draw_label_dsc_t dsc;
    lv_draw_label_dsc_init(&dsc);
    int32_t line_h = lv_font_get_line_height(dsc.font) + 2;
    int32_t visible_rows = MON_CANVAS_H / line_h;

    uint32_t tx_att_total = 0, tx_ok_total = 0;
    int total_in_use = 0;
    for (int i = 0; i < CAN_MAX_MESSAGES; i++) {
        if (!g_messages[i].in_use) continue;
        tx_att_total += can_link_tx_attempts((uint8_t)i);
        tx_ok_total += can_link_tx_ok((uint8_t)i);
        total_in_use++;
    }

    // Grow the (invisible) spacer to match the real row count so s_scroll's
    // native scrollbar/max-scroll-range tracks the current message list.
    int32_t total_rows = MON_HEADER_ROWS + total_in_use;
    int32_t max_scroll_row = total_rows > visible_rows ? total_rows - visible_rows : 0;
    // Content height is derived from max_scroll_row (not total_rows*line_h)
    // so the native max scroll_y lands exactly on a row boundary -- MON_CANVAS_H
    // isn't necessarily a whole multiple of line_h, so sizing from total_rows
    // left a few leftover pixels that floored row_offset one row short of the
    // true last page, hiding the final line until the drag was released.
    int32_t content_h = MON_CANVAS_H + max_scroll_row * line_h;
    lv_obj_set_height(s_spacer, content_h);

    int32_t row_offset = lv_obj_get_scroll_y(s_scroll) / line_h;
    if (row_offset > max_scroll_row) row_offset = max_scroll_row;
    if (row_offset < 0) row_offset = 0;

    lv_canvas_fill_bg(s_canvas, lv_color_black(), LV_OPA_COVER);

    lv_layer_t layer;
    lv_canvas_init_layer(s_canvas, &layer);

    // lv_draw_label() only queues the draw task; text isn't rendered until
    // lv_canvas_finish_layer() below, so each row needs its own buffer --
    // a single shared buffer left every row showing the last sprintf's text.
    static char row_bufs[32][64];
    int row_num = 0; // logical row index across the whole (unscrolled) list
    int drawn = 0;   // row_bufs/canvas slot index within the visible window

// Skips rows above the scroll offset and stops once the canvas is full;
// row_num still advances for skipped rows so scrolling stays in sync.
#define DRAW_LINE(color_val, ...) \
    do { \
        if (row_num >= row_offset && drawn < visible_rows) { \
            char *rowbuf = row_bufs[drawn]; \
            lv_snprintf(rowbuf, sizeof row_bufs[0], __VA_ARGS__); \
            dsc.text = rowbuf; \
            dsc.color = (color_val); \
            lv_area_t coords = { 0, drawn * line_h, MON_CANVAS_W - 1, drawn * line_h + line_h - 1 }; \
            lv_draw_label(&layer, &dsc, &coords); \
            drawn++; \
        } \
        row_num++; \
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
    DRAW_LINE(lv_color_white(), "TX total: %u/%u", (unsigned)tx_ok_total, (unsigned)tx_att_total);

    for (int i = 0; i < CAN_MAX_MESSAGES; i++) {
        if (!g_messages[i].in_use) continue;
        DRAW_LINE(lv_color_white(), "%s: %u/%u", g_messages[i].name,
                  (unsigned)can_link_tx_ok((uint8_t)i), (unsigned)can_link_tx_attempts((uint8_t)i));
    }

#undef DRAW_LINE

    lv_canvas_finish_layer(s_canvas, &layer);
}

