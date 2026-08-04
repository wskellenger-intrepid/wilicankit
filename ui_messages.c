// apps/wilicankit/ui_messages.c — see ui_messages.h.
#include "ui_messages.h"
#include "ui_common.h"
#include "ui_shell.h"
#include "app_state.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static lv_obj_t *s_list;
static lv_obj_t *s_form_page;
static lv_obj_t *s_signals_page;
static lv_obj_t *s_placement_page;
static lv_obj_t *s_placement_signal_slot;   // cleaned+rebuilt per open: label (edit) or dropdown (add)
static int       s_edit_index = -1;
static can_message_t s_edit_msg;   // scratch copy; only committed on Save

static lv_obj_t *s_ta_id, *s_sw_ext, *s_ta_dlc, *s_ta_channel, *s_ta_name;
static lv_obj_t *s_placement_list;
static lv_obj_t *s_dd_signal, *s_ta_start_bit, *s_sw_big_endian;
static uint8_t   s_dd_signal_ids[CAN_MAX_SIGNALS];
static int       s_edit_placement_index = -1;   // -1 = adding a new placement

static void open_message_form(int edit_index);
static void open_signals_modal(void);
static void open_placement_modal(int edit_placement_index);
static void refresh_placement_list(void);
static void placement_row_click_cb(lv_event_t *e);

// --- message list (top level) ---

static void fmt_msg_row(char *out, size_t cap, const can_message_t *m) {
    snprintf(out, cap, "%s%s0x%03lX  %s  DLC=%u  ch%u  %u sig",
             m->name, m->name[0] ? "  " : "",
             (unsigned long)m->can_id, m->extended_id ? "EXT" : "STD",
             m->dlc, m->channel, m->placement_count);
}

static void message_row_click_cb(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    open_message_form(idx);
}

static int cmp_message_name(const void *a, const void *b) {
    return strcmp(g_messages[*(const uint8_t *)a].name, g_messages[*(const uint8_t *)b].name);
}

