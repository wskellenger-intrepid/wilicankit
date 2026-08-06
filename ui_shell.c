// apps/wilicankit/ui_shell.c — see ui_shell.h.
#include "ui_shell.h"
#include "ui_common.h"
#include "ui_signals.h"
#include "ui_messages.h"
#include "ui_transmit.h"
#include "ui_controls.h"
#include "ui_config.h"
#include "ui_monitor.h"
#include "ui_icons.h"
#include "can_link.h"
#include "lvgl_port.h"
#include "lvgl.h"
#include <string.h>

#define WILICANKIT_VERSION "0.1.0"

typedef enum {
    AREA_MONITOR = 0,
    AREA_TRANSMIT,
    AREA_DIAGNOSTICS,
    AREA_SETUP,
    AREA_COUNT
} app_area_t;

// Transmit's own sub-tabs, mirroring SETUP_TAB_COUNT's pattern below.
// Controls lives here (not as a top-level area) to keep the bottom bar at
// 5 buttons total, matching the device's physical 5-button layout.
typedef enum {
    TRANSMIT_MESSAGES = 0,
    TRANSMIT_CONTROLS,
    TRANSMIT_TAB_COUNT
} transmit_tab_t;

typedef enum {
    SETUP_SIGNALS = 0,
    SETUP_MESSAGES,
    SETUP_CONFIG,
    SETUP_TAB_COUNT
} setup_tab_t;

// Monitor's own sub-tab, mirroring setup_tab_t's pattern above. Just one
// leaf today (Health); more can be added the same way later.
typedef enum {
    MONITOR_HEALTH = 0,
    MONITOR_TAB_COUNT
} monitor_tab_t;

// The bottom bar is a single set of 5 physical button slots whose
// label/icon/action are reconfigured based on context, rather than a
// top-level bar plus separate in-page tab rows — this matches the device's
// physical 5-button layout, where each on-screen slot maps to one hardware
// button regardless of what it currently does.
typedef enum {
    BAR_TOP = 0,      // Monitor / Transmit / Diagnostics / Setup / Online
    BAR_MONITOR,      // Back / Health / (blank) / (blank) / (blank)
    BAR_TRANSMIT,     // Back / Messages / Controls / Setup / Online
    BAR_SETUP,        // Back / Signals / Messages / Load / (blank)
    BAR_SIGNALS,      // Back / Add Signal / (blank) / (blank) / (blank)
    BAR_MESSAGES,     // Back / Add Message / (blank) / (blank) / (blank)
    BAR_LOAD,         // Back / New / Save As / Delete / (blank)
    BAR_CONTROLS,     // Back / Add Slider / Add Toggle / (blank) / (blank)
    // Diagnostics isn't implemented yet — Back is the only action.
    BAR_PLACEHOLDER,  // Back / (blank) / (blank) / (blank) / (blank)
    // Generic, dynamically-configured leaf screens pushed via
    // ui_shell_open_form/ui_shell_open_confirm (see the overlay nav stack
    // below) — their bar_slot_t tables are mutable and populated per-open
    // rather than static const, since the same mode is reused across many
    // different forms/confirms.
    BAR_FORM,         // Back / <save_label> / (blank) / (blank) / Delete
    BAR_CONFIRM,      // Back / <action_label> / (blank) / (blank) / (blank)
} bar_mode_t;

// One of the 5 bottom-bar slots. A slot with a NULL label is blank/disabled.
// Slot 4's entry may instead be the online indicator, marked by using
// action_online_toggle as its cb (see configure_bottom_bar).
typedef struct {
    const char *label;          // NULL => blank/disabled slot
    const lv_image_dsc_t *icon; // may be NULL (text-only button)
    void (*cb)(void);
} bar_slot_t;

static const char *AREA_LABELS[AREA_COUNT] = {
    "Monitor", "Transmit", "Diagnostics", "Setup"
};

static const char *TRANSMIT_LABELS[TRANSMIT_TAB_COUNT] = {
    "Messages", "Controls"
};

static const char *SETUP_LABELS[SETUP_TAB_COUNT] = {
    "Signals", "Messages", "Load"
};

static const char *MONITOR_LABELS[MONITOR_TAB_COUNT] = {
    "Health"
};

static app_area_t s_active_area = AREA_MONITOR;
// -1 = blank landing (no tab selected yet) — reset on every fresh area
// entry so re-visiting Transmit/Setup never shows a stale leftover tab.
static int s_active_transmit = -1;
static int s_active_setup = -1;
static int s_active_monitor = -1;
static bar_mode_t s_bar_mode = BAR_TOP;
static bool s_last_online;
// True at boot and whenever Back exits a top-level area back to the tile
// bar — shows s_home_page (blank for now, a logo later) instead of
// AREA_MONITOR's own "coming soon" placeholder, which is only for when
// the Monitor tile itself is tapped. See go_home().
static bool s_at_home = true;

static lv_obj_t *s_title;
static lv_obj_t *s_home_page;
static lv_obj_t *s_pages[AREA_COUNT];
static lv_obj_t *s_transmit_pages[TRANSMIT_TAB_COUNT];
static lv_obj_t *s_transmit_blank;
static lv_obj_t *s_setup_pages[SETUP_TAB_COUNT];
static lv_obj_t *s_setup_blank;
static lv_obj_t *s_monitor_pages[MONITOR_TAB_COUNT];
static lv_obj_t *s_monitor_blank;

