/* apps/wilicankit/lv_conf.h — minimal LVGL v9.2.x config for this app.
 * Only overrides that differ from LVGL's built-in defaults are listed here;
 * everything else falls back to lv_conf_internal.h's defaults (which mirror
 * lv_conf_template.h) — see lvgl/src/lv_conf_internal.h if you need to check
 * what a given default is. */

#ifndef LV_CONF_H
#define LV_CONF_H

/* RGB565, matches the ST7796 panel (bsp/display/st7796.h). Byte order (LVGL
 * is little-endian native, the panel wants big-endian on the wire) is fixed
 * up per-flush with lv_draw_sw_rgb565_swap() in lvgl_port.c, not here. */
#define LV_COLOR_DEPTH 16

/* Builtin allocator, backed by a static PSRAM pool (lv_psram_pool.c) instead
 * of SRAM — see LV_MEM_POOL_INCLUDE/LV_MEM_POOL_ALLOC below. This app kept
 * hitting LV_MEM_SIZE exhaustion (clip-corner compositing layers, nested
 * modals, bigger loaded configs — repo memory: "LVGL LV_MEM_SIZE
 * exhaustion...") when the pool lived in the tight 512 KB SRAM budget.
 * PSRAM has 8 MB and ~7.8 MB of that is otherwise unused, so the object
 * heap now gets a much bigger pool there instead of trimming SRAM further
 * or working around individual allocation sites (e.g. radius=0 to dodge
 * clip_corner layers). */
#define LV_USE_STDLIB_MALLOC LV_STDLIB_BUILTIN
#define LV_MEM_SIZE (1024U * 1024U)
#define LV_MEM_POOL_INCLUDE "lv_psram_pool.h"
#define LV_MEM_POOL_ALLOC lv_psram_pool_alloc

/* No RTOS — bare super-loop, single draw thread. */
#define LV_USE_OS LV_OS_NONE

/* Diagnostics go through DIAG()/RTT elsewhere in this app; skip LVGL's own
 * logging to keep things simple. */
#define LV_USE_LOG 0

/* Keep the LVGL-fetched tree lean: no example/demo source files needed. */
#define LV_BUILD_EXAMPLES 0

/* Config tab browses the USB stick's saved-config folder directly via
 * LVGL's own FatFs driver (src/libs/fsdrv/lv_fs_fatfs.c, lv_fs_fatfs_init()
 * — called from main.c) instead of a hand-rolled row list. Paths look like
 * "F:/wilicankit/name.json"; FatFs itself only ever mounts one volume
 * ("0:", see bsp/usbhost/usb_store.c), so the driver letter here is purely
 * an LVGL-side namespace, unrelated to FatFs's own "0:" drive string. No
 * music/pictures/video/documents dirs on this board, so the built-in
 * quick-access sidebar (which would need those) stays off. */
#define LV_USE_FILE_EXPLORER 1
#define LV_FILE_EXPLORER_QUICK_ACCESS 0
#define LV_USE_FS_FATFS 1
#define LV_FS_FATFS_LETTER 'F'

/* Widgets this app never uses (verified against every lv_*_create() call
 * site in apps/wilicankit/*.c) — disabled to free copy_to_ram code space.
 * Each widget's .c file is wrapped in `#if LV_USE_<X> != 0`, so leaving one
 * of these off that turns out to still be needed is a build-time error, not
 * a runtime risk. Widgets actually used (label, button, switch, dropdown,
 * textarea, keyboard, list, slider, menu) and their hard dependencies
 * (buttonmatrix for keyboard, bar for slider, image for menu's back-button
 * icon, table for lv_file_explorer's file listing) are left at their
 * default-on values and are NOT touched here. */
#define LV_USE_ANIMIMG     0
#define LV_USE_ARC         0
#define LV_USE_CALENDAR    0
#define LV_USE_CANVAS      0
#define LV_USE_CHART       0
#define LV_USE_CHECKBOX    0
#define LV_USE_IMAGEBUTTON 0
#define LV_USE_LED         0
#define LV_USE_LINE        0
#define LV_USE_MSGBOX      0
#define LV_USE_ROLLER      0
#define LV_USE_SCALE       0
#define LV_USE_SPAN        0
#define LV_USE_SPINBOX     0
#define LV_USE_SPINNER     0
#define LV_USE_TABVIEW     0
#define LV_USE_TILEVIEW    0
#define LV_USE_WIN         0

#endif /*LV_CONF_H*/