void ui_messages_refresh(void) {
    if (!s_list) return;
    lv_obj_clean(s_list);
    static char row_buf[64];
    static uint8_t order[CAN_MAX_MESSAGES];
    int n = 0;
    for (int i = 0; i < CAN_MAX_MESSAGES; i++) {
        if (g_messages[i].in_use) order[n++] = (uint8_t)i;
    }
    qsort(order, n, sizeof(order[0]), cmp_message_name);
    for (int k = 0; k < n; k++) {
        int i = order[k];
        fmt_msg_row(row_buf, sizeof row_buf, &g_messages[i]);
        lv_obj_t *btn = lv_list_add_button(s_list, LV_SYMBOL_EDIT, row_buf);
        lv_obj_add_event_cb(btn, message_row_click_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
}

// --- placement sub-list within the message form ---

static void fmt_placement_row(char *out, size_t cap, const can_placement_t *p) {
    const can_signal_t *sig = &g_signals[p->signal_id];
    snprintf(out, cap, "%s  @bit%u  %s  (tap to edit)",
             sig->name, p->start_bit, p->big_endian ? "MSB-first (Moto)" : "LSB-first (Intel)");
}

static void refresh_placement_list(void) {
    lv_obj_clean(s_placement_list);
    static char row_buf[64];
    for (uint8_t i = 0; i < s_edit_msg.placement_count; i++) {
        if (s_edit_msg.placements[i].signal_id == CAN_SIGNAL_ID_NONE) continue;
        fmt_placement_row(row_buf, sizeof row_buf, &s_edit_msg.placements[i]);
        lv_obj_t *btn = lv_list_add_button(s_placement_list, LV_SYMBOL_EDIT, row_buf);
        lv_obj_add_event_cb(btn, placement_row_click_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
}

static void placement_row_click_cb(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    open_placement_modal(idx);
}

static void rebuild_signal_dropdown(void) {
    static char opts[CAN_MAX_SIGNALS * (CAN_SIGNAL_NAME_MAX + 1)];
    opts[0] = '\0';
    int n = 0;
    for (int i = 0; i < CAN_MAX_SIGNALS; i++) {
        if (!g_signals[i].in_use) continue;
        if (n > 0) strncat(opts, "\n", sizeof(opts) - strlen(opts) - 1);
        strncat(opts, g_signals[i].name, sizeof(opts) - strlen(opts) - 1);
        s_dd_signal_ids[n] = (uint8_t)i;
        n++;
    }
    if (n == 0) strncpy(opts, "(create a signal first)", sizeof(opts) - 1);
    lv_dropdown_set_options(s_dd_signal, opts);
}

static void placement_add(void) {
    uint16_t sel = lv_dropdown_get_selected(s_dd_signal);
    if (sel >= CAN_MAX_SIGNALS) return;
    uint8_t signal_id = s_dd_signal_ids[sel];
    if (!g_signals[signal_id].in_use) return;   // "(create a signal first)" placeholder selected
    if (s_edit_msg.placement_count >= CAN_MAX_PLACEMENTS) return;

    long start_bit = strtol(lv_textarea_get_text(s_ta_start_bit), NULL, 10);
    if (start_bit < 0) start_bit = 0;
    if (start_bit > 63) start_bit = 63;

    can_placement_t *p = &s_edit_msg.placements[s_edit_msg.placement_count++];
    p->signal_id = signal_id;
    p->start_bit = (uint8_t)start_bit;
    p->big_endian = lv_obj_has_state(s_sw_big_endian, LV_STATE_CHECKED);

    refresh_placement_list();
    ui_shell_close(1);   // back to the Signals screen
}

static void placement_edit_save(void) {
    can_placement_t *p = &s_edit_msg.placements[s_edit_placement_index];

    long start_bit = strtol(lv_textarea_get_text(s_ta_start_bit), NULL, 10);
    if (start_bit < 0) start_bit = 0;
    if (start_bit > 63) start_bit = 63;
    p->start_bit = (uint8_t)start_bit;
    p->big_endian = lv_obj_has_state(s_sw_big_endian, LV_STATE_CHECKED);

    refresh_placement_list();
    ui_shell_close(1);   // back to the Signals screen
}

static void placement_delete_confirmed(void *user_data) {
    (void)user_data;
    int idx = s_edit_placement_index;
    // compact: shift everything after idx down by one
    for (int i = idx; i + 1 < s_edit_msg.placement_count; i++)
        s_edit_msg.placements[i] = s_edit_msg.placements[i + 1];
    if (s_edit_msg.placement_count > 0) s_edit_msg.placement_count--;

    refresh_placement_list();
    ui_shell_close(2);   // confirm + placement form -> back to the Signals screen
}

static void placement_delete_prompt(void) {
    ui_common_show_confirm("Remove this signal from the message?", "Delete", placement_delete_confirmed, NULL);
}

// --- add/edit-placement screen (drilled down from the Signals screen;
// holds just the signal (dropdown when adding, fixed label when editing) +
// start-bit + endian) ---

static void open_placement_modal(int edit_placement_index) {
    s_edit_placement_index = edit_placement_index;
    const can_placement_t *existing = edit_placement_index >= 0
        ? &s_edit_msg.placements[edit_placement_index] : NULL;

    lv_obj_clean(s_placement_signal_slot);
    if (existing) {
        lv_obj_t *sig_lbl = lv_label_create(s_placement_signal_slot);
        lv_label_set_text(sig_lbl, g_signals[existing->signal_id].name);
    } else {
        s_dd_signal = lv_dropdown_create(s_placement_signal_slot);
        lv_obj_set_width(s_dd_signal, LV_PCT(100));
        rebuild_signal_dropdown();
    }

    if (existing) {
        static char tmp[8];
        snprintf(tmp, sizeof tmp, "%u", existing->start_bit);
        lv_textarea_set_text(s_ta_start_bit, tmp);
        if (existing->big_endian) lv_obj_add_state(s_sw_big_endian, LV_STATE_CHECKED);
        else lv_obj_remove_state(s_sw_big_endian, LV_STATE_CHECKED);
    } else {
        lv_textarea_set_text(s_ta_start_bit, "0");
        lv_obj_remove_state(s_sw_big_endian, LV_STATE_CHECKED);
    }

    ui_shell_open_form(existing ? "Edit Signal Placement" : "Add Signal to Message", s_placement_page,
                        existing ? "Save" : "Add",
                        existing ? placement_edit_save : placement_add,
                        NULL, NULL, existing ? placement_delete_prompt : NULL);
}

static void signals_add_signal(void) {
    open_placement_modal(-1);
}

// --- Signals screen (placement list; opened via the message form's
// "Signals" bar slot, kept separate so the placement list gets its own
// comfortably-sized screen instead of being crammed under the message
// form's fields) ---

static void open_signals_modal(void) {
    ui_shell_open_form("Signals", s_signals_page, "Signal", signals_add_signal, NULL, NULL, NULL);
    refresh_placement_list();
}

static void form_manage_signals(void) {
    open_signals_modal();
}

// --- message form (top-level fields + Signals/Save/Delete via the bottom bar) ---

static void delete_confirmed(void *user_data) {
    int idx = (int)(intptr_t)user_data;
    app_state_free_message((uint8_t)idx);
    ui_shell_close(2);   // confirm + form -> back to BAR_MESSAGES
    ui_messages_refresh();
}

static void form_delete_prompt(void) {
    static char msg[64];
    const can_message_t *m = &g_messages[s_edit_index];
    snprintf(msg, sizeof msg, "Delete message '%s'? This cannot be undone.",
              m->name[0] ? m->name : "(unnamed)");
    ui_common_show_confirm(msg, "Delete", delete_confirmed, (void *)(intptr_t)s_edit_index);
}

static void form_save(void) {
    int idx = s_edit_index;
    if (idx < 0) idx = app_state_alloc_message();
    if (idx < 0) { ui_shell_close(1); return; }   // table full (v1: silently drop)

    can_message_t *m = &g_messages[idx];
    bool was_enabled = m->in_use ? m->enabled : false;
    uint32_t period_us = m->in_use ? m->period_us : 0;

    *m = s_edit_msg;
    m->in_use = true;
    m->enabled = was_enabled;     // preserved — owned by the Transmit tab
    m->period_us = period_us;     // preserved — owned by the Transmit tab
    m->can_id = (uint32_t)strtol(lv_textarea_get_text(s_ta_id), NULL, 16);
    m->extended_id = lv_obj_has_state(s_sw_ext, LV_STATE_CHECKED);
    long dlc = strtol(lv_textarea_get_text(s_ta_dlc), NULL, 10);
    if (dlc < 0) dlc = 0;
    if (dlc > 8) dlc = 8;
    m->dlc = (uint8_t)dlc;
    long channel = strtol(lv_textarea_get_text(s_ta_channel), NULL, 10);
    m->channel = (uint8_t)(channel < 0 ? 0 : channel);
    strncpy(m->name, lv_textarea_get_text(s_ta_name), CAN_MESSAGE_NAME_MAX - 1);
    m->name[CAN_MESSAGE_NAME_MAX - 1] = '\0';

    ui_shell_close(1);
    ui_messages_refresh();
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

static lv_obj_t *add_labeled_switch(lv_obj_t *parent, const char *label_txt) {
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

    return lv_switch_create(row);
}

static void open_message_form(int edit_index) {
    s_edit_index = edit_index;
    if (edit_index >= 0) {
        s_edit_msg = g_messages[edit_index];
    } else {
        memset(&s_edit_msg, 0, sizeof s_edit_msg);
        s_edit_msg.dlc = 8;
        for (int p = 0; p < CAN_MAX_PLACEMENTS; p++)
            s_edit_msg.placements[p].signal_id = CAN_SIGNAL_ID_NONE;
    }

    if (edit_index >= 0) {
        static char tmp[16];
        lv_textarea_set_text(s_ta_name, s_edit_msg.name);
        snprintf(tmp, sizeof tmp, "%lX", (unsigned long)s_edit_msg.can_id);
        lv_textarea_set_text(s_ta_id, tmp);
        if (s_edit_msg.extended_id) lv_obj_add_state(s_sw_ext, LV_STATE_CHECKED);
        else lv_obj_remove_state(s_sw_ext, LV_STATE_CHECKED);
        snprintf(tmp, sizeof tmp, "%u", s_edit_msg.dlc); lv_textarea_set_text(s_ta_dlc, tmp);
        snprintf(tmp, sizeof tmp, "%u", s_edit_msg.channel); lv_textarea_set_text(s_ta_channel, tmp);
    } else {
        lv_textarea_set_text(s_ta_name, "");
        lv_textarea_set_text(s_ta_id, "100");
        lv_obj_remove_state(s_sw_ext, LV_STATE_CHECKED);
        lv_textarea_set_text(s_ta_dlc, "8");
        lv_textarea_set_text(s_ta_channel, "0");
    }

    ui_shell_open_form(edit_index < 0 ? "Add Message" : "Edit Message", s_form_page, "Save", form_save, "Signals", form_manage_signals,
                        edit_index >= 0 ? form_delete_prompt : NULL);
}

void ui_messages_add(void) {
    open_message_form(-1);
}

lv_obj_t *ui_messages_create(lv_obj_t *parent) {
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);

    s_list = lv_list_create(parent);
    lv_obj_set_width(s_list, LV_PCT(100));
    lv_obj_set_flex_grow(s_list, 1);

    // --- Message Form (persistent, built once) ---
    s_form_page = lv_obj_create(ui_shell_overlay());
    lv_obj_set_size(s_form_page, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_border_width(s_form_page, 0, 0);
    lv_obj_set_style_pad_row(s_form_page, 3, 0);
    lv_obj_set_flex_flow(s_form_page, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(s_form_page, LV_OBJ_FLAG_HIDDEN);

    s_ta_name = add_labeled_ta(s_form_page, "Name", LV_KEYBOARD_MODE_TEXT_LOWER);
    s_ta_id = add_labeled_ta(s_form_page, "CAN ID (hex)", LV_KEYBOARD_MODE_TEXT_LOWER);
    s_sw_ext = add_labeled_switch(s_form_page, "Extended (29b)");
    s_ta_dlc = add_labeled_ta(s_form_page, "DLC (0-8)", LV_KEYBOARD_MODE_NUMBER);
    s_ta_channel = add_labeled_ta(s_form_page, "Channel (0/1)", LV_KEYBOARD_MODE_NUMBER);

    // --- Signals screen (persistent, built once) ---
    s_signals_page = lv_obj_create(ui_shell_overlay());
    lv_obj_set_size(s_signals_page, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_border_width(s_signals_page, 0, 0);
    lv_obj_set_style_pad_row(s_signals_page, 4, 0);
    lv_obj_set_flex_flow(s_signals_page, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(s_signals_page, LV_OBJ_FLAG_HIDDEN);

    s_placement_list = lv_list_create(s_signals_page);
    lv_obj_set_width(s_placement_list, LV_PCT(100));
    lv_obj_set_flex_grow(s_placement_list, 1);   // fill remaining height instead of a fixed box

    // --- Placement Form (persistent, built once) ---
    s_placement_page = lv_obj_create(ui_shell_overlay());
    lv_obj_set_size(s_placement_page, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_border_width(s_placement_page, 0, 0);
    lv_obj_set_style_pad_row(s_placement_page, 6, 0);
    lv_obj_set_flex_flow(s_placement_page, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(s_placement_page, LV_OBJ_FLAG_HIDDEN);

    // Cleaned+rebuilt per open with either a fixed label (editing) or a
    // signal dropdown (adding) — see open_placement_modal.
    s_placement_signal_slot = lv_obj_create(s_placement_page);
    lv_obj_set_width(s_placement_signal_slot, LV_PCT(100));
    lv_obj_set_height(s_placement_signal_slot, LV_SIZE_CONTENT);
    lv_obj_set_style_border_width(s_placement_signal_slot, 0, 0);
    lv_obj_set_style_pad_all(s_placement_signal_slot, 0, 0);

    lv_obj_t *bit_row = lv_obj_create(s_placement_page);
    lv_obj_set_width(bit_row, LV_PCT(100));
    lv_obj_set_height(bit_row, LV_SIZE_CONTENT);
    lv_obj_set_style_border_width(bit_row, 0, 0);
    lv_obj_set_style_pad_all(bit_row, 0, 0);
    lv_obj_set_flex_flow(bit_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bit_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(bit_row, 6, 0);

    lv_obj_t *bit_lbl = lv_label_create(bit_row);
    lv_label_set_text(bit_lbl, "Start bit");

    s_ta_start_bit = lv_textarea_create(bit_row);
    lv_textarea_set_one_line(s_ta_start_bit, true);
    lv_obj_set_width(s_ta_start_bit, 60);
    ui_common_bind_keyboard(s_ta_start_bit, LV_KEYBOARD_MODE_NUMBER);

    lv_obj_t *endian_lbl = lv_label_create(bit_row);
    lv_label_set_text(endian_lbl, "MSB-first (Moto/Big endian)");

    s_sw_big_endian = lv_switch_create(bit_row);

    ui_messages_refresh();
    return parent;
}
