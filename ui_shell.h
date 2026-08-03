// apps/wilicankit/ui_shell.h — top-level lv_menu sidebar (Monitor / Transmit /
// Diagnostics / Data / Config) and the shared on-screen keyboard singleton.
#ifndef UI_SHELL_H_
#define UI_SHELL_H_
#include "lvgl.h"

// Builds the whole UI on the active LVGL screen. Call once after
// lvgl_port_init().
void ui_shell_create(void);

// Refreshes the Online/Offline button if can_link's online state changed
// since the last call (e.g. auto-dropped offline after a send failure).
// Call once per main-loop iteration, after can_link_periodic_poll().
void ui_shell_poll(void);

// The hidden-by-default overlay layer that add/edit-form and confirm
// screens live in. Content modules (ui_signals.c etc.) create their
// persistent form/confirm pages as children of this container once, during
// their own *_create() call from ui_shell_create() — see ui_shell_open_form.
lv_obj_t *ui_shell_overlay(void);

// Shows `page` (already a child of ui_shell_overlay(), pre-built by the
// caller) as a full-screen "form", with the bottom bar reconfigured to
// Back / save_label / extra_label / (blank) / Delete. `crumb` is the
// breadcrumb segment appended to the app-bar title (e.g. "Add Signal"),
// distinct from `save_label` (the button text, e.g. "Save"). `extra_label`/
// `extra_cb` may both be NULL to leave that slot blank (most forms don't
// need it — Messages' form uses it for its "Signals" drill-down action).
// `delete_cb == NULL` leaves the Delete slot blank/disabled (e.g. while
// adding a brand-new item that can't yet be deleted).
void ui_shell_open_form(const char *crumb, lv_obj_t *page, const char *save_label, void (*save_cb)(void),
                        const char *extra_label, void (*extra_cb)(void),
                        void (*delete_cb)(void));

// Shows `page` as a full-screen yes/no confirmation, with the bottom bar
// reconfigured to Back / action_label / (blank) / (blank) / (blank). The
// action slot is styled red (destructive), matching this app's existing
// confirm-button convention. `action_label` also serves as the breadcrumb
// segment appended to the app-bar title.
void ui_shell_open_confirm(lv_obj_t *page, const char *action_label, void (*action_cb)(void));

// Leaves `levels` screens pushed via ui_shell_open_form/open_confirm.
// levels == 1 is the same as a physical Back tap; > 1 lets a destructive
// confirm's success path skip back past the form it was opened from,
// straight to the list that owns it.
void ui_shell_close(int levels);

#endif // UI_SHELL_H_

