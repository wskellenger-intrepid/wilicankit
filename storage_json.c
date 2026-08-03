// apps/wilicankit/storage_json.c — see storage_json.h.
#include "storage_json.h"
#include <string.h>

static cJSON *encode_signal(const can_signal_t *s, int id) {
    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "id", id);
    cJSON_AddStringToObject(o, "name", s->name);
    cJSON_AddStringToObject(o, "units", s->units);
    cJSON_AddNumberToObject(o, "bit_length", s->bit_length);
    cJSON_AddNumberToObject(o, "scale", s->scale);
    cJSON_AddNumberToObject(o, "offset", s->offset);
    cJSON_AddNumberToObject(o, "min_value", s->min_value);
    cJSON_AddNumberToObject(o, "max_value", s->max_value);
    cJSON_AddNumberToObject(o, "value", s->value);
    return o;
}

static cJSON *encode_placement(const can_placement_t *p) {
    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "signal_id", p->signal_id);
    cJSON_AddNumberToObject(o, "start_bit", p->start_bit);
    cJSON_AddBoolToObject(o, "big_endian", p->big_endian);
    return o;
}

static cJSON *encode_message(const can_message_t *m, int id) {
    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "id", id);
    cJSON_AddStringToObject(o, "name", m->name);
    cJSON_AddNumberToObject(o, "can_id", m->can_id);
    cJSON_AddBoolToObject(o, "extended_id", m->extended_id);
    cJSON_AddNumberToObject(o, "dlc", m->dlc);
    cJSON_AddNumberToObject(o, "channel", m->channel);
    cJSON_AddNumberToObject(o, "period_us", m->period_us);
    cJSON_AddBoolToObject(o, "enabled", m->enabled);
    cJSON *placements = cJSON_CreateArray();
    int count = m->placement_count < CAN_MAX_PLACEMENTS ? m->placement_count : CAN_MAX_PLACEMENTS;
    for (int i = 0; i < count; i++) {
        cJSON_AddItemToArray(placements, encode_placement(&m->placements[i]));
    }
    cJSON_AddItemToObject(o, "placements", placements);
    return o;
}

static cJSON *encode_control(const can_control_t *c, int id) {
    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "id", id);
    cJSON_AddNumberToObject(o, "signal_id", c->signal_id);
    cJSON_AddNumberToObject(o, "control_type", c->control_type);
    return o;
}

cJSON *storage_build_json(const can_signal_t  signals[CAN_MAX_SIGNALS],
                           const can_message_t messages[CAN_MAX_MESSAGES],
                           const can_control_t controls[CAN_MAX_CONTROLS]) {
    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;
    cJSON_AddNumberToObject(root, "format_version", STORAGE_JSON_FORMAT_VERSION);

    cJSON *sig_arr = cJSON_CreateArray();
    for (int i = 0; i < CAN_MAX_SIGNALS; i++) {
        if (!signals[i].in_use) continue;
        cJSON_AddItemToArray(sig_arr, encode_signal(&signals[i], i));
    }
    cJSON_AddItemToObject(root, "signals", sig_arr);

    cJSON *msg_arr = cJSON_CreateArray();
    for (int i = 0; i < CAN_MAX_MESSAGES; i++) {
        if (!messages[i].in_use) continue;
        cJSON_AddItemToArray(msg_arr, encode_message(&messages[i], i));
    }
    cJSON_AddItemToObject(root, "messages", msg_arr);

    cJSON *ctrl_arr = cJSON_CreateArray();
    for (int i = 0; i < CAN_MAX_CONTROLS; i++) {
        if (!controls[i].in_use) continue;
        cJSON_AddItemToArray(ctrl_arr, encode_control(&controls[i], i));
    }
    cJSON_AddItemToObject(root, "controls", ctrl_arr);

    return root;
}

// --- decode helpers — each returns false (leaving *out untouched) on any
// missing/wrong-typed field, so a caller can bail out of a whole-table
// decode without partially applying a malformed entry. ---

static bool get_bool(const cJSON *obj, const char *key, bool *out) {
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!cJSON_IsBool(item)) return false;
    *out = cJSON_IsTrue(item);
    return true;
}

static bool get_uint(const cJSON *obj, const char *key, uint32_t *out) {
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!cJSON_IsNumber(item)) return false;
    *out = (uint32_t)item->valuedouble;
    return true;
}

static bool get_double(const cJSON *obj, const char *key, double *out) {
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!cJSON_IsNumber(item)) return false;
    *out = item->valuedouble;
    return true;
}

static bool get_string(const cJSON *obj, const char *key, char *out, size_t out_cap) {
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!cJSON_IsString(item) || item->valuestring == NULL) return false;
    strncpy(out, item->valuestring, out_cap - 1);
    out[out_cap - 1] = '\0';
    return true;
}

// signal_id reference fields (placements/controls): bounds-checked against
// CAN_MAX_SIGNALS so a malformed/corrupted file can't drive an out-of-bounds
// g_signals[] read later. CAN_SIGNAL_ID_NONE is always accepted as "no signal".
static bool decode_signal_ref(const cJSON *o, const char *key, uint8_t *out) {
    uint32_t u;
    if (!get_uint(o, key, &u)) return false;
    if (u != CAN_SIGNAL_ID_NONE && u >= CAN_MAX_SIGNALS) return false;
    *out = (uint8_t)u;
    return true;
}

