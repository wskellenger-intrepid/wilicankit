// apps/wilicankit/storage_json.h — pure struct<->JSON encode/decode for the
// signal/message/control tables (app_state.h). No file I/O, no FatFs, no
// PSRAM — safe to compile and unit-test on the host (see
// tests/test_storage_json.c). storage.h/.c layers file I/O on top of this.
#ifndef WILICANKIT_STORAGE_JSON_H
#define WILICANKIT_STORAGE_JSON_H
#include "can_model.h"
#include "cJSON.h"
#include <stdbool.h>

// Bumped only if the schema below changes incompatibly; unrelated to the
// old binary format's STORAGE_VERSION (that format is gone). v1 (fixed
// slot position, every CAN_MAX_* slot written including unused ones) is
// gone as of v2 — see tools/wilicankit_json_migrate_v1_to_v2.py to convert
// an old v1 file offline; the firmware itself only understands v2.
#define STORAGE_JSON_FORMAT_VERSION 2

// Builds one JSON document: {"format_version", "signals", "messages",
// "controls"}. Each array is sparse — only in_use entries are written, and
// each entry carries an explicit "id" field (its table slot index) so
// order in the file doesn't matter and unused slots cost nothing. A
// message's "placements" array is trimmed to its actual placement_count
// (no CAN_MAX_PLACEMENTS padding); placement_count itself isn't stored —
// it's derived from the array length on decode. signal_id references
// (in placements/controls) are plain numeric ids matching the referenced
// signal's own "id" field. Caller owns the returned tree (cJSON_Delete).
// Returns NULL only on allocator OOM.
cJSON *storage_build_json(const can_signal_t  signals[CAN_MAX_SIGNALS],
                           const can_message_t messages[CAN_MAX_MESSAGES],
                           const can_control_t controls[CAN_MAX_CONTROLS]);

// Parses a document built by storage_build_json() (or a hand-authored/
// converted equivalent) into the given output tables, which are fully
// reset first (so any slot not present in the JSON ends up !in_use).
// Returns false on any missing/wrong-typed field, a format_version
// mismatch, a duplicate or out-of-range "id", or an out-of-range
// signal_id reference — the caller must treat out_* as invalid (not
// partially applied) on failure.
bool storage_parse_json(const cJSON    *root,
                         can_signal_t  out_signals[CAN_MAX_SIGNALS],
                         can_message_t out_messages[CAN_MAX_MESSAGES],
                         can_control_t out_controls[CAN_MAX_CONTROLS]);

#endif // WILICANKIT_STORAGE_JSON_H
