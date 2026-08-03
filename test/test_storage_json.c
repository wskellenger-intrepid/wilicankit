// tests/test_storage_json.c — round-trips can_signal_t/can_message_t/
// can_control_t tables through storage_build_json()/storage_parse_json()
// (apps/wilicankit/storage_json.c), independent of FatFs/PSRAM/hardware.
// The JSON schema is sparse/id-keyed (v2): only in_use entries are written,
// each carrying its own "id" — this test exercises that directly by only
// populating a couple of far-apart slots, not every CAN_MAX_* slot.
#include "test_util.h"
#include "storage_json.h"
#include <stdio.h>
#include <string.h>

static void fill_sample(can_signal_t  sig[CAN_MAX_SIGNALS],
                         can_message_t msg[CAN_MAX_MESSAGES],
                         can_control_t ctrl[CAN_MAX_CONTROLS]) {
    memset(sig, 0, sizeof(can_signal_t) * CAN_MAX_SIGNALS);
    memset(msg, 0, sizeof(can_message_t) * CAN_MAX_MESSAGES);
    memset(ctrl, 0, sizeof(can_control_t) * CAN_MAX_CONTROLS);
    // Match app_state_reset_all()'s sentinel defaults (storage_parse_json
    // pre-fills the same way), not a plain zero-memset.
    for (int m = 0; m < CAN_MAX_MESSAGES; m++)
        for (int p = 0; p < CAN_MAX_PLACEMENTS; p++)
            msg[m].placements[p].signal_id = CAN_SIGNAL_ID_NONE;
    for (int c = 0; c < CAN_MAX_CONTROLS; c++)
        ctrl[c].signal_id = CAN_SIGNAL_ID_NONE;

    sig[0].in_use = true;
    strcpy(sig[0].name, "RPM");
    strcpy(sig[0].units, "rpm");
    sig[0].bit_length = 16;
    sig[0].scale = 0.25;
    sig[0].offset = -40.0;
    sig[0].min_value = -40.0;
    sig[0].max_value = 8000.0;
    sig[0].value = 1234.5;

    sig[CAN_MAX_SIGNALS - 1].in_use = true;   // sparse: a big gap between ids
    strcpy(sig[CAN_MAX_SIGNALS - 1].name, "Last");
    sig[CAN_MAX_SIGNALS - 1].bit_length = 1;
    sig[CAN_MAX_SIGNALS - 1].scale = 1.0;

    msg[0].in_use = true;
    strcpy(msg[0].name, "Engine");
    msg[0].can_id = 0x1FFFFFFFu;   // max 29-bit extended ID
    msg[0].extended_id = true;
    msg[0].dlc = 8;
    msg[0].channel = 1;
    msg[0].period_us = 100000;
    msg[0].enabled = true;
    msg[0].placement_count = 2;
    msg[0].placements[0].signal_id = 0;
    msg[0].placements[0].start_bit = 0;
    msg[0].placements[0].big_endian = false;
    msg[0].placements[1].signal_id = CAN_SIGNAL_ID_NONE;   // active placement, no signal bound yet
    msg[0].placements[1].start_bit = 7;
    msg[0].placements[1].big_endian = true;

    ctrl[0].in_use = true;
    ctrl[0].signal_id = 0;
    ctrl[0].control_type = CAN_CONTROL_TOGGLE;
}

static bool signals_equal(const can_signal_t *a, const can_signal_t *b) {
    return a->in_use == b->in_use
        && strcmp(a->name, b->name) == 0
        && strcmp(a->units, b->units) == 0
        && a->bit_length == b->bit_length
        && a->scale == b->scale && a->offset == b->offset
        && a->min_value == b->min_value && a->max_value == b->max_value
        && a->value == b->value;
}

static bool placements_equal(const can_placement_t *a, const can_placement_t *b) {
    return a->signal_id == b->signal_id && a->start_bit == b->start_bit && a->big_endian == b->big_endian;
}

static bool messages_equal(const can_message_t *a, const can_message_t *b) {
    if (a->in_use != b->in_use || strcmp(a->name, b->name) != 0 || a->can_id != b->can_id
        || a->extended_id != b->extended_id || a->dlc != b->dlc || a->channel != b->channel
        || a->period_us != b->period_us || a->enabled != b->enabled
        || a->placement_count != b->placement_count) {
        return false;
    }
    for (int i = 0; i < CAN_MAX_PLACEMENTS; i++) {
        if (!placements_equal(&a->placements[i], &b->placements[i])) return false;
    }
    return true;
}

static bool controls_equal(const can_control_t *a, const can_control_t *b) {
    return a->in_use == b->in_use && a->signal_id == b->signal_id && a->control_type == b->control_type;
}

