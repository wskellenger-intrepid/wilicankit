// apps/wilicankit/lvgl_port.h — LVGL <-> BSP glue: display flush (ST7796,
// async DMA) + touch indev (FT6336) + DPAD keypad indev (uartkbd nav
// cluster) + tick source. Call after board_init(), st7796_init(), and
// ft6336_init(). uartkbd_init() may run after this — the DPAD indev is only
// read later, from the main loop's lv_timer_handler() call.
#ifndef WILICANKIT_LVGL_PORT_H
#define WILICANKIT_LVGL_PORT_H

#include "input/uartkbd_parse.h"

typedef void (*lvgl_port_button_handler_t)(uartkbd_btn_t btn, bool pressed);

// lv_init() + display/indev/tick registration. Call once at startup.
void lvgl_port_init(void);

// Receives non-DPAD uartkbd button edges (soft colors + HOME/OK/CANCEL/PAGE)
// that the LVGL keypad indev does not consume.
void lvgl_port_set_button_handler(lvgl_port_button_handler_t handler);

#endif // WILICANKIT_LVGL_PORT_H
