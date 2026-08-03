// apps/wilicankit/ui_monitor.h — Monitor area's Health leaf (canvas stats).
#ifndef UI_MONITOR_H_
#define UI_MONITOR_H_
#include "lvgl.h"

lv_obj_t *ui_monitor_create(lv_obj_t *parent);
void ui_monitor_refresh(void);

#endif // UI_MONITOR_H_
