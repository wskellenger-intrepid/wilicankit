// apps/wilicankit/ui_messages.h — Messages tab: define CAN messages (ID,

#ifndef UI_MESSAGES_H_
#define UI_MESSAGES_H_
// length, channel) and place existing signals into them with a start bit
// and endianness. Periodic rate / enable / one-shot send live on the
// Transmit tab (ui_transmit.h), not here.
#include "lvgl.h"

lv_obj_t *ui_messages_create(lv_obj_t *parent);
void ui_messages_refresh(void);

// Opens the "Add Message" form. Called from the bottom bar (ui_shell.c).
void ui_messages_add(void);


#endif // UI_MESSAGES_H_
