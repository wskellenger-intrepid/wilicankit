// apps/wilicankit/storage.c — see storage.h. FatFs I/O + a PSRAM-backed
// bump allocator for cJSON; struct<->JSON encode/decode itself lives in
// storage_json.c.
#include "storage.h"
#include "storage_json.h"
#include "app_state.h"
#include "ff.h"
#include "cJSON.h"
#include "pico/stdlib.h"   // __uninitialized_psram, via pico/platform/sections.h
#include "platform/diag.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>

// Sized for a fully-populated config at the new CAN_MAX_* capacities (worst
// case ~64 messages x 32 placements = 2048 placement objects dominate both
// the printed text size and the cJSON node count) — still cheap against the
// 8 MB PSRAM budget. Kept as two separate PSRAM regions (raw file text vs.
// cJSON's node arena) so the arena's bump allocations can never overwrite
// the text they're still being parsed from.
#define STORAGE_JSON_MAX_BYTES (512 * 1024)
#define JSON_ARENA_BYTES       (2 * 1024 * 1024)

static char    __uninitialized_psram("wilicankit_json_text")  s_json_text[STORAGE_JSON_MAX_BYTES];
static uint8_t __uninitialized_psram("wilicankit_json_arena") s_json_arena[JSON_ARENA_BYTES];
static size_t  s_json_arena_used;

// No-op free: this arena is only ever bulk-reset (json_arena_reset), never
// reclaimed node-by-node — cJSON_Delete() still calls it per node, harmless.
static void json_arena_free(void *ptr) { (void)ptr; }

static void *json_arena_alloc(size_t sz) {
    sz = (sz + 7u) & ~(size_t)7u;   // keep every allocation 8-byte aligned
    if (s_json_arena_used + sz > JSON_ARENA_BYTES) return NULL;
    void *p = &s_json_arena[s_json_arena_used];
    s_json_arena_used += sz;
    return p;
}

static void json_arena_reset(void) { s_json_arena_used = 0; }

static void build_path(char *out, size_t cap, const char *name) {
    snprintf(out, cap, "%s/%s.json", STORAGE_DIR, name);
}

int storage_list_configs(char names[][STORAGE_NAME_MAX], int max_names) {
    DIR dir;
    FILINFO fi;
    int n = 0;
    if (f_opendir(&dir, STORAGE_DIR) != FR_OK) return 0;
    while (n < max_names && f_readdir(&dir, &fi) == FR_OK && fi.fname[0]) {
        if (fi.fattrib & AM_DIR) continue;
        size_t len = strlen(fi.fname);
        if (len > 5 && strcasecmp(fi.fname + len - 5, ".json") == 0) {
            size_t copy_len = len - 5;
            if (copy_len >= STORAGE_NAME_MAX) copy_len = STORAGE_NAME_MAX - 1;
            memcpy(names[n], fi.fname, copy_len);
            names[n][copy_len] = '\0';
            n++;
        }
    }
    f_closedir(&dir);
    return n;
}

bool storage_config_exists(const char *name) {
    static char path[64];
    build_path(path, sizeof path, name);
    FILINFO fi;
    return f_stat(path, &fi) == FR_OK;
}

bool storage_save_config(const char *name) {
    f_mkdir(STORAGE_DIR);   // FR_EXIST is fine, ignore the return value
    static char path[64];
    build_path(path, sizeof path, name);

    json_arena_reset();
    cJSON_Hooks hooks = { .malloc_fn = json_arena_alloc, .free_fn = json_arena_free };
    cJSON_InitHooks(&hooks);

    cJSON *root = storage_build_json(g_signals, g_messages, g_controls);
    if (!root) DIAG("storage: json build failed (arena exhausted?): %s\n", path);

    // Preallocated print (noalloc): writes directly into s_json_text with no
    // internal buffer growth, unlike cJSON_Print — which reallocates its
    // output buffer via the arena as it grows, and since json_arena_free is a
    // no-op every intermediate (smaller) buffer from that growth is orphaned,
    // silently doubling the effective cost and exhausting JSON_ARENA_BYTES.
    bool printed = root && cJSON_PrintPreallocated(root, s_json_text, STORAGE_JSON_MAX_BYTES, 1);
    if (root && !printed) DIAG("storage: json print overflowed %d-byte buffer: %s\n", STORAGE_JSON_MAX_BYTES, path);

    bool ok = false;
    if (printed) {
        FIL f;
        if (f_open(&f, path, FA_CREATE_ALWAYS | FA_WRITE) == FR_OK) {
            UINT written;
            UINT len = (UINT)strlen(s_json_text);
            ok = f_write(&f, s_json_text, len, &written) == FR_OK && written == len;
            f_close(&f);
        } else {
            DIAG("storage: open for write failed: %s\n", path);
        }
    }
    if (root) cJSON_Delete(root);
    cJSON_InitHooks(NULL);   // restore default malloc/free for the rest of the app

    if (!ok) DIAG("storage: write failed: %s\n", path);
    return ok;
}

bool storage_load_config(const char *name) {
    static char path[64];
    build_path(path, sizeof path, name);

    FIL f;
    if (f_open(&f, path, FA_READ) != FR_OK) {
        DIAG("storage: open for read failed: %s\n", path);
        return false;
    }
    FSIZE_t size = f_size(&f);
    if (size == 0 || size >= STORAGE_JSON_MAX_BYTES) {
        DIAG("storage: file empty or too large: %s\n", path);
        f_close(&f);
        return false;
    }

    UINT rd;
    bool ok = f_read(&f, s_json_text, (UINT)size, &rd) == FR_OK && rd == size;
    f_close(&f);
    if (!ok) {
        DIAG("storage: read failed: %s\n", path);
        return false;
    }
    s_json_text[size] = '\0';

    json_arena_reset();
    cJSON_Hooks hooks = { .malloc_fn = json_arena_alloc, .free_fn = json_arena_free };
    cJSON_InitHooks(&hooks);

    cJSON *root = cJSON_ParseWithLength(s_json_text, (size_t)size);

    // Decode into scratch first — a truncated/malformed file must not
    // partially clobber the live tables.
    static can_signal_t  __uninitialized_psram("wilicankit_tmp_signals")  tmp_signals[CAN_MAX_SIGNALS];
    static can_message_t __uninitialized_psram("wilicankit_tmp_messages") tmp_messages[CAN_MAX_MESSAGES];
    static can_control_t __uninitialized_psram("wilicankit_tmp_controls") tmp_controls[CAN_MAX_CONTROLS];
    ok = root && storage_parse_json(root, tmp_signals, tmp_messages, tmp_controls);

    if (root) cJSON_Delete(root);
    cJSON_InitHooks(NULL);

    if (!ok) {
        DIAG("storage: parse failed: %s\n", path);
        return false;
    }
    memcpy(g_signals, tmp_signals, sizeof g_signals);
    memcpy(g_messages, tmp_messages, sizeof g_messages);
    memcpy(g_controls, tmp_controls, sizeof g_controls);
    return true;
}

bool storage_delete_config(const char *name) {
    static char path[64];
    build_path(path, sizeof path, name);
    return f_unlink(path) == FR_OK;
}
