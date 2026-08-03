// Bottom-nav / sub-tab icons, pre-rendered 24x24 A8 (alpha-only) bitmaps.
//
// Source: Tabler Icons (MIT licensed), https://github.com/tabler/tabler-icons
// -- PNG renders of the "outline" set fetched via the @tabler/icons-png
// package on jsDelivr into tools/icon_src/*.png (same icon set as the .svg
// originals already there). ui_icons.c is generated from those PNGs by
// tools/gen_icons.py (Pillow: downscale to 24x24, keep the alpha channel
// only) -- do not hand-edit ui_icons.c; re-run that script instead.
//
// Runtime SVG rendering (LV_USE_SVG) was tried first and dropped: its
// LV_USE_VECTOR_GRAPHIC/LV_USE_MATRIX/LV_USE_FLOAT prerequisites overflowed
// this copy_to_ram build's SRAM by ~28KB. These static bitmaps cost only a
// few KB total instead.
#ifndef UI_ICONS_H
#define UI_ICONS_H

#include "lvgl.h"

// Alpha-only (LV_COLOR_FORMAT_A8): tint with
// lv_obj_set_style_image_recolor()/_recolor_opa() at the call site.
extern const lv_image_dsc_t ui_icon_monitor;     // tools/icon_src/device-desktop.png
extern const lv_image_dsc_t ui_icon_transmit;    // tools/icon_src/upload.png
extern const lv_image_dsc_t ui_icon_diagnostics; // tools/icon_src/stethoscope.png
extern const lv_image_dsc_t ui_icon_setup;       // tools/icon_src/settings.png
extern const lv_image_dsc_t ui_icon_controls;    // tools/icon_src/adjustments-horizontal.png
extern const lv_image_dsc_t ui_icon_back;        // tools/icon_src/arrow-left.png
extern const lv_image_dsc_t ui_icon_load;        // tools/icon_src/folder.png
extern const lv_image_dsc_t ui_icon_signals;     // tools/icon_src/activity.png
extern const lv_image_dsc_t ui_icon_messages;    // tools/icon_src/mail.png
extern const lv_image_dsc_t ui_icon_plus;        // tools/icon_src/plus.png
extern const lv_image_dsc_t ui_icon_save;        // tools/icon_src/device-floppy.png
extern const lv_image_dsc_t ui_icon_delete;      // tools/icon_src/trash.png
extern const lv_image_dsc_t ui_icon_new;         // tools/icon_src/file-plus.png
extern const lv_image_dsc_t ui_icon_online;      // tools/icon_src/wifi.png

#endif // UI_ICONS_H