int main(void) {
    can_signal_t  signals[CAN_MAX_SIGNALS];
    can_message_t messages[CAN_MAX_MESSAGES];
    can_control_t controls[CAN_MAX_CONTROLS];
    fill_sample(signals, messages, controls);

    cJSON *root = storage_build_json(signals, messages, controls);
    ASSERT_TRUE(root != NULL);

    // Sparse: only 2 signals / 1 message / 1 control are in_use — the
    // built tree must not contain one entry per CAN_MAX_* slot.
    ASSERT_TRUE(cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(root, "signals")) == 2);
    ASSERT_TRUE(cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(root, "messages")) == 1);
    ASSERT_TRUE(cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(root, "controls")) == 1);

    char *text = cJSON_Print(root);
    ASSERT_TRUE(text != NULL);

    // Reparse the printed text, as storage_load_config() would after an
    // f_read — proves the schema survives a real text round trip, not just
    // an in-memory tree copy.
    cJSON *reparsed = cJSON_Parse(text);
    ASSERT_TRUE(reparsed != NULL);

    can_signal_t  out_signals[CAN_MAX_SIGNALS];
    can_message_t out_messages[CAN_MAX_MESSAGES];
    can_control_t out_controls[CAN_MAX_CONTROLS];
    memset(out_signals, 0xAA, sizeof out_signals);
    memset(out_messages, 0xAA, sizeof out_messages);
    memset(out_controls, 0xAA, sizeof out_controls);
    ASSERT_TRUE(storage_parse_json(reparsed, out_signals, out_messages, out_controls));

    // storage_parse_json must fully reset every slot itself (not just the
    // ones actually named in the sparse JSON) — the 0xAA pre-fill above
    // plus this full-range comparison is what proves that.
    for (int i = 0; i < CAN_MAX_SIGNALS; i++) ASSERT_TRUE(signals_equal(&signals[i], &out_signals[i]));
    for (int i = 0; i < CAN_MAX_MESSAGES; i++) ASSERT_TRUE(messages_equal(&messages[i], &out_messages[i]));
    for (int i = 0; i < CAN_MAX_CONTROLS; i++) ASSERT_TRUE(controls_equal(&controls[i], &out_controls[i]));

    cJSON_free(text);
    cJSON_Delete(root);
    cJSON_Delete(reparsed);

    // --- malformed/partial input must fail cleanly, not partially decode ---
    {
        cJSON *bad = cJSON_Parse("{\"format_version\": 999}");
        ASSERT_TRUE(!storage_parse_json(bad, out_signals, out_messages, out_controls));
        cJSON_Delete(bad);
    }
    ASSERT_TRUE(!storage_parse_json(NULL, out_signals, out_messages, out_controls));

    // The old v1 (fixed-position) format_version must now be rejected.
    {
        cJSON *v1 = cJSON_Parse("{\"format_version\": 1, \"signals\": [], \"messages\": [], \"controls\": []}");
        ASSERT_TRUE(!storage_parse_json(v1, out_signals, out_messages, out_controls));
        cJSON_Delete(v1);
    }

    // Duplicate id within one array must fail the whole parse.
    {
        const char *dup =
            "{\"format_version\": 2,"
            " \"signals\": ["
            "  {\"id\": 0, \"name\": \"A\", \"units\": \"\", \"bit_length\": 1,"
            "   \"scale\": 1, \"offset\": 0, \"min_value\": 0, \"max_value\": 1, \"value\": 0},"
            "  {\"id\": 0, \"name\": \"B\", \"units\": \"\", \"bit_length\": 1,"
            "   \"scale\": 1, \"offset\": 0, \"min_value\": 0, \"max_value\": 1, \"value\": 0}"
            " ], \"messages\": [], \"controls\": []}";
        cJSON *doc = cJSON_Parse(dup);
        ASSERT_TRUE(doc != NULL);
        ASSERT_TRUE(!storage_parse_json(doc, out_signals, out_messages, out_controls));
        cJSON_Delete(doc);
    }

    // Out-of-range id must fail the whole parse.
    {
        char buf[320];
        snprintf(buf, sizeof buf,
            "{\"format_version\": 2,"
            " \"signals\": [{\"id\": %d, \"name\": \"X\", \"units\": \"\", \"bit_length\": 1,"
            "  \"scale\": 1, \"offset\": 0, \"min_value\": 0, \"max_value\": 1, \"value\": 0}],"
            " \"messages\": [], \"controls\": []}",
            CAN_MAX_SIGNALS);
        cJSON *doc = cJSON_Parse(buf);
        ASSERT_TRUE(doc != NULL);
        ASSERT_TRUE(!storage_parse_json(doc, out_signals, out_messages, out_controls));
        cJSON_Delete(doc);
    }

    // Out-of-range signal_id in a placement must fail the whole parse.
    {
        char buf[400];
        snprintf(buf, sizeof buf,
            "{\"format_version\": 2, \"signals\": [], \"controls\": [],"
            " \"messages\": [{\"id\": 0, \"name\": \"M\", \"can_id\": 1, \"extended_id\": false,"
            "  \"dlc\": 8, \"channel\": 0, \"period_us\": 0, \"enabled\": false,"
            "  \"placements\": [{\"signal_id\": %d, \"start_bit\": 0, \"big_endian\": false}]}]}",
            CAN_MAX_SIGNALS);
        cJSON *doc = cJSON_Parse(buf);
        ASSERT_TRUE(doc != NULL);
        ASSERT_TRUE(!storage_parse_json(doc, out_signals, out_messages, out_controls));
        cJSON_Delete(doc);
    }

    // Out-of-range signal_id in a control must fail the whole parse.
    {
        char buf[256];
        snprintf(buf, sizeof buf,
            "{\"format_version\": 2, \"signals\": [], \"messages\": [],"
            " \"controls\": [{\"id\": 0, \"signal_id\": %d, \"control_type\": 0}]}",
            CAN_MAX_SIGNALS);
        cJSON *doc = cJSON_Parse(buf);
        ASSERT_TRUE(doc != NULL);
        ASSERT_TRUE(!storage_parse_json(doc, out_signals, out_messages, out_controls));
        cJSON_Delete(doc);
    }

    TEST_RETURN();
}

