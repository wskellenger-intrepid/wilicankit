// apps/wilicankit/ui_transmit.c — see ui_transmit.h.
#include "ui_transmit.h"
#include <stdbool.h>
#include "ui_common.h"
#include "app_state.h"
#include "can_link.h"
#include <stdio.h>
#include <stdlib.h>

static lv_obj_t *s_container;

// Periodic-rate dropdown: fixed presets (ms) followed by a "Custom" entry.
// TX_CUSTOM_IDX is that trailing option's index into TX_PRESET_MS/opts.
static const uint16_t TX_PRESET_MS[] = {10, 20, 50, 100, 200, 1000};
#define TX_PRESET_COUNT ((int)(sizeof(TX_PRESET_MS) / sizeof(TX_PRESET_MS[0])))
#define TX_CUSTOM_IDX   TX_PRESET_COUNT
static const char *TX_DROPDOWN_OPTS = "10 ms\n20 ms\n50 ms\n100 ms\n200 ms\n1000 ms\nCustom";

// Index of the preset matching ms, or TX_CUSTOM_IDX if none match.
static int preset_index_for_ms(uint32_t ms) {
    for (int i = 0; i < TX_PRESET_COUNT; i++) if (TX_PRESET_MS[i] == ms) return i;
    return TX_CUSTOM_IDX;
}

// Queues the periodic arm/update-or-disable for the next can_link_poll()
// pass instead of blocking here — see can_link.h's "Deferred requests".
static void apply_periodic(int idx) {
    can_link_request_periodic_update((uint8_t)idx);
}

// Dropdown changed: a preset applies its period immediately and rebuilds
// the row (which locks the textbox and shows the new value); "Custom"
// unlocks the textbox for free entry (applied on defocus by
// custom_period_ta_defocus_cb below) without touching the period yet.
static void period_dropdown_cb(lv_event_t *e) {
    lv_obj_t *dd = lv_event_get_target(e);
    lv_obj_t *ta = (lv_obj_t *)lv_event_get_user_data(e);
    uint16_t sel = lv_dropdown_get_selected(dd);

    if (sel == TX_CUSTOM_IDX) {
        lv_obj_remove_state(ta, LV_STATE_DISABLED);
        return;
    }
    int idx = (int)(intptr_t)lv_obj_get_user_data(dd);
    g_messages[idx].period_us = (uint32_t)TX_PRESET_MS[sel] * 1000u;
    apply_periodic(idx);
    ui_transmit_refresh();
}

// Custom period textbox loses focus: commit its value, but only while the
// dropdown is still on "Custom" (it's LV_STATE_DISABLED otherwise, so this
// mainly guards against a stray defocus during the dropdown's own update).
static void custom_period_ta_defocus_cb(lv_event_t *e) {
    lv_obj_t *ta = lv_event_get_target(e);
    lv_obj_t *dd = (lv_obj_t *)lv_event_get_user_data(e);
    if (lv_dropdown_get_selected(dd) != TX_CUSTOM_IDX) return;
    int idx = (int)(intptr_t)lv_obj_get_user_data(ta);
    long ms = strtol(lv_textarea_get_text(ta), NULL, 10);
    if (ms < 0) ms = 0;
    g_messages[idx].period_us = (uint32_t)ms * 1000u;
    apply_periodic(idx);
}

// Enabled toggle: arms/disarms periodic transmission and grays out "Send"
// while periodic is active (they're mutually exclusive on the wire).
static void enabled_switch_cb(lv_event_t *e) {
    lv_obj_t *sw = lv_event_get_target(e);
    lv_obj_t *send_btn = (lv_obj_t *)lv_event_get_user_data(e);
    int idx = (int)(intptr_t)lv_obj_get_user_data(sw);
    bool on = lv_obj_has_state(sw, LV_STATE_CHECKED);

    g_messages[idx].enabled = on;
    apply_periodic(idx);
    if (on) lv_obj_add_state(send_btn, LV_STATE_DISABLED);
    else    lv_obj_remove_state(send_btn, LV_STATE_DISABLED);
}

