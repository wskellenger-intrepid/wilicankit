// apps/wilicankit/storage.h — save/load/list/delete named configurations
// (the full signal/message/control tables) as human-readable JSON on the USB
// thumb drive (FatFs, see bsp/usbhost/usb_store.h). Multiple configs can
// coexist as separate files under STORAGE_DIR. Encode/decode logic lives in
// storage_json.h/.c (pure, host-testable); this layer is just the FatFs glue
// plus a PSRAM-backed allocator for cJSON (see storage.c).
#ifndef WILICANKIT_STORAGE_H
#define WILICANKIT_STORAGE_H
#include <stdbool.h>
#include <stddef.h>

#define STORAGE_DIR       "0:/wilicankit"
#define STORAGE_NAME_MAX  32

// Lists config names (no directory/extension) into `names[0..N)`, each up
// to STORAGE_NAME_MAX bytes, at most `max_names` entries. Returns the
// number found (0 if no stick is mounted or the directory doesn't exist).
int storage_list_configs(char names[][STORAGE_NAME_MAX], int max_names);

// True if "<STORAGE_DIR>/<name>.json" already exists (used to confirm
// overwrite before a Save As over an existing file).
bool storage_config_exists(const char *name);

// Saves the current g_signals/g_messages/g_controls tables (see app_state.h)
// to "<STORAGE_DIR>/<name>.json". Creates STORAGE_DIR if needed.
bool storage_save_config(const char *name);

// Loads "<STORAGE_DIR>/<name>.json", replacing the current
// g_signals/g_messages/g_controls tables. On failure, current state is left
// untouched (validated into a scratch buffer before committing).
bool storage_load_config(const char *name);

// Deletes "<STORAGE_DIR>/<name>.json".
bool storage_delete_config(const char *name);

#endif // WILICANKIT_STORAGE_H
