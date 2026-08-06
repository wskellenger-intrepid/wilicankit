// apps/wilicankit/ow_link.c — see ow_link.h.
#include "ow_link.h"
#include "onewili.h"
#include "onewili_fwgui.h"
#include "platform/diag.h"
#include "pico/stdlib.h"

// ~37 KB of buffers (per toggleled) — far too big for the 2 KB stack, and
// too big for SRAM's fixed 512 KB copy_to_ram budget too (was 45% of all
// .bss). Only touched by blocking, low-frequency serial calls (open, CAN
// writes, GPIO reads) -- no DMA/ISR, so PSRAM's extra QSPI latency doesn't
// matter here the way it does for the LVGL draw buffers.
static ow_device __uninitialized_psram("wilicankit_ow_device") s_dev;
static bool s_open = false;

void ow_link_open(void) {
    while (ow_open_fwgui(&s_dev) != OW_OK) {
        DIAG("ow_link: FwGUI link open failed (is the main CPU running stock fw?), retry in 1 s\n");
        sleep_ms(1000);
    }
    s_open = true;
    DIAG("ow_link: link up\n");
}

struct ow_device *ow_link_device(void) {
    return s_open ? &s_dev : NULL;
}

bool ow_link_is_open(void) {
    return s_open;
}
