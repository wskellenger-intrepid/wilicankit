// apps/wilicankit/ui_controls.c — see ui_controls.h.
#include "ui_controls.h"
#include "ui_common.h"
#include "ui_shell.h"
#include "app_state.h"
#include "can_link.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SLIDER_RESOLUTION 1000
#define SLIDER_PUSH_MIN_INTERVAL_MS 50

static lv_obj_t *s_container;
static lv_obj_t *s_form_page;
static lv_obj_t *s_dd_signal;
static uint8_t   s_dd_signal_ids[CAN_MAX_SIGNALS];
static uint8_t   s_adding_control_type;   // CAN_CONTROL_SLIDER/TOGGLE for the open Add form
static uint32_t  s_last_push_ms[CAN_MAX_CONTROLS];

// Re-sends every enabled message that has `signal_id` placed in it, right
// now, with its payload rebuilt from current signal values. Uses an
// immediate one-shot TX (can_link_request_send_once) rather than
// can_link_request_periodic_update: the latter resets the message's
// periodic timer (see can_link.c), which just pushes the next real
// periodic send further into the future instead of sending now — that was
// the root cause of slider/toggle changes not reaching the bus until the
// slider was released (the timer kept getting reset every throttled call
// during the drag). Queuing (see can_link.h's "Deferred requests") avoids
// blocking on a FwGUI round-trip directly in this event callback, which
// would freeze slider dragging for its duration.
static void push_signal_updates(uint8_t signal_id) {
    for (int m = 0; m < CAN_MAX_MESSAGES; m++) {
        can_message_t *msg = &g_messages[m];
        if (!msg->in_use || !msg->enabled) continue;
        for (int p = 0; p < msg->placement_count; p++) {
            if (msg->placements[p].signal_id == signal_id) {
                can_link_request_send_once((uint8_t)m);
                break;
            }
        }
    }
}

static void remove_control_confirmed(void *user_data) {
    int idx = (int)(intptr_t)user_data;
    app_state_free_control((uint8_t)idx);
    ui_shell_close(1);   // confirm -> back to BAR_CONTROLS
    ui_controls_refresh();
}

static void remove_control_btn_cb(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    uint8_t sig_id = g_controls[idx].signal_id;
    const char *sig_name = (sig_id != CAN_SIGNAL_ID_NONE && g_signals[sig_id].in_use)
        ? g_signals[sig_id].name : "(no signal)";
    const char *kind = (g_controls[idx].control_type == CAN_CONTROL_TOGGLE) ? "Toggle" : "Slider";
    static char msg[64];
    snprintf(msg, sizeof msg, "Remove the %s for '%s'?", kind, sig_name);
    ui_common_show_confirm(msg, "Remove", remove_control_confirmed, (void *)(intptr_t)idx);
}

static void slider_value_changed_cb(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    lv_obj_t *sl = lv_event_get_target(e);
    uint8_t sig_id = g_controls[idx].signal_id;
    if (sig_id == CAN_SIGNAL_ID_NONE || !g_signals[sig_id].in_use) return;
    can_signal_t *sig = &g_signals[sig_id];

    int32_t pos = lv_slider_get_value(sl);
    double physical = sig->min_value + ((double)pos / SLIDER_RESOLUTION) * (sig->max_value - sig->min_value);
    sig->value = physical;

    lv_obj_t *lbl = (lv_obj_t *)lv_obj_get_user_data(sl);
    if (lbl) {
        static char buf[48];
        snprintf(buf, sizeof buf, "%s: %.3g %s", sig->name, physical, sig->units);
        lv_label_set_text(lbl, buf);
    }

    // Throttled to SLIDER_PUSH_MIN_INTERVAL_MS during a drag (VALUE_CHANGED
    // fires continuously while dragging) to bound how often a one-shot TX
    // is queued; always push on RELEASED so the final position is never
    // dropped by the throttle window.
    bool due = (lv_event_get_code(e) == LV_EVENT_RELEASED);
    uint32_t now_ms = to_ms_since_boot(get_absolute_time());
    if (!due && (now_ms - s_last_push_ms[idx]) >= SLIDER_PUSH_MIN_INTERVAL_MS) due = true;
    if (due) {
        push_signal_updates(sig_id);
        s_last_push_ms[idx] = now_ms;
    }
}

