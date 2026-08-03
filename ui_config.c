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
#include <stdio.h>
#include <string.h>
#include <strings.h>

// LVGL-side path for storage.h's STORAGE_DIR ("0:/wilicankit") through the
// "F:" driver registered by lv_fs_fatfs_init() (main.c) — see lv_conf.h's
// LV_USE_FS_FATFS/LV_FS_FATFS_LETTER comment.
#define CONFIG_FS_ROOT "F:/wilicankit"

static lv_obj_t *s_explorer;
static lv_obj_t *s_status_lbl;

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

// True if the explorer is currently showing our config folder — guards
// Load/Delete against a user who navigated ".." out of it: storage.h's
// load/delete/exists calls are always relative to STORAGE_DIR, so acting on
// a selection from anywhere else on the stick would silently target the
// wrong file.
static bool explorer_at_config_root(void) {
    return strcmp(lv_file_explorer_get_current_path(s_explorer), CONFIG_FS_ROOT "/") == 0;
}

void ui_config_refresh(void) {
    if (!s_explorer) return;
    lv_file_explorer_open_dir(s_explorer, CONFIG_FS_ROOT);
}

static void explorer_file_selected_cb(lv_event_t *e) {
    (void)e;
    if (!explorer_at_config_root()) {
        lv_label_set_text(s_status_lbl, "Only files under wilicankit/ can be loaded");
        return;
    }
    char name[STORAGE_NAME_MAX];
    strip_json_ext(lv_file_explorer_get_selected_file_name(s_explorer), name, sizeof name);
    bool ok = storage_load_config(name);
    lv_label_set_text_fmt(s_status_lbl, ok ? "Loaded '%s'" : "Load failed: '%s'", name);
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

static void do_delete_confirmed(void *user_data) {
    (void)user_data;
    bool ok = storage_delete_config(s_confirm_name);
    lv_label_set_text_fmt(s_status_lbl, ok ? "Deleted '%s'" : "Delete failed: '%s'", s_confirm_name);
    ui_shell_close(1);   // confirm -> back to BAR_LOAD
    if (ok) ui_config_refresh();
}

static void delete_btn_cb(lv_event_t *e) {
    (void)e;
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
    snprintf(s_confirm_msg, sizeof s_confirm_msg, "Delete '%s'? This cannot be undone.", s_confirm_name);
    ui_common_show_confirm(s_confirm_msg, "Delete", do_delete_confirmed, NULL);
}

void ui_config_delete(void) { delete_btn_cb(NULL); }

static void do_overwrite_confirmed(void *user_data) {
    (void)user_data;
    bool ok = storage_save_config(s_confirm_name);
    lv_label_set_text_fmt(s_status_lbl, ok ? "Overwrote '%s'" : "Overwrite failed: '%s'", s_confirm_name);
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
    if (storage_config_exists(name)) {
        strncpy(s_confirm_name, name, sizeof s_confirm_name - 1);
        s_confirm_name[sizeof s_confirm_name - 1] = '\0';
        snprintf(s_confirm_msg, sizeof s_confirm_msg, "Overwrite '%s' with the current config?", s_confirm_name);
        // Pushed on top of the still-open Save-As screen: Back from here
        // (cancel) pops just this confirm, landing back on Save-As with the
        // typed name intact.
        ui_common_show_confirm(s_confirm_msg, "Overwrite", do_overwrite_confirmed, NULL);
        return;
    }
    bool ok = storage_save_config(name);
    lv_label_set_text_fmt(s_status_lbl, ok ? "Saved '%s'" : "Save failed: '%s'", name);
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

lv_obj_t *ui_config_create(lv_obj_t *parent) {
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);

    s_status_lbl = lv_label_create(parent);
    lv_label_set_text(s_status_lbl, "");

    s_explorer = lv_file_explorer_create(parent);
    lv_obj_set_width(s_explorer, LV_PCT(100));
    lv_obj_set_flex_grow(s_explorer, 1);
    lv_obj_add_event_cb(s_explorer, explorer_file_selected_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_file_explorer_open_dir(s_explorer, CONFIG_FS_ROOT);

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

