// apps/wilicankit/ui_signals.c — see ui_signals.h.
#include "ui_signals.h"
#include "ui_common.h"
#include "ui_shell.h"
#include "app_state.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static lv_obj_t *s_list;
static lv_obj_t *s_form_page;
static int       s_edit_index = -1;

static lv_obj_t *s_ta_name, *s_ta_units, *s_ta_bits, *s_ta_scale, *s_ta_offset, *s_ta_min, *s_ta_max;

static void open_signal_form(int edit_index);

static void fmt_row_text(char *out, size_t cap, const can_signal_t *s) {
    snprintf(out, cap, "%s  [%u bit]  x%.4g %+.4g %s",
             s->name, s->bit_length, s->scale, s->offset, s->units);
}

static void row_click_cb(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    open_signal_form(idx);
}

void ui_signals_add(void) {
    open_signal_form(-1);
}

void ui_signals_refresh(void) {
    if (!s_list) return;
    lv_obj_clean(s_list);
    static char row_buf[64];   // static: keep this off the (4 KB) stack

    // Display order is alphabetical by name; the underlying signal index
    // (used as row_click_cb's user_data, and everywhere else in the app)
    // is unchanged -- only the iteration/render order here is sorted.
    static uint8_t idxs[CAN_MAX_SIGNALS];
    int n = 0;
    for (int i = 0; i < CAN_MAX_SIGNALS; i++) {
        if (g_signals[i].in_use) idxs[n++] = (uint8_t)i;
    }
    for (int i = 1; i < n; i++) {
        uint8_t key = idxs[i];
        int j = i - 1;
        while (j >= 0 && strcmp(g_signals[idxs[j]].name, g_signals[key].name) > 0) {
            idxs[j + 1] = idxs[j];
            j--;
        }
        idxs[j + 1] = key;
    }

    for (int k = 0; k < n; k++) {
        int i = idxs[k];
        fmt_row_text(row_buf, sizeof row_buf, &g_signals[i]);
        lv_obj_t *btn = lv_list_add_button(s_list, LV_SYMBOL_EDIT, row_buf);
        lv_obj_add_event_cb(btn, row_click_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
}

static void delete_confirmed(void *user_data) {
    int idx = (int)(intptr_t)user_data;
    app_state_free_signal((uint8_t)idx);
    ui_shell_close(2);   // confirm + form -> back to BAR_SIGNALS
    ui_signals_refresh();
}

static void form_delete_prompt(void) {
    static char msg[80];
    snprintf(msg, sizeof msg, "Delete signal '%s'? Also removes it from any messages/controls using it.",
              g_signals[s_edit_index].name);
    ui_common_show_confirm(msg, "Delete", delete_confirmed, (void *)(intptr_t)s_edit_index);
}

static void form_save(void) {
    int idx = s_edit_index;
    if (idx < 0) idx = app_state_alloc_signal();
    if (idx < 0) { ui_shell_close(1); return; }   // table full (v1: silently drop)

    can_signal_t *s = &g_signals[idx];
    s->in_use = true;
    strncpy(s->name, lv_textarea_get_text(s_ta_name), CAN_SIGNAL_NAME_MAX - 1);
    s->name[CAN_SIGNAL_NAME_MAX - 1] = '\0';
    strncpy(s->units, lv_textarea_get_text(s_ta_units), CAN_SIGNAL_UNITS_MAX - 1);
    s->units[CAN_SIGNAL_UNITS_MAX - 1] = '\0';

    long bits = strtol(lv_textarea_get_text(s_ta_bits), NULL, 10);
    if (bits < 1) bits = 1;
    if (bits > 64) bits = 64;
    s->bit_length = (uint8_t)bits;
    s->scale = strtod(lv_textarea_get_text(s_ta_scale), NULL);
    s->offset = strtod(lv_textarea_get_text(s_ta_offset), NULL);
    s->min_value = strtod(lv_textarea_get_text(s_ta_min), NULL);
    s->max_value = strtod(lv_textarea_get_text(s_ta_max), NULL);
    if (s->value < s->min_value || s->value > s->max_value) s->value = s->min_value;

    ui_shell_close(1);
    ui_signals_refresh();
}

static lv_obj_t *add_labeled_ta(lv_obj_t *parent, const char *label_txt, lv_keyboard_mode_t kb_mode) {
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *lbl = lv_label_create(row);
    lv_label_set_text(lbl, label_txt);
    lv_obj_set_width(lbl, 90);

    lv_obj_t *ta = lv_textarea_create(row);
    lv_textarea_set_one_line(ta, true);
    lv_obj_set_flex_grow(ta, 1);
    ui_common_bind_keyboard(ta, kb_mode);
    return ta;
}

static void open_signal_form(int edit_index) {
    s_edit_index = edit_index;

    if (edit_index >= 0) {
        can_signal_t *s = &g_signals[edit_index];
        static char tmp[32];   // static: keep off the stack
        lv_textarea_set_text(s_ta_name, s->name);
        lv_textarea_set_text(s_ta_units, s->units);
        snprintf(tmp, sizeof tmp, "%u", s->bit_length); lv_textarea_set_text(s_ta_bits, tmp);
        snprintf(tmp, sizeof tmp, "%g", s->scale);      lv_textarea_set_text(s_ta_scale, tmp);
        snprintf(tmp, sizeof tmp, "%g", s->offset);     lv_textarea_set_text(s_ta_offset, tmp);
        snprintf(tmp, sizeof tmp, "%g", s->min_value);  lv_textarea_set_text(s_ta_min, tmp);
        snprintf(tmp, sizeof tmp, "%g", s->max_value);  lv_textarea_set_text(s_ta_max, tmp);
    } else {
        lv_textarea_set_text(s_ta_name, "");
        lv_textarea_set_text(s_ta_units, "");
        lv_textarea_set_text(s_ta_bits, "8");
        lv_textarea_set_text(s_ta_scale, "1");
        lv_textarea_set_text(s_ta_offset, "0");
        lv_textarea_set_text(s_ta_min, "0");
        lv_textarea_set_text(s_ta_max, "255");
    }

    ui_shell_open_form(edit_index < 0 ? "Add Signal" : "Edit Signal", s_form_page, "Save", form_save, NULL, NULL,
                        edit_index >= 0 ? form_delete_prompt : NULL);
}

lv_obj_t *ui_signals_create(lv_obj_t *parent) {
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);

    s_list = lv_list_create(parent);
    lv_obj_set_width(s_list, LV_PCT(100));
    lv_obj_set_flex_grow(s_list, 1);

    s_form_page = lv_obj_create(ui_shell_overlay());
    lv_obj_set_size(s_form_page, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_border_width(s_form_page, 0, 0);
    lv_obj_set_style_pad_row(s_form_page, 4, 0);
    lv_obj_set_flex_flow(s_form_page, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(s_form_page, LV_OBJ_FLAG_HIDDEN);

    s_ta_name   = add_labeled_ta(s_form_page, "Name",         LV_KEYBOARD_MODE_TEXT_LOWER);
    s_ta_units  = add_labeled_ta(s_form_page, "Units",        LV_KEYBOARD_MODE_TEXT_LOWER);
    s_ta_bits   = add_labeled_ta(s_form_page, "Bits (1-64)",  LV_KEYBOARD_MODE_NUMBER);
    s_ta_scale  = add_labeled_ta(s_form_page, "Scale",        LV_KEYBOARD_MODE_NUMBER);
    s_ta_offset = add_labeled_ta(s_form_page, "Offset",       LV_KEYBOARD_MODE_NUMBER);
    s_ta_min    = add_labeled_ta(s_form_page, "Min (phys)",   LV_KEYBOARD_MODE_NUMBER);
    s_ta_max    = add_labeled_ta(s_form_page, "Max (phys)",   LV_KEYBOARD_MODE_NUMBER);

    ui_signals_refresh();
    return parent;
}