// 5 persistent bottom-bar button slots, relabeled/re-iconed per bar mode
// instead of being destroyed/recreated (cheaper, and avoids heap churn on
// this RAM-constrained target).
static lv_obj_t *s_bar_btns[5];
static lv_obj_t *s_bar_icons[5];
static lv_obj_t *s_bar_labels[5];
// Aliases for slot 4, which is always the online indicator except it goes
// blank/disabled in BAR_SETUP — kept as separate names since style_online_btn
// predates this and is also called from ui_shell_poll().
static lv_obj_t *s_online_btn;
static lv_obj_t *s_online_lbl;static lv_obj_t *s_online_icon;
// Hidden-by-default layer (a child of `content`, so it covers only the
// content area — the app bar and bottom bar stay put) holding every
// add/edit-form and confirm screen as its own children, each a full-size
// page built once by its owning content module and shown/hidden here
// rather than created/destroyed per open. See ui_shell_overlay().
static lv_obj_t *s_overlay;

// Small nav stack for screens pushed onto s_overlay — independent of (and
// does not replace) parent_bar_mode()'s existing top-level Back behavior.
// Deep enough for the deepest case: Message Form -> Manage Signals ->
// Placement Form -> a delete confirm.
// Each frame carries its own bar_slot_t snapshot (rather than reusing one
// shared mutable table) since Messages nests BAR_FORM inside BAR_FORM
// (Message Form -> Signals -> Placement Form) — without a per-frame copy,
// popping back to an outer form would show the inner form's stale
// labels/callbacks.
#define NAV_STACK_MAX 4
typedef struct { bar_mode_t mode; lv_obj_t *page; bar_slot_t slots[5]; const char *crumb; } nav_frame_t;
static nav_frame_t s_nav_stack[NAV_STACK_MAX];
static int         s_nav_depth;
static bar_mode_t  s_pre_overlay_bar_mode;   // restored when the stack empties

// Mutable slot tables for BAR_FORM/BAR_CONFIRM are stored per-frame in
// s_nav_stack itself (see nav_frame_t) — bar_slots_for_mode reads the
// current top of stack for these two modes.

static void ui_shell_set_transmit_tab(int tab);
static void ui_shell_set_setup_tab(int tab);
static void ui_shell_set_monitor_tab(int tab);
static void ui_shell_set_area(app_area_t area);
static void go_home(void);
static void configure_bottom_bar(void);
static void style_online_btn(void);

static void refresh_area(app_area_t area) {
    switch (area) {
        case AREA_MONITOR:
            if (s_active_monitor == MONITOR_HEALTH) ui_monitor_refresh();
            break;
        case AREA_TRANSMIT:
            if (s_active_transmit == TRANSMIT_MESSAGES) ui_transmit_refresh();
            else if (s_active_transmit == TRANSMIT_CONTROLS) ui_controls_refresh();
            break;
        case AREA_SETUP:
            if (s_active_setup == SETUP_SIGNALS) ui_signals_refresh();
            else if (s_active_setup == SETUP_MESSAGES) ui_messages_refresh();
            else if (s_active_setup == SETUP_CONFIG) ui_config_refresh();
            break;
        default: break;
    }
}

// Rebuilds the app-bar title as a full breadcrumb: base area/tab (blank tab
// omits its segment — see ui_shell_set_transmit_tab/set_setup_tab) plus one
// segment per overlay screen on the nav stack. Called on every area/tab
// change and on every nav_push/nav_pop, so the overlay stack is never stale.
static void refresh_title(void) {
    static char title[96];
    if (s_at_home) {
        lv_snprintf(title, sizeof title, "wilicankit %s", WILICANKIT_VERSION);
    } else if (s_active_area == AREA_SETUP && s_active_setup >= 0) {
        lv_snprintf(title, sizeof title, "Setup / %s", SETUP_LABELS[s_active_setup]);
    } else if (s_active_area == AREA_TRANSMIT && s_active_transmit >= 0) {
        lv_snprintf(title, sizeof title, "Transmit / %s", TRANSMIT_LABELS[s_active_transmit]);
    } else if (s_active_area == AREA_MONITOR && s_active_monitor >= 0) {
        lv_snprintf(title, sizeof title, "Monitor / %s", MONITOR_LABELS[s_active_monitor]);
    } else {
        lv_snprintf(title, sizeof title, "%s", AREA_LABELS[s_active_area]);
    }
    for (int i = 0; i < s_nav_depth; i++) {
        size_t len = strlen(title);
        lv_snprintf(title + len, sizeof title - len, " / %s", s_nav_stack[i].crumb);
    }
    lv_label_set_text(s_title, title);
}

// Icons are hardcoded white (ui_icons.c) since LVGL's SVG parser doesn't
// resolve currentColor, so both button states here stay dark enough for a
// white icon to read clearly on either.

