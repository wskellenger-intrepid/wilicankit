// apps/wilicankit/storage.c — see storage.h. FatFs (USB) / OneWili SDFS (SD)
// I/O + a PSRAM-backed bump allocator for cJSON; struct<->JSON encode/decode
// itself lives in storage_json.c.
#include "storage.h"
#include "storage_json.h"
#include "app_state.h"
#include "ff.h"
#include "onewili_sd.h"
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

static storage_backend_t s_backend = STORAGE_BACKEND_SD;

void storage_set_backend(storage_backend_t backend) { s_backend = backend; }
storage_backend_t storage_get_backend(void) { return s_backend; }

bool storage_sd_available(void) {
    bool is_dir = false;
    uint32_t size = 0;
    return ow_sd_stat(NULL, "/appdata", &is_dir, &size) == OW_OK && is_dir;
}

static void build_path(char *out, size_t cap, const char *name) {
    const char *dir = (s_backend == STORAGE_BACKEND_SD) ? SD_STORAGE_DIR : STORAGE_DIR;
    size_t len = strlen(name);
    bool has_ext = len > 5 && strcasecmp(name + len - 5, ".json") == 0;
    snprintf(out, cap, has_ext ? "%s/%s" : "%s/%s.json", dir, name);
}

bool storage_name_is_valid(const char *name) {
    size_t len = strlen(name);
    if (len == 0 || len >= STORAGE_NAME_MAX) return false;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)name[i];
        if (c < 0x20 || strchr("\\/:*?\"<>|", (char)c)) return false;   // FAT32-illegal/control chars
    }
    if (name[len - 1] == ' ' || name[len - 1] == '.') return false;   // FAT32 silently strips these, inviting mismatches
    return true;
}

// ow_sd_list callback: collect ".json" entries the same way the FatFs loop
// below does, ignoring directories.
typedef struct {
    char (*names)[STORAGE_NAME_MAX];
    int  max_names;
    int  count;
} sd_list_ctx_t;

static void sd_list_cb(const char *name, bool is_dir, uint32_t size, void *user) {
    (void)size;
    sd_list_ctx_t *ctx = (sd_list_ctx_t *)user;
    if (is_dir || ctx->count >= ctx->max_names) return;
    size_t len = strlen(name);
    if (len > 5 && strcasecmp(name + len - 5, ".json") == 0) {
        size_t copy_len = len - 5;
        if (copy_len >= STORAGE_NAME_MAX) copy_len = STORAGE_NAME_MAX - 1;
        memcpy(ctx->names[ctx->count], name, copy_len);
        ctx->names[ctx->count][copy_len] = '\0';
        ctx->count++;
    }
}

int storage_list_configs(char names[][STORAGE_NAME_MAX], int max_names) {
    if (s_backend == STORAGE_BACKEND_SD) {
        sd_list_ctx_t ctx = { names, max_names, 0 };
        if (ow_sd_list(NULL, SD_STORAGE_DIR, sd_list_cb, &ctx) != OW_OK) return 0;
        return ctx.count;
    }
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
    if (s_backend == STORAGE_BACKEND_SD) {
        bool is_dir = false;
        uint32_t size = 0;
        return ow_sd_stat(NULL, path, &is_dir, &size) == OW_OK && !is_dir;
    }
    FILINFO fi;
    return f_stat(path, &fi) == FR_OK;
}