static void send_once_btn_cb(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    can_link_request_send_once((uint8_t)idx);
}

void ui_transmit_refresh(void) {
    if (!s_container) return;
    lv_obj_clean(s_container);
    static char summary[80];
    static char period_ms_buf[16];

    for (int i = 0; i < CAN_MAX_MESSAGES; i++) {
        if (!g_messages[i].in_use) continue;
        can_message_t *m = &g_messages[i];

        lv_obj_t *card = lv_obj_create(s_container);
        lv_obj_set_width(card, LV_PCT(100));
        lv_obj_set_height(card, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_all(card, 4, 0);
        lv_obj_set_style_pad_row(card, 4, 0);

        lv_obj_t *lbl = lv_label_create(card);
        snprintf(summary, sizeof summary, "%s%s0x%03lX  DLC=%u  ch%u  period=%lu ms  %s",
                 m->name, m->name[0] ? "  " : "",
                 (unsigned long)m->can_id, m->dlc, m->channel,
                 (unsigned long)(m->period_us / 1000u), m->enabled ? "ENABLED" : "disabled");
        lv_label_set_text(lbl, summary);

        lv_obj_t *row = lv_obj_create(card);
        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_height(row, LV_SIZE_CONTENT);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW_WRAP);
        lv_obj_set_style_pad_column(row, 4, 0);
        lv_obj_set_style_pad_row(row, 4, 0);

        int preset_idx = preset_index_for_ms(m->period_us / 1000u);

        lv_obj_t *dd = lv_dropdown_create(row);
        lv_dropdown_set_options(dd, TX_DROPDOWN_OPTS);
        lv_dropdown_set_selected(dd, (uint16_t)preset_idx);
        lv_obj_set_width(dd, 90);
        lv_obj_set_user_data(dd, (void *)(intptr_t)i);

        lv_obj_t *ta = lv_textarea_create(row);
        lv_textarea_set_one_line(ta, true);
        lv_obj_set_width(ta, 60);
        snprintf(period_ms_buf, sizeof period_ms_buf, "%lu", (unsigned long)(m->period_us / 1000u));
        lv_textarea_set_text(ta, period_ms_buf);
        lv_obj_set_user_data(ta, (void *)(intptr_t)i);
        ui_common_bind_keyboard(ta, LV_KEYBOARD_MODE_NUMBER);
        if (preset_idx != TX_CUSTOM_IDX) lv_obj_add_state(ta, LV_STATE_DISABLED);

        lv_obj_add_event_cb(dd, period_dropdown_cb, LV_EVENT_VALUE_CHANGED, ta);
        lv_obj_add_event_cb(ta, custom_period_ta_defocus_cb, LV_EVENT_DEFOCUSED, dd);

        lv_obj_t *cb = lv_switch_create(row);
        if (m->enabled) lv_obj_add_state(cb, LV_STATE_CHECKED);
        lv_obj_set_user_data(cb, (void *)(intptr_t)i);
        lv_label_set_text(lv_label_create(row), "enabled");

        lv_obj_t *send_btn = lv_button_create(row);
        lv_obj_add_event_cb(send_btn, send_once_btn_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_label_set_text(lv_label_create(send_btn), "Send");
        if (m->enabled) lv_obj_add_state(send_btn, LV_STATE_DISABLED);

        lv_obj_add_event_cb(cb, enabled_switch_cb, LV_EVENT_VALUE_CHANGED, send_btn);
    }
}

lv_obj_t *ui_transmit_create(lv_obj_t *parent) {
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);

    s_container = lv_obj_create(parent);
    lv_obj_set_width(s_container, LV_PCT(100));
    lv_obj_set_flex_grow(s_container, 1);
    lv_obj_set_flex_flow(s_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_container, 4, 0);

    ui_transmit_refresh();
    return parent;
}
