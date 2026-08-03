// apps/wilicankit/app_state.c — see app_state.h.
#include "app_state.h"
#include "pico/stdlib.h"   // __uninitialized_psram, via pico/platform/sections.h
#include <string.h>

can_signal_t  __uninitialized_psram("wilicankit_signals")  g_signals[CAN_MAX_SIGNALS];
can_message_t __uninitialized_psram("wilicankit_messages") g_messages[CAN_MAX_MESSAGES];
can_control_t __uninitialized_psram("wilicankit_controls") g_controls[CAN_MAX_CONTROLS];

int app_state_alloc_signal(void) {
    for (int i = 0; i < CAN_MAX_SIGNALS; i++) {
        if (!g_signals[i].in_use) {
            memset(&g_signals[i], 0, sizeof g_signals[i]);
            g_signals[i].in_use = true;
            g_signals[i].scale = 1.0;
            g_signals[i].bit_length = 8;
            g_signals[i].max_value = 255.0;
            return i;
        }
    }
    return -1;
}

int app_state_alloc_message(void) {
    for (int i = 0; i < CAN_MAX_MESSAGES; i++) {
        if (!g_messages[i].in_use) {
            memset(&g_messages[i], 0, sizeof g_messages[i]);
            g_messages[i].in_use = true;
            g_messages[i].dlc = 8;
            for (int p = 0; p < CAN_MAX_PLACEMENTS; p++)
                g_messages[i].placements[p].signal_id = CAN_SIGNAL_ID_NONE;
            return i;
        }
    }
    return -1;
}

int app_state_alloc_control(void) {
    for (int i = 0; i < CAN_MAX_CONTROLS; i++) {
        if (!g_controls[i].in_use) {
            memset(&g_controls[i], 0, sizeof g_controls[i]);
            g_controls[i].in_use = true;
            g_controls[i].signal_id = CAN_SIGNAL_ID_NONE;
            return i;
        }
    }
    return -1;
}

void app_state_free_signal(uint8_t id) {
    if (id >= CAN_MAX_SIGNALS) return;
    g_signals[id].in_use = false;
    for (int m = 0; m < CAN_MAX_MESSAGES; m++) {
        can_message_t *msg = &g_messages[m];
        for (int p = 0; p < msg->placement_count; p++) {
            if (msg->placements[p].signal_id == id)
                msg->placements[p].signal_id = CAN_SIGNAL_ID_NONE;
        }
    }
    for (int s = 0; s < CAN_MAX_CONTROLS; s++) {
        if (g_controls[s].in_use && g_controls[s].signal_id == id)
            g_controls[s].in_use = false;
    }
}

void app_state_free_message(uint8_t id) {
    if (id >= CAN_MAX_MESSAGES) return;
    g_messages[id].in_use = false;
}

void app_state_free_control(uint8_t id) {
    if (id >= CAN_MAX_CONTROLS) return;
    g_controls[id].in_use = false;
}

void app_state_reset_all(void) {
    memset(g_signals, 0, sizeof g_signals);
    memset(g_messages, 0, sizeof g_messages);
    memset(g_controls, 0, sizeof g_controls);
    for (int m = 0; m < CAN_MAX_MESSAGES; m++)
        for (int p = 0; p < CAN_MAX_PLACEMENTS; p++)
            g_messages[m].placements[p].signal_id = CAN_SIGNAL_ID_NONE;
    for (int c = 0; c < CAN_MAX_CONTROLS; c++)
        g_controls[c].signal_id = CAN_SIGNAL_ID_NONE;
}
