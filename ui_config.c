// apps/wilicankit/ui_config.c — see ui_config.h.
#include "ui_config.h"
#include "ui_common.h"
#include "ui_shell.h"
#include "storage.h"
#include "ui_signals.h"
#include "ui_messages.h"
#include "ui_transmit.h"
#include "ui_controls.h"
#include "can_link.h"
#include "app_state.h"
#include "pico/stdlib.h"   // __uninitialized_psram, via pico/platform/sections.h
#include <stdio.h>
#include <string.h>
#include <strings.h>

// LVGL-side path for storage.h's STORAGE_DIR ("0:/wilicankit") through the
// "F:" driver registered by lv_fs_fatfs_init() (main.c) — see lv_conf.h's
// LV_USE_FS_FATFS/LV_FS_FATFS_LETTER comment.
#define CONFIG_FS_ROOT "F:/wilicankit"

static lv_obj_t *s_explorer;
static lv_obj_t *s_status_lbl;
static lv_obj_t *s_backend_dd;
static lv_obj_t *s_sd_list;

// Config names currently shown in s_sd_list, indexed the same as its rows —
// row click handlers get only an index (lv_event user_data), so this is what
// they look the name back up in.
#define SD_LIST_MAX 32
static char __uninitialized_psram("wilicankit_sd_names") s_sd_names[SD_LIST_MAX][STORAGE_NAME_MAX];

// Name of the last-tapped SD row, or "" if none yet this session -- SD has
// no persistent widget selection like lv_file_explorer, so Delete needs its
// own memory of "whatever was last loaded" (mirrors the USB explorer's own
// selection-based Delete UX).
static char s_sd_selected_name[STORAGE_NAME_MAX] = "";

// Shared state for the "are you sure?" confirm screen (ui_common_show_confirm)
// — reused for Delete, New, and Save-As-over-an-existing-file; only one of
// these is ever in flight at a time.
static char s_confirm_name[STORAGE_NAME_MAX];
static char s_confirm_msg[64];

// The persistent "Save As" screen's own state.
static lv_obj_t *s_save_page;
static lv_obj_t *s_ta_name;

// Name of the config currently loaded/saved, or "" if none (fresh boot or
// after New) -- Save As defaults its textarea to this.
static char s_current_config_name[STORAGE_NAME_MAX] = "";

static void refresh_all_data_tabs(void) {
    ui_signals_refresh();
    ui_messages_refresh();
    ui_transmit_refresh();
    ui_controls_refresh();
}

// Strips a trailing ".json" (case-insensitive) from `fname` into `out`.
static void strip_json_ext(const char *fname, char *out, size_t out_cap) {
    size_t len = strlen(fname);
    if (len > 5 && strcasecmp(fname + len - 5, ".json") == 0) len -= 5;
    if (len >= out_cap) len = out_cap - 1;
    memcpy(out, fname, len);
    out[len] = '\0';
}

// Formats `name` for display as "<name>.json" without doubling the
// extension if it's already present (mirrors storage.c's build_path).
static void with_json_ext(const char *name, char *out, size_t out_cap) {
    size_t len = strlen(name);
    bool has_ext = len > 5 && strcasecmp(name + len - 5, ".json") == 0;
    snprintf(out, out_cap, has_ext ? "%s" : "%s.json", name);
}

// True if the explorer is currently showing our config folder — guards
// Load/Delete against a user who navigated ".." out of it: storage.h's
// load/delete/exists calls are always relative to STORAGE_DIR, so acting on
// a selection from anywhere else on the stick would silently target the
// wrong file.
static bool explorer_at_config_root(void) {
    return strcmp(lv_file_explorer_get_current_path(s_explorer), CONFIG_FS_ROOT "/") == 0;
}

// Repopulates s_sd_list from storage_list_configs() -- called on Config-tab
// nav-in and after any Save/Delete while the SD backend is active.
static void sd_row_click_cb(lv_event_t *e);