static void toggle_value_changed_cb(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    lv_obj_t *sw = lv_event_get_target(e);
    uint8_t sig_id = g_controls[idx].signal_id;
    if (sig_id == CAN_SIGNAL_ID_NONE || !g_signals[sig_id].in_use) return;
    can_signal_t *sig = &g_signals[sig_id];

    bool on = lv_obj_has_state(sw, LV_STATE_CHECKED);
    sig->value = on ? sig->max_value : sig->min_value;

    lv_obj_t *lbl = (lv_obj_t *)lv_obj_get_user_data(sw);
    if (lbl) {
        static char buf[48];
        snprintf(buf, sizeof buf, "%s: %.3g %s", sig->name, sig->value, sig->units);
        lv_label_set_text(lbl, buf);
    }

    // A switch flip is a single discrete event (not a drag), so push
    // immediately every time — no throttle needed.
    push_signal_updates(sig_id);
}

static int cmp_control_slider_first(const void *a, const void *b) {
    uint8_t ia = *(const uint8_t *)a, ib = *(const uint8_t *)b;
    if (g_controls[ia].control_type != g_controls[ib].control_type)
        return (int)g_controls[ia].control_type - (int)g_controls[ib].control_type;
    return strcmp(g_signals[g_controls[ia].signal_id].name, g_signals[g_controls[ib].signal_id].name);
}

