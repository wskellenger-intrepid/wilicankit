// apps/wilicankit/device_leds.h — blanks the board's addressable RGB LEDs by
// power-cycling their zone, so nothing here drives the WS2812s directly.
#ifndef DEVICE_LEDS_H_
#define DEVICE_LEDS_H_

// Power-cycles zone 10 (addressable RGB LEDs): the WS2812s hold whatever the
// previously-loaded app latched into them, and dropping their rail is what
// clears it. The rail is switched back on afterwards so it is left in its
// normal state for whatever runs after this app. Call every super-loop pass;
// it runs once, then no-ops. Non-blocking.
void device_leds_task(void);

#endif // DEVICE_LEDS_H_