bool storage_save_config(const char *name) {
    if (!storage_name_is_valid(name)) {
        DIAG("storage: invalid config name: %s\n", name);
        return false;
    }
    static char path[64];
    build_path(path, sizeof path, name);
    if (s_backend == STORAGE_BACKEND_SD) {
        ow_sd_mkdir(NULL, "/appdata");     // FR_EXIST-equivalent is fine, ignore
        ow_sd_mkdir(NULL, SD_STORAGE_DIR);
    } else {
        f_mkdir(STORAGE_DIR);   // FR_EXIST is fine, ignore the return value
    }

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
        size_t len = strlen(s_json_text);
        if (s_backend == STORAGE_BACKEND_SD) {
            // ow_sd_put_mem sends every SDFS_MAX_PAYLOAD chunk back-to-back with
            // no pacing; confirmed on hardware that MAIN's SDFS server silently
            // drops everything past the first chunk, truncating the file to
            // exactly SDFS_MAX_PAYLOAD bytes. Writing one chunk per ow_sd_write
            // call gets an ack (sdfs_hwrite's per-call backpressure point)
            // before the next chunk is sent, so MAIN can keep up.
            ow_sd_file f;
            ok = ow_sd_open(NULL, &f, path, OW_SD_WRITE) == OW_OK;
            for (size_t off = 0; ok && off < len; off += (SDFS_MAX_PAYLOAD - 1)) {
                size_t n = len - off;
                if (n > SDFS_MAX_PAYLOAD - 1) n = SDFS_MAX_PAYLOAD - 1;
                ok = ow_sd_write(&f, s_json_text + off, n) == OW_OK;
            }
            if (f.is_open) ok = (ow_sd_close(&f) == OW_OK) && ok;
            if (!ok) {
                DIAG("storage: sd write failed: %s\n", path);
                ow_sd_remove(NULL, path);   // sdfslib writes have no rollback -- clean up the truncated file ourselves
            }
        } else {
            FIL f;
            if (f_open(&f, path, FA_CREATE_ALWAYS | FA_WRITE) == FR_OK) {
                UINT written;
                ok = f_write(&f, s_json_text, (UINT)len, &written) == FR_OK && written == len;
                f_close(&f);
            } else {
                DIAG("storage: open for write failed: %s\n", path);
            }
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

    size_t size = 0;
    if (s_backend == STORAGE_BACKEND_SD) {
        if (ow_sd_get_mem(NULL, path, s_json_text, STORAGE_JSON_MAX_BYTES - 1, &size) != OW_OK || size == 0) {
            DIAG("storage: sd read failed: %s\n", path);
            return false;
        }
    } else {
        FIL f;
        if (f_open(&f, path, FA_READ) != FR_OK) {
            DIAG("storage: open for read failed: %s\n", path);
            return false;
        }
        FSIZE_t fsize = f_size(&f);
        if (fsize == 0 || fsize >= STORAGE_JSON_MAX_BYTES) {
            DIAG("storage: file empty or too large: %s\n", path);
            f_close(&f);
            return false;
        }
        UINT rd;
        bool read_ok = f_read(&f, s_json_text, (UINT)fsize, &rd) == FR_OK && rd == fsize;
        f_close(&f);
        if (!read_ok) {
            DIAG("storage: read failed: %s\n", path);
            return false;
        }
        size = fsize;
    }
    s_json_text[size] = '\0';

    json_arena_reset();
    cJSON_Hooks hooks = { .malloc_fn = json_arena_alloc, .free_fn = json_arena_free };
    cJSON_InitHooks(&hooks);

    cJSON *root = cJSON_ParseWithLength(s_json_text, size);

    // Decode into scratch first — a truncated/malformed file must not
    // partially clobber the live tables.
    static can_signal_t  __uninitialized_psram("wilicankit_tmp_signals")  tmp_signals[CAN_MAX_SIGNALS];
    static can_message_t __uninitialized_psram("wilicankit_tmp_messages") tmp_messages[CAN_MAX_MESSAGES];
    static can_control_t __uninitialized_psram("wilicankit_tmp_controls") tmp_controls[CAN_MAX_CONTROLS];
    bool ok = root && storage_parse_json(root, tmp_signals, tmp_messages, tmp_controls);

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
    if (s_backend == STORAGE_BACKEND_SD) return ow_sd_remove(NULL, path) == OW_OK;
    return f_unlink(path) == FR_OK;
}
