// apps/wilicankit/ui_controls.h — Controls tab: add sliders/toggles bound to
// existing signals; moving a slider or flipping a toggle updates the
// signal's physical value and immediately re-sends (one-shot) any enabled
// message containing it. The per-card Sine and Random buttons deliberately
// do NOT re-send: they only update the signal's value, leaving it to each
// message's own periodic config to decide when it reaches the bus.
#ifndef WILICANKIT_UI_CONTROLS_H
#define WILICANKIT_UI_CONTROLS_H
#include "lvgl.h"

lv_obj_t *ui_controls_create(lv_obj_t *parent);
void ui_controls_refresh(void);

// Opens the Add Slider/Add Toggle form. Called from the bottom bar (ui_shell.c).
void ui_controls_add_slider(void);
void ui_controls_add_toggle(void);

#endif // WILICANKIT_UI_CONTROLS_H


