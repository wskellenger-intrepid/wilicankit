// apps/wilicankit/app_state.h — shared in-memory tables for signals,
// messages, and control (slider/toggle) bindings. Single global instance
// (this app has no concept of multiple simultaneous "documents" open at
// once — Phase 8's save/load swaps the whole set of tables in and out of
// these arrays).
#ifndef WILICANKIT_APP_STATE_H
#define WILICANKIT_APP_STATE_H
#include "can_model.h"

extern can_signal_t  g_signals[CAN_MAX_SIGNALS];
extern can_message_t g_messages[CAN_MAX_MESSAGES];
extern can_control_t g_controls[CAN_MAX_CONTROLS];

// Each finds the first free (!in_use) slot, zero-initializes it, marks it
// in_use (with sane defaults), and returns its index — or -1 if full.
int app_state_alloc_signal(void);
int app_state_alloc_message(void);
int app_state_alloc_control(void);

// Frees a signal slot and clears every placement/slider that referenced it
// (leaving them in place but pointing at CAN_SIGNAL_ID_NONE).
void app_state_free_signal(uint8_t id);
void app_state_free_message(uint8_t id);
void app_state_free_control(uint8_t id);

// Clears every signal/message/control back to the empty (!in_use) state
// this app boots into — the "New" button's unload-config action.
void app_state_reset_all(void);

#endif // WILICANKIT_APP_STATE_H
