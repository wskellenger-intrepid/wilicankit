// apps/wilicankit/ow_link.h — owns the app's single OneWili link to the MAIN
// CPU. Opened once from main(), then the device pointer is handed to each
// module that needs MAIN (can_link.c for CAN TX, device_leds.c for the board
// LEDs). onewili_fwgui.h: "Single link per board" — there is exactly one
// ow_device in the app and it lives here.
#ifndef OW_LINK_H_
#define OW_LINK_H_
#include <stdbool.h>

struct ow_device;

// Opens the FwGUI link. Blocks, retrying forever (with a DIAG message per
// attempt) until it comes up. Call once at startup, after board_init().
void ow_link_open(void);

// The opened device, or NULL before ow_link_open() completes.
struct ow_device *ow_link_device(void);

bool ow_link_is_open(void);

// Resets the DISPLAY CPU, which restores the stock GUI firmware (flash is
// never touched) -- this app's own execution ends here. No-op if the link
// isn't open. Used for the long-press-Home "exit app" gesture.
void ow_link_exit_app(void);

#endif // OW_LINK_H_
