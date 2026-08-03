// apps/wilicankit/ui_signals.h — Signals tab: list existing signals, add new
// ones, edit/delete existing ones via a modal form.
#ifndef WILICANKIT_UI_SIGNALS_H
#define WILICANKIT_UI_SIGNALS_H
#include "lvgl.h"

// Populates `parent` (a tabview content container) with the Signals screen.
lv_obj_t *ui_signals_create(lv_obj_t *parent);

// Rebuilds the signal list from app_state. Call after any add/edit/delete.
void ui_signals_refresh(void);

// Opens the "Add Signal" form. Called from the bottom bar (ui_shell.c).
void ui_signals_add(void);

#endif // WILICANKIT_UI_SIGNALS_H