void ui_controls_refresh(void) {
    if (!s_container) return;
    lv_obj_clean(s_container);
    static char buf[48];
    static uint8_t order[CAN_MAX_CONTROLS];
    int n = 0;
    for (int i = 0; i < CAN_MAX_CONTROLS; i++) {
        if (!g_controls[i].in_use) continue;
        uint8_t sig_id = g_controls[i].signal_id;
        if (sig_id == CAN_SIGNAL_ID_NONE || !g_signals[sig_id].in_use) continue;
        order[n++] = (uint8_t)i;
    }
    qsort(order, n, sizeof(order[0]), cmp_control_slider_first);

    for (int k = 0; k < n; k++) {
        int i = order[k];
        uint8_t sig_id = g_controls[i].signal_id;
        can_signal_t *sig = &g_signals[sig_id];

        lv_obj_t *card = lv_obj_create(s_container);
        lv_obj_set_width(card, LV_PCT(100));
        lv_obj_set_height(card, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_all(card, 4, 0);
        lv_obj_set_style_pad_row(card, 4, 0);

        lv_obj_t *top_row = lv_obj_create(card);
        lv_obj_set_width(top_row, LV_PCT(100));
        lv_obj_set_height(top_row, LV_SIZE_CONTENT);
        lv_obj_set_style_border_width(top_row, 0, 0);
        lv_obj_set_style_pad_all(top_row, 0, 0);
        lv_obj_set_flex_flow(top_row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(top_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t *lbl = lv_label_create(top_row);
        snprintf(buf, sizeof buf, "%s: %.3g %s", sig->name, sig->value, sig->units);
        lv_label_set_text(lbl, buf);

        lv_obj_t *del_btn = lv_button_create(top_row);
        lv_obj_add_event_cb(del_btn, remove_control_btn_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_label_set_text(lv_label_create(del_btn), LV_SYMBOL_TRASH);

        if (g_controls[i].control_type == CAN_CONTROL_TOGGLE) {
            lv_obj_t *sw = lv_switch_create(card);
            double mid = (sig->min_value + sig->max_value) / 2.0;
            if (sig->value > mid) lv_obj_add_state(sw, LV_STATE_CHECKED);
            lv_obj_set_user_data(sw, lbl);
            lv_obj_add_event_cb(sw, toggle_value_changed_cb, LV_EVENT_VALUE_CHANGED, (void *)(intptr_t)i);
        } else {
            lv_obj_t *sl = lv_slider_create(card);
            lv_obj_set_width(sl, LV_PCT(100));
            lv_slider_set_range(sl, 0, SLIDER_RESOLUTION);
            double range = (sig->max_value > sig->min_value) ? (sig->max_value - sig->min_value) : 1.0;
            int32_t pos = (int32_t)(((sig->value - sig->min_value) / range) * SLIDER_RESOLUTION);
            if (pos < 0) pos = 0;
            if (pos > SLIDER_RESOLUTION) pos = SLIDER_RESOLUTION;
            lv_slider_set_value(sl, pos, LV_ANIM_OFF);
            lv_obj_set_user_data(sl, lbl);
            lv_obj_add_event_cb(sl, slider_value_changed_cb, LV_EVENT_VALUE_CHANGED, (void *)(intptr_t)i);
            lv_obj_add_event_cb(sl, slider_value_changed_cb, LV_EVENT_RELEASED, (void *)(intptr_t)i);
        }
    }
}

// Rebuilds the Add form's signal dropdown. For Add Toggle (`bit1_only`
// true), only signals with bit_length == 1 are offered — a toggle only
// makes sense for a 1-bit on/off signal.
static int cmp_signal_name(const void *a, const void *b) {
    return strcmp(g_signals[*(const uint8_t *)a].name, g_signals[*(const uint8_t *)b].name);
}

static void rebuild_signal_dropdown(bool bit1_only) {
    static char opts[CAN_MAX_SIGNALS * (CAN_SIGNAL_NAME_MAX + 1)];
    int n = 0;
    for (int i = 0; i < CAN_MAX_SIGNALS; i++) {
        if (!g_signals[i].in_use) continue;
        if (bit1_only && g_signals[i].bit_length != 1) continue;
        s_dd_signal_ids[n++] = (uint8_t)i;
    }
    qsort(s_dd_signal_ids, n, sizeof(s_dd_signal_ids[0]), cmp_signal_name);

    opts[0] = '\0';
    for (int k = 0; k < n; k++) {
        if (k > 0) strncat(opts, "\n", sizeof(opts) - strlen(opts) - 1);
        strncat(opts, g_signals[s_dd_signal_ids[k]].name, sizeof(opts) - strlen(opts) - 1);
    }
    if (n == 0) {
        strncpy(opts, bit1_only ? "(create a 1-bit signal first)" : "(create a signal first)",
                sizeof(opts) - 1);
        opts[sizeof(opts) - 1] = '\0';
    }
    lv_dropdown_set_options(s_dd_signal, opts);
}

static void add_control_save(void) {
    uint16_t sel = lv_dropdown_get_selected(s_dd_signal);
    if (sel < CAN_MAX_SIGNALS) {
        uint8_t sig_id = s_dd_signal_ids[sel];
        if (g_signals[sig_id].in_use) {
            int idx = app_state_alloc_control();
            if (idx >= 0) {
                g_controls[idx].signal_id = sig_id;
                g_controls[idx].control_type = s_adding_control_type;
            }
        }
    }
    ui_shell_close(1);
    ui_controls_refresh();
}

static void open_add_control_form(uint8_t control_type) {
    s_adding_control_type = control_type;
    bool bit1_only = (control_type == CAN_CONTROL_TOGGLE);

    rebuild_signal_dropdown(bit1_only);

    ui_shell_open_form(bit1_only ? "Add Toggle" : "Add Slider", s_form_page, "Save", add_control_save, NULL, NULL, NULL);
}

static void add_slider_btn_cb(lv_event_t *e) {
    (void)e;
    open_add_control_form(CAN_CONTROL_SLIDER);
}

static void add_toggle_btn_cb(lv_event_t *e) {
    (void)e;
    open_add_control_form(CAN_CONTROL_TOGGLE);
}

void ui_controls_add_slider(void) { add_slider_btn_cb(NULL); }
void ui_controls_add_toggle(void) { add_toggle_btn_cb(NULL); }

lv_obj_t *ui_controls_create(lv_obj_t *parent) {
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);

    s_container = lv_obj_create(parent);
    lv_obj_set_width(s_container, LV_PCT(100));
    lv_obj_set_flex_grow(s_container, 1);
    lv_obj_set_flex_flow(s_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_container, 4, 0);

    s_form_page = lv_obj_create(ui_shell_overlay());
    lv_obj_set_size(s_form_page, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_border_width(s_form_page, 0, 0);
    lv_obj_set_style_pad_row(s_form_page, 8, 0);
    lv_obj_set_flex_flow(s_form_page, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(s_form_page, LV_OBJ_FLAG_HIDDEN);

    s_dd_signal = lv_dropdown_create(s_form_page);
    lv_obj_set_width(s_dd_signal, LV_PCT(100));

    ui_controls_refresh();
    return parent;
}