// Visible whenever the bottom bar has an online indicator (BAR_TOP,
// BAR_TRANSMIT) so bus status is never hidden behind whatever sub-screen the
// user happens to be on. Blanked out in BAR_SETUP instead (see
// configure_bottom_bar) since that context has no slot for it.
static void style_online_btn(void) {
    bool online = can_link_is_online();
    s_last_online = online;
    lv_label_set_text(s_online_lbl, online ? "Online" : "Offline");
    lv_image_set_src(s_online_icon, online ? &ui_icon_online : &ui_icon_offline);
    lv_obj_set_style_bg_color(s_online_btn,
        online ? lv_palette_main(LV_PALETTE_GREEN) : lv_palette_main(LV_PALETTE_RED), 0);
    lv_obj_set_style_text_color(s_online_btn, lv_color_white(), 0);
}

// Entering Monitor/Diagnostics always shows Back-only (BAR_PLACEHOLDER)
// since neither is implemented yet; entering Transmit/Setup always resets
// to a blank sub-tab landing (see ui_shell_set_transmit_tab/set_setup_tab)
// rather than remembering whatever tab was last viewed. The app's boot
// screen bypasses this entirely — ui_shell_create() calls go_home()
// directly, never goto_area(), so it keeps the initial BAR_TOP tile bar.
static void goto_area(app_area_t area) {
    s_bar_mode = (area == AREA_TRANSMIT) ? BAR_TRANSMIT
               : (area == AREA_SETUP) ? BAR_SETUP
               : (area == AREA_MONITOR) ? BAR_MONITOR
               : BAR_PLACEHOLDER;
    if (area == AREA_TRANSMIT) ui_shell_set_transmit_tab(-1);
    if (area == AREA_SETUP) ui_shell_set_setup_tab(-1);
    if (area == AREA_MONITOR) ui_shell_set_monitor_tab(-1);
    ui_shell_set_area(area);
}

static void action_area_monitor(void) { goto_area(AREA_MONITOR); }
static void action_area_diagnostics(void) { goto_area(AREA_DIAGNOSTICS); }
static void action_enter_transmit(void) { goto_area(AREA_TRANSMIT); }
static void action_enter_setup(void) { goto_area(AREA_SETUP); }

// Back pops one level in the bar hierarchy (leaf context -> its parent
// context -> top level), leaving the active area/tab (and thus on-screen
// content) untouched. If a form/confirm screen is on the overlay stack,
// that takes priority (pops one overlay level instead).
static bar_mode_t parent_bar_mode(bar_mode_t mode) {
    switch (mode) {
        case BAR_SIGNALS:
        case BAR_MESSAGES:
        case BAR_LOAD:
            return BAR_SETUP;
        case BAR_CONTROLS:
            return BAR_TRANSMIT;
        default:
            return BAR_TOP;
    }
}
static void nav_pop(int levels);
static void action_back(void) {
    if (s_nav_depth > 0) { nav_pop(1); return; }
    bar_mode_t parent = parent_bar_mode(s_bar_mode);
    s_bar_mode = parent;
    // Leaving Transmit/Setup/Diagnostics/Monitor entirely (as opposed to a
    // leaf context popping to its own tab-selector) means the *area*, not
    // just the bar, changes — switch content back to Home or it stays
    // stuck showing whatever sub-page was on screen (e.g. Messages).
    if (parent == BAR_TOP) { go_home(); return; }
    // Popping a leaf action bar (Signals/Messages/Load/Controls) back to
    // its hub otherwise leaves that leaf's content page on screen — blank
    // the hub's landing tab, same as a fresh goto_area() entry.
    if (parent == BAR_TRANSMIT) ui_shell_set_transmit_tab(-1);
    else if (parent == BAR_SETUP) ui_shell_set_setup_tab(-1);
    else configure_bottom_bar();
}

static void action_transmit_messages(void) { ui_shell_set_transmit_tab(TRANSMIT_MESSAGES); }
static void action_transmit_controls(void) { s_bar_mode = BAR_CONTROLS; ui_shell_set_transmit_tab(TRANSMIT_CONTROLS); }
static void action_setup_signals(void) { s_bar_mode = BAR_SIGNALS; ui_shell_set_setup_tab(SETUP_SIGNALS); }
static void action_setup_messages(void) { s_bar_mode = BAR_MESSAGES; ui_shell_set_setup_tab(SETUP_MESSAGES); }
static void action_setup_load(void) { s_bar_mode = BAR_LOAD; ui_shell_set_setup_tab(SETUP_CONFIG); }
static void action_monitor_health(void) { ui_shell_set_monitor_tab(MONITOR_HEALTH); }
static void action_online_toggle(void) { can_link_set_online(!can_link_is_online()); style_online_btn(); }

static void action_signals_add(void) { ui_signals_add(); }
static void action_messages_add(void) { ui_messages_add(); }
static void action_load_new(void) { ui_config_new(); }
static void action_load_save_as(void) { ui_config_save_as(); }
static void action_load_delete(void) { ui_config_delete(); }
static void action_controls_add_slider(void) { ui_controls_add_slider(); }
static void action_controls_add_toggle(void) { ui_controls_add_toggle(); }

