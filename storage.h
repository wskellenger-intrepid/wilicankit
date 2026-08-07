// apps/wilicankit/storage.h — save/load/list/delete named configurations
// (the full signal/message/control tables) as human-readable JSON, on either
// the USB thumb drive (FatFs, see bsp/usbhost/usb_store.h) or the SD card
// (OneWili SDFS, see wilibsp/libs/onewili/include/onewili_sd.h -- the SD card
// is owned by the MAIN CPU, reached over the FwGUI link already opened by
// can_link_open()). Multiple configs can coexist as separate files under the
// active backend's config dir. Encode/decode logic lives in storage_json.h/.c
// (pure, host-testable); this layer is just the FatFs/SDFS glue plus a
// PSRAM-backed allocator for cJSON (see storage.c).
#ifndef WILICANKIT_STORAGE_H
#define WILICANKIT_STORAGE_H
#include <stdbool.h>
#include <stddef.h>

#define STORAGE_DIR       "0:/wilicankit"
#define SD_STORAGE_DIR    "/appdata/wilicankit"
#define STORAGE_NAME_MAX  32

typedef enum { STORAGE_BACKEND_USB, STORAGE_BACKEND_SD } storage_backend_t;

// Selects which backend list/exists/save/load/delete act on. Defaults to
// STORAGE_BACKEND_SD.
void storage_set_backend(storage_backend_t backend);
storage_backend_t storage_get_backend(void);

// True if the SD card is reachable through the MAIN CPU right now (a stat on
// "/appdata"). Blocks on a UART round-trip to MAIN -- call on demand only
// (e.g. Config page opened / backend toggle flipped), never from a per-frame
// poll.
bool storage_sd_available(void);

// Lists full file names (including their ".json" extension, exactly as
// stored) of every "*.json" entry (case-insensitive) into `names[0..N)`,
// each up to STORAGE_NAME_MAX bytes, at most `max_names` entries. Returns
// the number found (0 if the active backend's media isn't available or the
// directory doesn't exist).
int storage_list_configs(char names[][STORAGE_NAME_MAX], int max_names);

// True if the active backend already has a file named exactly `name` (pass
// the full file name, e.g. "foo.json" -- used to confirm overwrite before a
// Save As over an existing file).
bool storage_config_exists(const char *name);

// True if `name` is a non-empty, FAT32-safe file name: fits within
// STORAGE_NAME_MAX, has no path separators/control chars/other characters
// FAT32 forbids in a name, and doesn't end in a space or dot.
bool storage_name_is_valid(const char *name);

// Saves the current g_signals/g_messages/g_controls tables (see app_state.h)
// under the active backend's config dir as the literal file name `name`
// (pass the full file name, e.g. "foo.json" -- this never adds or assumes an
// extension). Creates the dir if needed.
bool storage_save_config(const char *name);

// Loads the file named exactly `name` from the active backend, replacing
// the current g_signals/g_messages/g_controls tables. On failure, current
// state is left untouched (validated into a scratch buffer before
// committing).
bool storage_load_config(const char *name);

// Deletes the file named exactly `name` from the active backend.
bool storage_delete_config(const char *name);

#endif // WILICANKIT_STORAGE_H