static void sd_list_refresh(void) {
    lv_obj_clean(s_sd_list);
    int n = storage_list_configs(s_sd_names, SD_LIST_MAX);
    for (int i = 0; i < n; i++) {
        char label[STORAGE_NAME_MAX + 5];
        with_json_ext(s_sd_names[i], label, sizeof label);
        lv_obj_t *btn = lv_list_add_button(s_sd_list, LV_SYMBOL_FILE, label);
        lv_obj_add_event_cb(btn, sd_row_click_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
}

void ui_config_refresh(void) {
    if (storage_get_backend() == STORAGE_BACKEND_SD) {
        if (!s_sd_list) return;
        if (storage_sd_available()) {
            sd_list_refresh();
        } else {
            lv_obj_clean(s_sd_list);
            lv_label_set_text(s_status_lbl, "SD card not available");
        }
        return;
    }
    if (!s_explorer) return;
    lv_file_explorer_open_dir(s_explorer, CONFIG_FS_ROOT);
}

// Shared by both backends' "tap to load" handlers below.
static void load_and_apply(const char *name) {
    bool ok = storage_load_config(name);
    char disp[STORAGE_NAME_MAX + 5];
    with_json_ext(name, disp, sizeof disp);
    lv_label_set_text_fmt(s_status_lbl, ok ? "Loaded '%s'" : "Load failed: '%s'", disp);
    if (ok) {
        strncpy(s_current_config_name, name, sizeof s_current_config_name - 1);
        s_current_config_name[sizeof s_current_config_name - 1] = '\0';
        // storage_load_config overwrites g_messages directly, bypassing the
        // switch/dropdown callbacks that normally arm can_link's periodic
        // timers — without this, a loaded "enabled" message stays silent
        // until its row is manually toggled off and on.
        can_link_resync_periodic_all();
        refresh_all_data_tabs();
    }
}

static void explorer_file_selected_cb(lv_event_t *e) {
    (void)e;
    if (!explorer_at_config_root()) {
        lv_label_set_text(s_status_lbl, "Only files under wilicankit/ can be loaded");
        return;
    }
    char name[STORAGE_NAME_MAX];
    strip_json_ext(lv_file_explorer_get_selected_file_name(s_explorer), name, sizeof name);
    load_and_apply(name);
}

static void sd_row_click_cb(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    strncpy(s_sd_selected_name, s_sd_names[idx], sizeof s_sd_selected_name - 1);
    s_sd_selected_name[sizeof s_sd_selected_name - 1] = '\0';
    load_and_apply(s_sd_selected_name);
}

static void do_delete_confirmed(void *user_data) {
    (void)user_data;
    bool ok = storage_delete_config(s_confirm_name);
    char disp[STORAGE_NAME_MAX + 5];
    with_json_ext(s_confirm_name, disp, sizeof disp);
    lv_label_set_text_fmt(s_status_lbl, ok ? "Deleted '%s'" : "Delete failed: '%s'", disp);
    ui_shell_close(1);   // confirm -> back to BAR_LOAD
    if (ok) ui_config_refresh();
}

static void delete_btn_cb(lv_event_t *e) {
    (void)e;
    if (storage_get_backend() == STORAGE_BACKEND_SD) {
        if (s_sd_selected_name[0] == '\0') {
            lv_label_set_text(s_status_lbl, "Tap a config first, then Delete");
            return;
        }
        strncpy(s_confirm_name, s_sd_selected_name, sizeof s_confirm_name - 1);
        s_confirm_name[sizeof s_confirm_name - 1] = '\0';
    } else {
        if (!explorer_at_config_root()) {
            lv_label_set_text(s_status_lbl, "Only files under wilicankit/ can be deleted");
            return;
        }
        const char *fname = lv_file_explorer_get_selected_file_name(s_explorer);
        if (!fname || fname[0] == '\0') {
            lv_label_set_text(s_status_lbl, "Tap a file first, then Delete");
            return;
        }
        strip_json_ext(fname, s_confirm_name, sizeof s_confirm_name);
    }
    char disp[STORAGE_NAME_MAX + 5];
    with_json_ext(s_confirm_name, disp, sizeof disp);
    snprintf(s_confirm_msg, sizeof s_confirm_msg, "Delete '%s'? This cannot be undone.", disp);
    ui_common_show_confirm(s_confirm_msg, "Delete", do_delete_confirmed, NULL);
}

void ui_config_delete(void) { delete_btn_cb(NULL); }

static void do_overwrite_confirmed(void *user_data) {
    (void)user_data;
    bool ok = storage_save_config(s_confirm_name);
    char disp[STORAGE_NAME_MAX + 5];
    with_json_ext(s_confirm_name, disp, sizeof disp);
    lv_label_set_text_fmt(s_status_lbl, ok ? "Overwrote '%s'" : "Overwrite failed: '%s'", disp);
    if (ok) {
        strncpy(s_current_config_name, s_confirm_name, sizeof s_current_config_name - 1);
        s_current_config_name[sizeof s_current_config_name - 1] = '\0';
    }
    ui_shell_close(2);   // confirm + Save-As screen -> back to BAR_LOAD
    if (ok) ui_config_refresh();
}

static void save_form_save(void) {
    const char *name = lv_textarea_get_text(s_ta_name);
    if (name[0] == '\0') {
        lv_label_set_text(s_status_lbl, "Enter a name first");
        return;
    }
    if (!storage_name_is_valid(name)) {
        lv_label_set_text(s_status_lbl, "Name has invalid characters");
        return;
    }
    if (storage_config_exists(name)) {
        strncpy(s_confirm_name, name, sizeof s_confirm_name - 1);
        s_confirm_name[sizeof s_confirm_name - 1] = '\0';
        char disp[STORAGE_NAME_MAX + 5];
        with_json_ext(s_confirm_name, disp, sizeof disp);
        snprintf(s_confirm_msg, sizeof s_confirm_msg, "Overwrite '%s' with the current config?", disp);
        // Pushed on top of the still-open Save-As screen: Back from here
        // (cancel) pops just this confirm, landing back on Save-As with the
        // typed name intact.
        ui_common_show_confirm(s_confirm_msg, "Overwrite", do_overwrite_confirmed, NULL);
        return;
    }
    bool ok = storage_save_config(name);
    char disp[STORAGE_NAME_MAX + 5];
    with_json_ext(name, disp, sizeof disp);
    lv_label_set_text_fmt(s_status_lbl, ok ? "Saved '%s'" : "Save failed: '%s'", disp);
    if (ok) {
        strncpy(s_current_config_name, name, sizeof s_current_config_name - 1);
        s_current_config_name[sizeof s_current_config_name - 1] = '\0';
        ui_shell_close(1);
        ui_config_refresh();
    }
}

static void do_new_confirmed(void *user_data) {
    (void)user_data;
    app_state_reset_all();
    s_current_config_name[0] = '\0';
    lv_label_set_text(s_status_lbl, "New config (unsaved)");
    // g_messages is now empty, so this just stops every periodic send —
    // same resync used after a load (see explorer_file_selected_cb).
    can_link_resync_periodic_all();
    refresh_all_data_tabs();
    ui_shell_close(1);   // confirm -> back to BAR_LOAD
}

static void new_btn_cb(lv_event_t *e) {
    (void)e;
    ui_common_show_confirm("Unsaved changes will be lost. Are you sure?", "New", do_new_confirmed, NULL);
}

void ui_config_new(void) { new_btn_cb(NULL); }

static void save_as_btn_cb(lv_event_t *e) {
    (void)e;
    lv_textarea_set_text(s_ta_name, s_current_config_name);
    ui_shell_open_form("Save As", s_save_page, "Save", save_form_save, NULL, NULL, NULL);
}

void ui_config_save_as(void) { save_as_btn_cb(NULL); }

// Storage dropdown changed: switch the active backend, clear the "currently
// loaded" name (it belonged to the old backend's files), swap which list is
// visible, and refresh it.
static void backend_dropdown_cb(lv_event_t *e) {
    uint16_t sel = lv_dropdown_get_selected(lv_event_get_target(e));
    storage_backend_t backend = (sel == 0) ? STORAGE_BACKEND_SD : STORAGE_BACKEND_USB;
    storage_set_backend(backend);
    s_current_config_name[0] = '\0';
    s_sd_selected_name[0] = '\0';
    lv_label_set_text(s_status_lbl, "");
    if (backend == STORAGE_BACKEND_SD) {
        lv_obj_add_flag(s_explorer, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_sd_list, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(s_explorer, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_sd_list, LV_OBJ_FLAG_HIDDEN);
    }
    ui_config_refresh();
}

lv_obj_t *ui_config_create(lv_obj_t *parent) {
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);

    lv_obj_t *backend_row = lv_obj_create(parent);
    lv_obj_set_size(backend_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_border_width(backend_row, 0, 0);
    lv_obj_set_style_pad_all(backend_row, 0, 0);
    lv_obj_set_flex_flow(backend_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(backend_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *backend_lbl = lv_label_create(backend_row);
    lv_label_set_text(backend_lbl, "Storage:");

    s_backend_dd = lv_dropdown_create(backend_row);
    lv_dropdown_set_options(s_backend_dd, "SD card\nUSB stick");
    lv_dropdown_set_selected(s_backend_dd, 0);   // SD is the default backend
    lv_obj_add_event_cb(s_backend_dd, backend_dropdown_cb, LV_EVENT_VALUE_CHANGED, NULL);

    s_status_lbl = lv_label_create(parent);
    lv_label_set_text(s_status_lbl, "");

    s_explorer = lv_file_explorer_create(parent);
    lv_obj_set_width(s_explorer, LV_PCT(100));
    lv_obj_set_flex_grow(s_explorer, 1);
    lv_obj_add_event_cb(s_explorer, explorer_file_selected_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_file_explorer_open_dir(s_explorer, CONFIG_FS_ROOT);
    lv_obj_add_flag(s_explorer, LV_OBJ_FLAG_HIDDEN);   // SD is the default backend

    s_sd_list = lv_list_create(parent);
    lv_obj_set_width(s_sd_list, LV_PCT(100));
    lv_obj_set_flex_grow(s_sd_list, 1);

    // Deliberately no storage_sd_available() probe here: ui_config_create()
    // runs from ui_shell_create(), which is called before can_link_open()
    // (see main.c) -- the FwGUI link the SD probe needs isn't armed yet.
    // ui_config_refresh() (fired on Config-tab nav-in, well after boot) does
    // the actual probe.
    storage_set_backend(STORAGE_BACKEND_SD);

    s_save_page = lv_obj_create(ui_shell_overlay());
    lv_obj_set_size(s_save_page, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_border_width(s_save_page, 0, 0);
    lv_obj_set_style_pad_row(s_save_page, 8, 0);
    lv_obj_set_flex_flow(s_save_page, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(s_save_page, LV_OBJ_FLAG_HIDDEN);

    s_ta_name = lv_textarea_create(s_save_page);
    lv_textarea_set_one_line(s_ta_name, true);
    lv_textarea_set_placeholder_text(s_ta_name, "config name");
    ui_common_bind_keyboard(s_ta_name, LV_KEYBOARD_MODE_TEXT_LOWER);

    return parent;
}

