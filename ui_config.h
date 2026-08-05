// apps/wilicankit/ui_config.h — Config tab: New (reset to blank) / Save As
// (name via keyboard) over the saved configs on either the SD card (default,
// a flat lv_list -- see ui_config.c) or the USB stick (lv_file_explorer) --
// tap an entry to Load, or use the Delete toolbar button.
#ifndef WILICANKIT_UI_CONFIG_H
#define WILICANKIT_UI_CONFIG_H
#include "lvgl.h"

lv_obj_t *ui_config_create(lv_obj_t *parent);
void ui_config_refresh(void);

// Toolbar actions, called from the bottom bar (ui_shell.c).
void ui_config_new(void);
void ui_config_save_as(void);
void ui_config_delete(void);

#endif // WILICANKIT_UI_CONFIG_H