static const bar_slot_t BAR_TOP_SLOTS[5] = {
    { "Monitor",     &ui_icon_monitor,     action_area_monitor },
    { "Transmit",    &ui_icon_transmit,    action_enter_transmit },
    { "Diagnostics", &ui_icon_diagnostics, action_area_diagnostics },
    { "Setup",       &ui_icon_setup,       action_enter_setup },
    { NULL,          &ui_icon_online,      action_online_toggle },
};
static const bar_slot_t BAR_MONITOR_SLOTS[5] = {
    { "Back",   &ui_icon_back,   action_back },
    { "Health", &ui_icon_health, action_monitor_health },
    { NULL,     NULL,            NULL },
    { NULL,     NULL,            NULL },
    { NULL,     NULL,            NULL },
};
static const bar_slot_t BAR_TRANSMIT_SLOTS[5] = {
    { "Back",     &ui_icon_back,     action_back },
    { "Messages", &ui_icon_messages, action_transmit_messages },
    { "Controls", &ui_icon_controls, action_transmit_controls },
    { "Setup",    &ui_icon_setup,    action_enter_setup },
    { NULL,       &ui_icon_online,   action_online_toggle },
};
static const bar_slot_t BAR_SETUP_SLOTS[5] = {
    { "Back",     &ui_icon_back,     action_back },
    { "Signals",  &ui_icon_signals,  action_setup_signals },
    { "Messages", &ui_icon_messages, action_setup_messages },
    { "Load",     &ui_icon_load,     action_setup_load },
    { NULL,       NULL,              NULL },
};
static const bar_slot_t BAR_SIGNALS_SLOTS[5] = {
    { "Back",   &ui_icon_back, action_back },
    { "Signal", &ui_icon_plus, action_signals_add },
    { NULL,         NULL,          NULL },
    { NULL,         NULL,          NULL },
    { NULL,         NULL,          NULL },
};
static const bar_slot_t BAR_MESSAGES_SLOTS[5] = {
    { "Back",    &ui_icon_back, action_back },
    { "Message", &ui_icon_plus, action_messages_add },
    { NULL,           NULL,         NULL },
    { NULL,           NULL,         NULL },
    { NULL,           NULL,         NULL },
};
static const bar_slot_t BAR_LOAD_SLOTS[5] = {
    { "Back",    &ui_icon_back,   action_back },
    { "New",     &ui_icon_new,    action_load_new },
    { "Save As", &ui_icon_save,   action_load_save_as },
    { "Delete",  &ui_icon_delete, action_load_delete },
    { NULL,      NULL,            NULL },
};
static const bar_slot_t BAR_CONTROLS_SLOTS[5] = {
    { "Back",   &ui_icon_back, action_back },
    { "Slider", &ui_icon_plus, action_controls_add_slider },
    { "Toggle", &ui_icon_plus, action_controls_add_toggle },
    { NULL,          NULL,          NULL },
    { NULL,          NULL,          NULL },
};

// Monitor/Diagnostics aren't implemented yet — Back is the only action.
static const bar_slot_t BAR_PLACEHOLDER_SLOTS[5] = {
    { "Back", &ui_icon_back, action_back },
    { NULL,   NULL,          NULL },
    { NULL,   NULL,          NULL },
    { NULL,   NULL,          NULL },
    { NULL,   NULL,          NULL },
};

static const bar_slot_t *bar_slots_for_mode(bar_mode_t mode) {
    switch (mode) {
        case BAR_MONITOR:     return BAR_MONITOR_SLOTS;
        case BAR_TRANSMIT:    return BAR_TRANSMIT_SLOTS;
        case BAR_SETUP:       return BAR_SETUP_SLOTS;
        case BAR_SIGNALS:     return BAR_SIGNALS_SLOTS;
        case BAR_MESSAGES:    return BAR_MESSAGES_SLOTS;
        case BAR_LOAD:        return BAR_LOAD_SLOTS;
        case BAR_CONTROLS:    return BAR_CONTROLS_SLOTS;
        case BAR_PLACEHOLDER: return BAR_PLACEHOLDER_SLOTS;
        case BAR_FORM:
        case BAR_CONFIRM:
            // Current frame's own snapshot, not a shared global (see nav_frame_t).
            return s_nav_stack[s_nav_depth - 1].slots;
        default:           return BAR_TOP_SLOTS;
    }
}

// Which of slots 0-3 is "active" (highlighted) for the current selection.
// Leaf action contexts (Signals/Messages/Load/Controls) have no persistent
// selection to highlight — every slot there is a momentary action button.
// -1 (no highlight) also covers Transmit/Setup's blank landing state.
static int active_bar_slot(void) {
    switch (s_bar_mode) {
        case BAR_MONITOR:
            return (s_active_monitor == MONITOR_HEALTH) ? 1 : -1;
        case BAR_TRANSMIT:
            if (s_active_transmit == TRANSMIT_MESSAGES) return 1;
            if (s_active_transmit == TRANSMIT_CONTROLS) return 2;
            return -1;
        case BAR_SETUP:
            if (s_active_setup == SETUP_SIGNALS) return 1;
            if (s_active_setup == SETUP_MESSAGES) return 2;
            if (s_active_setup == SETUP_CONFIG) return 3;
            return -1;
        case BAR_SIGNALS:
        case BAR_MESSAGES:
        case BAR_LOAD:
        case BAR_CONTROLS:
        case BAR_PLACEHOLDER:
        case BAR_FORM:
        case BAR_CONFIRM:
            return -1;
        default: return s_at_home ? -1 : (int)s_active_area;
    }
}

