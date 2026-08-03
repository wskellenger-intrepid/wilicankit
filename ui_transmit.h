// apps/wilicankit/ui_transmit.h — Transmit tab: per-message periodic rate
// via a dropdown (fixed presets + "Custom", which unlocks a ms textbox),
// an "enabled" toggle switch (arms/disarms the OneWili periodic slot), and a
// one-shot "Send" button (disabled while periodic is enabled).
#ifndef WILICANKIT_UI_TRANSMIT_H
#define WILICANKIT_UI_TRANSMIT_H
#include "lvgl.h"

lv_obj_t *ui_transmit_create(lv_obj_t *parent);
void ui_transmit_refresh(void);

#endif // WILICANKIT_UI_TRANSMIT_H


