// apps/wilicankit/ui_common.c — see ui_common.h.
#include "ui_common.h"
#include "ui_shell.h"

static lv_obj_t *s_keyboard;

// The scrollable ancestor currently given extra bottom padding to make
// room to scroll a field above the keyboard (see scroll_ta_above_keyboard),
// and the padding value to restore once the keyboard closes.
static lv_obj_t *s_kb_scroller;
static int32_t   s_kb_scroller_pad_bottom;

static void restore_kb_scroller(void) {
    if (!s_kb_scroller) return;
    lv_obj_set_style_pad_bottom(s_kb_scroller, s_kb_scroller_pad_bottom, LV_PART_MAIN);
    lv_obj_scroll_to_y(s_kb_scroller, 0, LV_ANIM_OFF);
    s_kb_scroller = NULL;
}

static void kb_ready_cancel_cb(lv_event_t *e) {
    (void)e;
    lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_keyboard_set_textarea(s_keyboard, NULL);
    restore_kb_scroller();
}

// Nudges ta's nearest scrollable ancestor so ta clears the (floating,
// screen-bottom) keyboard — the keyboard overlays content rather than
// resizing anything, so the app bar/content/bottom bar never move. A form's
// fields often already fit within view with nothing to scroll, so extra
// bottom padding equal to the overlap is added first to give the scroll
// somewhere to go; restore_kb_scroller() removes it again.
static void scroll_ta_above_keyboard(lv_obj_t *ta) {
    restore_kb_scroller();

    lv_obj_update_layout(lv_obj_get_screen(ta));
    lv_area_t ta_a, kb_a;
    lv_obj_get_coords(ta, &ta_a);
    lv_obj_get_coords(s_keyboard, &kb_a);
    int32_t overlap = ta_a.y2 - kb_a.y1 + 8;
    if (overlap <= 0) return;

    lv_obj_t *scroller = lv_obj_get_parent(ta);
    // Skip content-hugging row wrappers (see add_labeled_ta) — padding
    // those just grows the row itself instead of creating scroll room.
    while (scroller && (!lv_obj_has_flag(scroller, LV_OBJ_FLAG_SCROLLABLE) ||
                         lv_obj_get_style_height(scroller, LV_PART_MAIN) == LV_SIZE_CONTENT)) {
        scroller = lv_obj_get_parent(scroller);
    }
    if (!scroller) return;

    s_kb_scroller = scroller;
    s_kb_scroller_pad_bottom = lv_obj_get_style_pad_bottom(scroller, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(scroller, s_kb_scroller_pad_bottom + overlap, LV_PART_MAIN);
    lv_obj_scroll_by_bounded(scroller, 0, -overlap, LV_ANIM_OFF);
}

static void ta_focus_cb(lv_event_t *e) {
    lv_obj_t *ta = lv_event_get_target(e);
    lv_keyboard_mode_t mode = (lv_keyboard_mode_t)(intptr_t)lv_event_get_user_data(e);
    lv_keyboard_set_mode(s_keyboard, mode);
    lv_keyboard_set_textarea(s_keyboard, ta);
    lv_obj_remove_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_keyboard);
    scroll_ta_above_keyboard(ta);
}

static void ta_defocus_cb(lv_event_t *e) {
    (void)e;
    lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_keyboard_set_textarea(s_keyboard, NULL);
    restore_kb_scroller();
}

void ui_common_init(lv_obj_t *screen) {
    s_keyboard = lv_keyboard_create(screen);
    lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s_keyboard, kb_ready_cancel_cb, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(s_keyboard, kb_ready_cancel_cb, LV_EVENT_CANCEL, NULL);
}

void ui_common_bind_keyboard(lv_obj_t *ta, lv_keyboard_mode_t mode) {
    lv_obj_add_event_cb(ta, ta_focus_cb, LV_EVENT_FOCUSED, (void *)(intptr_t)mode);
    lv_obj_add_event_cb(ta, ta_defocus_cb, LV_EVENT_DEFOCUSED, NULL);
}

// --- shared full-screen confirm (ui_shell_open_confirm-backed) ---
// Same one-at-a-time assumption as the modal version above; the page is
// created lazily on first use rather than up front in ui_shell_create(),
// since it has no dependency on any particular content module's page order.
static lv_obj_t            *s_confirm_page;
static lv_obj_t            *s_confirm_page_lbl;
static ui_confirm_action_t  s_confirm_page_action;
static void                *s_confirm_page_user_data;

static void confirm_page_action_cb(void) {
    if (s_confirm_page_action) s_confirm_page_action(s_confirm_page_user_data);
}

static void ensure_confirm_page(void) {
    if (s_confirm_page) return;
    s_confirm_page = lv_obj_create(ui_shell_overlay());
    lv_obj_set_size(s_confirm_page, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_border_width(s_confirm_page, 0, 0);
    lv_obj_add_flag(s_confirm_page, LV_OBJ_FLAG_HIDDEN);

    s_confirm_page_lbl = lv_label_create(s_confirm_page);
    lv_label_set_long_mode(s_confirm_page_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_confirm_page_lbl, LV_PCT(100));
}

void ui_common_show_confirm(const char *message, const char *action_label,
                             ui_confirm_action_t on_confirm, void *user_data) {
    ensure_confirm_page();
    s_confirm_page_action = on_confirm;
    s_confirm_page_user_data = user_data;
    lv_label_set_text(s_confirm_page_lbl, message);
    ui_shell_open_confirm(s_confirm_page, action_label, confirm_page_action_cb);
}