// Reconfigures the 5 persistent bottom-bar slots for the current bar mode —
// called whenever s_bar_mode or the active area/tab changes.
static void configure_bottom_bar(void) {
    const bar_slot_t *slots = bar_slots_for_mode(s_bar_mode);
    int active = active_bar_slot();

    for (int i = 0; i < 5; i++) {
        bool is_online_slot = (slots[i].cb == action_online_toggle);
        if (slots[i].icon) {
            lv_image_set_src(s_bar_icons[i], slots[i].icon);
            lv_obj_remove_flag(s_bar_icons[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_bar_icons[i], LV_OBJ_FLAG_HIDDEN);
        }
        if (is_online_slot) {
            lv_obj_remove_state(s_bar_btns[i], LV_STATE_DISABLED);
            style_online_btn();
        } else if (slots[i].label) {
            lv_label_set_text(s_bar_labels[i], slots[i].label);
            lv_obj_remove_state(s_bar_btns[i], LV_STATE_DISABLED);
            // The confirm screen's action slot is always destructive-styled
            // (red), matching this app's existing confirm-button convention.
            lv_color_t bg = (s_bar_mode == BAR_CONFIRM && i == 1) ? lv_palette_main(LV_PALETTE_RED)
                : (i == active) ? lv_palette_main(LV_PALETTE_BLUE) : lv_palette_darken(LV_PALETTE_GREY, 2);
            lv_obj_set_style_bg_color(s_bar_btns[i], bg, 0);
        } else {
            lv_label_set_text(s_bar_labels[i], "");
            lv_obj_set_style_bg_color(s_bar_btns[i], lv_palette_darken(LV_PALETTE_GREY, 2), 0);
            lv_obj_add_state(s_bar_btns[i], LV_STATE_DISABLED);
        }
    }
}

// Shows exactly `page` among s_overlay's children (hiding every sibling),
// discovering the sibling set generically rather than requiring ui_shell.c
// to track which content module owns which page.
static void show_overlay_page(lv_obj_t *page) {
    uint32_t n = lv_obj_get_child_count(s_overlay);
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t *child = lv_obj_get_child(s_overlay, i);
        if (child == page) lv_obj_remove_flag(child, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(child, LV_OBJ_FLAG_HIDDEN);
    }
}

static void nav_push(bar_mode_t mode, lv_obj_t *page, const bar_slot_t *slots, const char *crumb) {
    if (s_nav_depth == 0) {
        s_pre_overlay_bar_mode = s_bar_mode;
        lv_obj_remove_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_nav_depth < NAV_STACK_MAX) {
        nav_frame_t *f = &s_nav_stack[s_nav_depth++];
        f->mode = mode;
        f->page = page;
        for (int i = 0; i < 5; i++) f->slots[i] = slots[i];
        f->crumb = crumb;
    }
    show_overlay_page(page);
    s_bar_mode = mode;
    configure_bottom_bar();
    refresh_title();
}

static void nav_pop(int levels) {
    for (int i = 0; i < levels && s_nav_depth > 0; i++) s_nav_depth--;
    if (s_nav_depth == 0) {
        lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
        s_bar_mode = s_pre_overlay_bar_mode;
    } else {
        nav_frame_t *top = &s_nav_stack[s_nav_depth - 1];
        show_overlay_page(top->page);
        s_bar_mode = top->mode;
    }
    configure_bottom_bar();
    refresh_title();
}

lv_obj_t *ui_shell_overlay(void) { return s_overlay; }

// Small fixed vocabulary of labels used across every ui_shell_open_form/
// open_confirm call site — mapping by label text here avoids extending
// every call site with an explicit icon parameter for what is otherwise a
// dynamically-built bar_slot_t.
static const lv_image_dsc_t *icon_for_label(const char *label) {
    if (!label) return NULL;
    if (!strcmp(label, "Save") || !strcmp(label, "Overwrite")) return &ui_icon_save;
    if (!strcmp(label, "Add") || !strcmp(label, "Signal")) return &ui_icon_plus;
    if (!strcmp(label, "Signals")) return &ui_icon_signals;
    if (!strcmp(label, "Delete") || !strcmp(label, "Remove")) return &ui_icon_delete;
    if (!strcmp(label, "New")) return &ui_icon_new;
    return NULL;
}

void ui_shell_open_form(const char *crumb, lv_obj_t *page, const char *save_label, void (*save_cb)(void),
                        const char *extra_label, void (*extra_cb)(void),
                        void (*delete_cb)(void)) {
    bar_slot_t slots[5];
    slots[0] = (bar_slot_t){ "Back", &ui_icon_back, action_back };
    slots[1] = (bar_slot_t){ save_label, icon_for_label(save_label), save_cb };
    slots[2] = extra_label ? (bar_slot_t){ extra_label, icon_for_label(extra_label), extra_cb }
                           : (bar_slot_t){ NULL, NULL, NULL };
    slots[3] = (bar_slot_t){ NULL, NULL, NULL };
    slots[4] = delete_cb ? (bar_slot_t){ "Delete", &ui_icon_delete, delete_cb }
                         : (bar_slot_t){ NULL, NULL, NULL };
    nav_push(BAR_FORM, page, slots, crumb);
}

void ui_shell_open_confirm(lv_obj_t *page, const char *action_label, void (*action_cb)(void)) {
    bar_slot_t slots[5];
    slots[0] = (bar_slot_t){ "Back", &ui_icon_back, action_back };
    slots[1] = (bar_slot_t){ action_label, icon_for_label(action_label), action_cb };
    slots[2] = (bar_slot_t){ NULL, NULL, NULL };
    slots[3] = (bar_slot_t){ NULL, NULL, NULL };
    slots[4] = (bar_slot_t){ NULL, NULL, NULL };
    nav_push(BAR_CONFIRM, page, slots, action_label);
}

void ui_shell_close(int levels) { nav_pop(levels); }

// tab == -1 shows a blank landing page (no sub-tab selected) instead of any
// real sub-page — goto_area() resets to -1 on every fresh entry to
// Transmit/Setup rather than remembering the last-viewed tab.
static void ui_shell_set_transmit_tab(int tab) {
    s_active_transmit = tab;
    for (int i = 0; i < TRANSMIT_TAB_COUNT; i++) {
        if (i == s_active_transmit) lv_obj_remove_flag(s_transmit_pages[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(s_transmit_pages[i], LV_OBJ_FLAG_HIDDEN);
    }
    if (s_transmit_blank) {
        if (tab < 0) lv_obj_remove_flag(s_transmit_blank, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(s_transmit_blank, LV_OBJ_FLAG_HIDDEN);
    }
    refresh_title();
    refresh_area(AREA_TRANSMIT);
    configure_bottom_bar();
}

static void ui_shell_set_setup_tab(int tab) {
    s_active_setup = tab;
    for (int i = 0; i < SETUP_TAB_COUNT; i++) {
        if (i == s_active_setup) lv_obj_remove_flag(s_setup_pages[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(s_setup_pages[i], LV_OBJ_FLAG_HIDDEN);
    }
    if (s_setup_blank) {
        if (tab < 0) lv_obj_remove_flag(s_setup_blank, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(s_setup_blank, LV_OBJ_FLAG_HIDDEN);
    }
    refresh_title();
    refresh_area(AREA_SETUP);
    configure_bottom_bar();
}

static void ui_shell_set_monitor_tab(int tab) {
    s_active_monitor = tab;
    for (int i = 0; i < MONITOR_TAB_COUNT; i++) {
        if (i == s_active_monitor) lv_obj_remove_flag(s_monitor_pages[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(s_monitor_pages[i], LV_OBJ_FLAG_HIDDEN);
    }
    if (s_monitor_blank) {
        if (tab < 0) lv_obj_remove_flag(s_monitor_blank, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(s_monitor_blank, LV_OBJ_FLAG_HIDDEN);
    }
    refresh_title();
    refresh_area(AREA_MONITOR);
    configure_bottom_bar();
}

static void ui_shell_set_area(app_area_t area) {
    s_active_area = area;
    s_at_home = false;
    lv_obj_add_flag(s_home_page, LV_OBJ_FLAG_HIDDEN);
    for (int i = 0; i < AREA_COUNT; i++) {
        if (i == s_active_area) lv_obj_remove_flag(s_pages[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(s_pages[i], LV_OBJ_FLAG_HIDDEN);
    }
    refresh_title();
    refresh_area(area);
    configure_bottom_bar();
}

// The blank top-level landing (see s_at_home) — distinct from any of the 4
// area pages, so it never inherits Monitor's own placeholder content.
static void go_home(void) {
    s_at_home = true;
    for (int i = 0; i < AREA_COUNT; i++) lv_obj_add_flag(s_pages[i], LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_home_page, LV_OBJ_FLAG_HIDDEN);
    s_bar_mode = BAR_TOP;
    refresh_title();
    configure_bottom_bar();
}

// Single callback for all 5 bottom-bar slots; looks up the current bar
// mode's action at click time rather than rebinding event callbacks per
// mode switch.
static void bar_slot_btn_cb(lv_event_t *e) {
    int slot = (int)(intptr_t)lv_event_get_user_data(e);
    void (*cb)(void) = bar_slots_for_mode(s_bar_mode)[slot].cb;
    if (cb) cb();
}

static void area_cycle_next(void) {
    int next = ((int)s_active_area + 1) % AREA_COUNT;
    goto_area((app_area_t)next);
}

static void shell_button_cb(uartkbd_btn_t btn, bool pressed) {
    if (!pressed) return;

    switch (btn) {
        case UARTKBD_BTN_HOME:
            nav_pop(NAV_STACK_MAX);   // close any open form/confirm overlay first
            go_home();
            break;
        case UARTKBD_BTN_PAGE:
            area_cycle_next();
            break;
        case UARTKBD_BTN_OK: {
            lv_group_t *group = lv_group_get_default();
            if (group) lv_group_send_data(group, LV_KEY_ENTER);
            break;
        }
        case UARTKBD_BTN_CANCEL: {
            lv_group_t *group = lv_group_get_default();
            if (group) lv_group_send_data(group, LV_KEY_ESC);
            break;
        }
        case UARTKBD_BTN_GREY:
        case UARTKBD_BTN_YELLOW:
        case UARTKBD_BTN_GREEN:
        case UARTKBD_BTN_BLUE:
        case UARTKBD_BTN_RED: {
            // GREY..RED enum order matches bar_slot_t[0..4] 1:1 — mirror
            // whatever the on-screen slot does, same as bar_slot_btn_cb.
            void (*cb)(void) = bar_slots_for_mode(s_bar_mode)[(int)btn].cb;
            if (cb) cb();
            break;
        }
        default:
            break;
    }
}

void ui_shell_create(void) {
    lv_obj_t *screen = lv_screen_active();
    ui_common_init(screen);

    lv_obj_t *root = lv_obj_create(screen);
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_radius(root, 0, 0);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);

    lv_obj_t *app_bar = lv_obj_create(root);
    lv_obj_set_width(app_bar, LV_PCT(100));
    lv_obj_set_height(app_bar, 36);
    lv_obj_set_style_pad_hor(app_bar, 8, 0);
    lv_obj_set_style_pad_ver(app_bar, 6, 0);
    lv_obj_set_style_border_width(app_bar, 0, 0);
    lv_obj_set_style_radius(app_bar, 0, 0);
    lv_obj_set_style_bg_color(app_bar, lv_palette_darken(LV_PALETTE_BLUE, 3), 0);
    lv_obj_set_flex_flow(app_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(app_bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    s_title = lv_label_create(app_bar);
    lv_obj_set_style_text_color(s_title, lv_color_white(), 0);
    lv_obj_set_flex_grow(s_title, 1);
    // Static "…" truncation (no scroll animation), matching this app's
    // existing no-animation convention, for deeply-nested breadcrumbs.
    lv_label_set_long_mode(s_title, LV_LABEL_LONG_DOT);

    lv_obj_t *content = lv_obj_create(root);
    lv_obj_set_width(content, LV_PCT(100));
    lv_obj_set_flex_grow(content, 1);
    lv_obj_set_style_pad_all(content, 0, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_radius(content, 0, 0);

    // Created before any *_create() below so content modules can parent
    // their persistent form/confirm pages under ui_shell_overlay() while
    // building their own area. Raised above every s_pages[] sibling once
    // all of them exist too, so it draws on top regardless of creation
    // order (see the lv_obj_move_foreground() call after the setup pages).
    s_overlay = lv_obj_create(content);
    lv_obj_set_size(s_overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_all(s_overlay, 6, 0);
    lv_obj_set_style_border_width(s_overlay, 0, 0);
    lv_obj_set_style_radius(s_overlay, 0, 0);
    lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);

    // Blank for now (a logo later) — the top-level landing shown at boot
    // and whenever Back exits an area back to the tile bar. Distinct from
    // s_pages[AREA_MONITOR], which is only Monitor's own placeholder.
    s_home_page = lv_obj_create(content);
    lv_obj_set_size(s_home_page, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_all(s_home_page, 0, 0);
    lv_obj_set_style_border_width(s_home_page, 0, 0);
    lv_obj_set_style_radius(s_home_page, 0, 0);
    lv_obj_add_flag(s_home_page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_home_page, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < AREA_COUNT; i++) {
        s_pages[i] = lv_obj_create(content);
        lv_obj_set_size(s_pages[i], LV_PCT(100), LV_PCT(100));
        lv_obj_set_style_pad_all(s_pages[i], 6, 0);
        lv_obj_set_style_border_width(s_pages[i], 0, 0);
        lv_obj_set_style_radius(s_pages[i], 0, 0);
    }

    // Monitor's Health sub-tab is selected via the bottom bar (BAR_MONITOR),
    // same as Transmit's/Setup's leaves.
    lv_obj_t *monitor_page = s_pages[AREA_MONITOR];
    lv_obj_set_style_pad_all(monitor_page, 0, 0);

    for (int i = 0; i < MONITOR_TAB_COUNT; i++) {
        s_monitor_pages[i] = lv_obj_create(monitor_page);
        lv_obj_set_size(s_monitor_pages[i], LV_PCT(100), LV_PCT(100));
        lv_obj_set_style_pad_all(s_monitor_pages[i], 6, 0);
        lv_obj_set_style_border_width(s_monitor_pages[i], 0, 0);
    }

    ui_monitor_create(s_monitor_pages[MONITOR_HEALTH]);

    // Blank landing shown when Monitor is entered fresh (no sub-tab selected
    // yet) — see ui_shell_set_monitor_tab(-1).
    s_monitor_blank = lv_obj_create(monitor_page);
    lv_obj_set_size(s_monitor_blank, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_all(s_monitor_blank, 6, 0);
    lv_obj_set_style_border_width(s_monitor_blank, 0, 0);

    // Transmit's Messages/Controls sub-tabs are selected via the bottom bar
    // (BAR_TRANSMIT) rather than an in-page tab row, so this is just a plain
    // full-size container for the two sub-pages.
    lv_obj_t *transmit_page = s_pages[AREA_TRANSMIT];
    lv_obj_set_style_pad_all(transmit_page, 0, 0);

    for (int i = 0; i < TRANSMIT_TAB_COUNT; i++) {
        s_transmit_pages[i] = lv_obj_create(transmit_page);
        lv_obj_set_size(s_transmit_pages[i], LV_PCT(100), LV_PCT(100));
        lv_obj_set_style_pad_all(s_transmit_pages[i], 6, 0);
        lv_obj_set_style_border_width(s_transmit_pages[i], 0, 0);
    }

    ui_transmit_create(s_transmit_pages[TRANSMIT_MESSAGES]);
    ui_controls_create(s_transmit_pages[TRANSMIT_CONTROLS]);

    // Blank landing shown when Transmit is entered fresh (no sub-tab
    // selected yet) — see ui_shell_set_transmit_tab(-1).
    s_transmit_blank = lv_obj_create(transmit_page);
    lv_obj_set_size(s_transmit_blank, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_all(s_transmit_blank, 6, 0);
    lv_obj_set_style_border_width(s_transmit_blank, 0, 0);

    lv_obj_set_flex_flow(s_pages[AREA_DIAGNOSTICS], LV_FLEX_FLOW_COLUMN);
    lv_obj_t *diag_lbl = lv_label_create(s_pages[AREA_DIAGNOSTICS]);
    lv_label_set_text(diag_lbl, "Diagnostics view coming soon");

    // Setup's Signals/Messages/Load sub-tabs are likewise selected via the
    // bottom bar (BAR_SETUP).
    lv_obj_t *setup_page = s_pages[AREA_SETUP];
    lv_obj_set_style_pad_all(setup_page, 0, 0);

    for (int i = 0; i < SETUP_TAB_COUNT; i++) {
        s_setup_pages[i] = lv_obj_create(setup_page);
        lv_obj_set_size(s_setup_pages[i], LV_PCT(100), LV_PCT(100));
        lv_obj_set_style_pad_all(s_setup_pages[i], 6, 0);
        lv_obj_set_style_border_width(s_setup_pages[i], 0, 0);
    }

    ui_signals_create(s_setup_pages[SETUP_SIGNALS]);
    ui_messages_create(s_setup_pages[SETUP_MESSAGES]);
    ui_config_create(s_setup_pages[SETUP_CONFIG]);

    // Blank landing shown when Setup is entered fresh (no sub-tab selected
    // yet) — see ui_shell_set_setup_tab(-1).
    s_setup_blank = lv_obj_create(setup_page);
    lv_obj_set_size(s_setup_blank, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_all(s_setup_blank, 6, 0);
    lv_obj_set_style_border_width(s_setup_blank, 0, 0);

    // Every s_pages[]/sub-page now exists; move the overlay above all of
    // them so it draws on top no matter which one is visible underneath.
    lv_obj_move_foreground(s_overlay);

    lv_obj_t *bottom = lv_obj_create(root);
    lv_obj_set_width(bottom, LV_PCT(100));
    lv_obj_set_height(bottom, 56);
    lv_obj_set_style_pad_all(bottom, 4, 0);
    lv_obj_set_style_border_width(bottom, 0, 0);
    lv_obj_set_style_radius(bottom, 0, 0);
    lv_obj_set_style_pad_column(bottom, 4, 0);
    lv_obj_set_flex_flow(bottom, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bottom, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    for (int i = 0; i < 5; i++) {
        s_bar_btns[i] = lv_button_create(bottom);
        lv_obj_set_flex_grow(s_bar_btns[i], 1);
        lv_obj_set_height(s_bar_btns[i], LV_PCT(100));
        lv_obj_set_style_pad_all(s_bar_btns[i], 2, 0);
        lv_obj_set_flex_flow(s_bar_btns[i], LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(s_bar_btns[i], LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_add_event_cb(s_bar_btns[i], bar_slot_btn_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_set_style_text_color(s_bar_btns[i], lv_color_white(), 0);

        s_bar_icons[i] = lv_image_create(s_bar_btns[i]);
        lv_obj_set_style_image_recolor_opa(s_bar_icons[i], LV_OPA_COVER, 0);
        lv_obj_set_style_image_recolor(s_bar_icons[i], lv_color_white(), 0);
        s_bar_labels[i] = lv_label_create(s_bar_btns[i]);
    }
    // Slot 4 is always the online indicator except in BAR_SETUP, where
    // configure_bottom_bar() blanks/disables it.
    s_online_btn = s_bar_btns[4];
    s_online_lbl = s_bar_labels[4];
    s_online_icon = s_bar_icons[4];

    // Boot lands on the blank Home page with the blank Transmit/Setup
    // landings behind it and the full BAR_TOP tile bar — see go_home().
    ui_shell_set_transmit_tab(-1);
    ui_shell_set_setup_tab(-1);
    go_home();
    style_online_btn();
    lvgl_port_set_button_handler(shell_button_cb);
    lv_group_focus_obj(s_bar_btns[0]);
}

void ui_shell_poll(void) {
    if (can_link_is_online() != s_last_online) style_online_btn();
}
