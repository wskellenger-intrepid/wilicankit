// apps/wilicankit/ui_common.h — shared UI helpers: a single app-wide
// on-screen keyboard singleton (shown/hidden as textareas gain/lose focus)
// and the shared full-screen "are you sure?" confirm (see ui_shell.h for
// the add/edit-form screen mechanism these confirms are opened on top of).
#ifndef WILICANKIT_UI_COMMON_H
#define WILICANKIT_UI_COMMON_H
#include "lvgl.h"

// Creates the shared on-screen keyboard (hidden by default) as a child of
// `screen`. Call once during UI shell setup, before any form is opened.
void ui_common_init(lv_obj_t *screen);

// Wires `ta` so focusing it shows the shared keyboard (in the given mode)
// bound to it; defocusing hides the keyboard again.
void ui_common_bind_keyboard(lv_obj_t *ta, lv_keyboard_mode_t mode);

// Called with `user_data` when the confirm screen's destructive action
// button is pressed (see ui_common_show_confirm).
typedef void (*ui_confirm_action_t)(void *user_data);

// Full-screen replacement for ui_common_open_confirm, backed by
// ui_shell_open_confirm: shows `message` (wrapped) on a shared, lazily-
// created confirm screen with the bottom bar reading
// Back / action_label / (blank) / (blank) / (blank) (action slot red).
// `on_confirm` is responsible for calling ui_shell_close() itself (1 to
// return to whatever screen opened the confirm, or more to also skip back
// past that screen — e.g. after a delete). Only one confirm can be open at
// a time.
void ui_common_show_confirm(const char *message, const char *action_label,
                             ui_confirm_action_t on_confirm, void *user_data);

#endif // WILICANKIT_UI_COMMON_H