static bool decode_signal(const cJSON *o, can_signal_t *s) {
    uint32_t u;
    if (!cJSON_IsObject(o)) return false;
    if (!get_string(o, "name", s->name, sizeof s->name)) return false;
    if (!get_string(o, "units", s->units, sizeof s->units)) return false;
    if (!get_uint(o, "bit_length", &u)) return false;
    s->bit_length = (uint8_t)u;
    if (!get_double(o, "scale", &s->scale)) return false;
    if (!get_double(o, "offset", &s->offset)) return false;
    if (!get_double(o, "min_value", &s->min_value)) return false;
    if (!get_double(o, "max_value", &s->max_value)) return false;
    if (!get_double(o, "value", &s->value)) return false;
    return true;
}

static bool decode_placement(const cJSON *o, can_placement_t *p) {
    uint32_t u;
    if (!cJSON_IsObject(o)) return false;
    if (!decode_signal_ref(o, "signal_id", &p->signal_id)) return false;
    if (!get_uint(o, "start_bit", &u)) return false;
    p->start_bit = (uint8_t)u;
    if (!get_bool(o, "big_endian", &p->big_endian)) return false;
    return true;
}

static bool decode_message(const cJSON *o, can_message_t *m) {
    uint32_t u;
    if (!cJSON_IsObject(o)) return false;
    if (!get_string(o, "name", m->name, sizeof m->name)) return false;
    if (!get_uint(o, "can_id", &m->can_id)) return false;
    if (!get_bool(o, "extended_id", &m->extended_id)) return false;
    if (!get_uint(o, "dlc", &u)) return false;
    m->dlc = (uint8_t)u;
    if (!get_uint(o, "channel", &u)) return false;
    m->channel = (uint8_t)u;
    if (!get_uint(o, "period_us", &m->period_us)) return false;
    if (!get_bool(o, "enabled", &m->enabled)) return false;

    const cJSON *placements = cJSON_GetObjectItemCaseSensitive(o, "placements");
    if (!cJSON_IsArray(placements)) return false;
    int count = cJSON_GetArraySize(placements);
    if (count < 0 || count > CAN_MAX_PLACEMENTS) return false;
    m->placement_count = (uint8_t)count;
    for (int i = 0; i < count; i++) {
        if (!decode_placement(cJSON_GetArrayItem(placements, i), &m->placements[i])) return false;
    }
    return true;
}

static bool decode_control(const cJSON *o, can_control_t *c) {
    uint32_t u;
    if (!cJSON_IsObject(o)) return false;
    if (!decode_signal_ref(o, "signal_id", &c->signal_id)) return false;
    if (!get_uint(o, "control_type", &u)) return false;
    c->control_type = (uint8_t)u;
    return true;
}

bool storage_parse_json(const cJSON    *root,
                         can_signal_t  out_signals[CAN_MAX_SIGNALS],
                         can_message_t out_messages[CAN_MAX_MESSAGES],
                         can_control_t out_controls[CAN_MAX_CONTROLS]) {
    if (!cJSON_IsObject(root)) return false;

    uint32_t version;
    if (!get_uint(root, "format_version", &version) || version != STORAGE_JSON_FORMAT_VERSION) return false;

    memset(out_signals, 0, sizeof(can_signal_t) * CAN_MAX_SIGNALS);
    memset(out_messages, 0, sizeof(can_message_t) * CAN_MAX_MESSAGES);
    memset(out_controls, 0, sizeof(can_control_t) * CAN_MAX_CONTROLS);
    for (int m = 0; m < CAN_MAX_MESSAGES; m++)
        for (int p = 0; p < CAN_MAX_PLACEMENTS; p++)
            out_messages[m].placements[p].signal_id = CAN_SIGNAL_ID_NONE;
    for (int c = 0; c < CAN_MAX_CONTROLS; c++)
        out_controls[c].signal_id = CAN_SIGNAL_ID_NONE;

    const cJSON *sig_arr = cJSON_GetObjectItemCaseSensitive(root, "signals");
    if (!cJSON_IsArray(sig_arr)) return false;
    {
        bool seen[CAN_MAX_SIGNALS] = {0};
        int n = cJSON_GetArraySize(sig_arr);
        for (int i = 0; i < n; i++) {
            const cJSON *o = cJSON_GetArrayItem(sig_arr, i);
            uint32_t id;
            if (!cJSON_IsObject(o) || !get_uint(o, "id", &id) || id >= CAN_MAX_SIGNALS || seen[id]) return false;
            if (!decode_signal(o, &out_signals[id])) return false;
            seen[id] = true;
            out_signals[id].in_use = true;
        }
    }

    const cJSON *msg_arr = cJSON_GetObjectItemCaseSensitive(root, "messages");
    if (!cJSON_IsArray(msg_arr)) return false;
    {
        bool seen[CAN_MAX_MESSAGES] = {0};
        int n = cJSON_GetArraySize(msg_arr);
        for (int i = 0; i < n; i++) {
            const cJSON *o = cJSON_GetArrayItem(msg_arr, i);
            uint32_t id;
            if (!cJSON_IsObject(o) || !get_uint(o, "id", &id) || id >= CAN_MAX_MESSAGES || seen[id]) return false;
            if (!decode_message(o, &out_messages[id])) return false;
            seen[id] = true;
            out_messages[id].in_use = true;
        }
    }

    const cJSON *ctrl_arr = cJSON_GetObjectItemCaseSensitive(root, "controls");
    if (!cJSON_IsArray(ctrl_arr)) return false;
    {
        bool seen[CAN_MAX_CONTROLS] = {0};
        int n = cJSON_GetArraySize(ctrl_arr);
        for (int i = 0; i < n; i++) {
            const cJSON *o = cJSON_GetArrayItem(ctrl_arr, i);
            uint32_t id;
            if (!cJSON_IsObject(o) || !get_uint(o, "id", &id) || id >= CAN_MAX_CONTROLS || seen[id]) return false;
            if (!decode_control(o, &out_controls[id])) return false;
            seen[id] = true;
            out_controls[id].in_use = true;
        }
    }

    return true;
}
