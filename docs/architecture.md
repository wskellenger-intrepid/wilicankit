# wilicankit — architecture & status

Continuity notes for picking this app back up in a fresh session (no chat
history). Read this before making changes. See also `docs/data-model.md`,
`docs/build-notes.md`, and `docs/known-risks.md` in this same folder.

## What this is

A general-purpose, user-configurable CAN device driver/GUI app for the
FreeWili 2 **display** processor (RP2350B). Not tied to any specific
vehicle — signals and messages are defined entirely at runtime through the
touch UI and saved to a USB stick. The first validation target is a 2006
Nissan Xterra instrument cluster at 500 kbit/s, but that's just the first
use case, not a hardcoded assumption anywhere in the code.

## Hard architecture constraints (do not violate these)

1. **No native CAN on the display processor.** The RP2350B display CPU has
   no CAN controller. All CAN TX goes through OneWili commands
   (`libs/onewili`) to the **MAIN CPU** over the FwGUI UART0 link (8 Mbaud).
   `can_link.c` is the *only* file that talks to MAIN, and it only calls
   pre-existing generated `ow_io_canfd_*` functions — this app must never
   add code that runs on or modifies MAIN CPU firmware.
2. **MAIN CPU must be running stock `FW2Main.uf2`** for the OneWili bridge
   to exist. If `can_link_open()` (called from `main.c`) loops forever
   printing `FwGUI link open failed` over RTT, that's the first thing to
   check — see repo `/memories/repo/hardware-bringup-facts.md` for the
   MAIN/DISPLAY wake dependency and flashing order.
3. **500 kbit/s bitrate is assumed pre-configured on MAIN**, not set by this
   app. OneWili has no "set CAN bitrate" command (only a raw register poke,
   `ow_io_canfd_set_can_register`, which we don't have a register map for).
   This is an **unverified risk** — see `docs/known-risks.md`.
4. **Core0 stack is capped at 4 KB** (`PICO_STACK_SIZE=0x1000` in
   CMakeLists.txt) — this is the hard max for `copy_to_ram` apps on this
   board (RP2350's `SCRATCH_Y` bank is only 4 KB total). All UI code uses
   `static` (not stack) buffers for anything non-trivial (snprintf scratch,
   dropdown option strings, etc.) as a result. Keep doing this in new code.
5. **LVGL draw buffers live in PSRAM**, not SRAM (see `lvgl_port.c`) — an
   earlier SRAM-only attempt overflowed `.bss` by ~33 KB. `LV_MEM_SIZE`
   (LVGL's own object heap) is a separate, smaller pool in SRAM
   (`lv_conf.h`, currently 40 KB — trimmed once already to fix a `.bss`
   overflow; see `docs/build-notes.md` before growing it back up).

## Module map

| File | Purpose |
|---|---|
| `main.c` | Board bring-up, `lvgl_port_init()`, `ui_shell_create()`, `can_link_open()`, `usb_store_init()`, main loop. |
| `lvgl_port.h/.c` | LVGL <-> BSP glue: display flush (`st7796_flush_async` + byte-swap), touch indev (`ft6336_poll`), tick source. PSRAM draw buffers. |
| `can_model.h/.c` | Signal/message/placement structs + bit pack/unpack + signal scale/offset conversion. Pure logic, host-testable (`tests/test_can_pack.c`). |
| `can_link.h/.c` | OneWili wrapper: open link, one-shot send, arm/disable periodic. The only file touching MAIN. |
| `app_state.h/.c` | Global `g_signals`/`g_messages`/`g_controls` tables + alloc/free helpers. |
| `ui_common.h/.c` | Shared on-screen keyboard singleton + modal dialog helper, used by every form. |
| `ui_shell.h/.c` | Top-level `lv_menu` sidebar: Monitor / Transmit (Scheduled, One-Shot*, Fuzz*) / Diagnostics* / Data (Signals, Messages, Controls) / Config, root back-button navigation, per-item refresh-on-load. (*placeholder pages, content not yet implemented.) |
| `ui_signals.h/.c` | Signals tab: list + add/edit/delete modal form. |
| `ui_messages.h/.c` | Messages tab: list + add/edit/delete modal form with an inline bit-placement sub-editor. |
| `ui_transmit.h/.c` | Transmit tab: per-message 10/20/100 ms presets + custom ms field, enable switch (arms/disarms OneWili periodic slot), Send Once button. |
| `ui_controls.h/.c` | Controls tab: add sliders/toggles bound to a signal; moving a slider or flipping a toggle updates the signal value and immediately re-sends any enabled message containing it (one-shot TX, does not disturb the periodic schedule). |
| `storage_json.h/.c` | Pure struct<->JSON encode/decode for the signal/message/control tables (cJSON tree building/parsing, no file I/O). Host-testable (`tests/test_storage_json.c`). |
| `storage.h/.c` | Save/load/list/delete named configs as human-readable JSON files on `0:/wilicankit/*.json` (FatFs). Wraps storage_json.h/.c with FatFs I/O and a PSRAM-backed allocator for cJSON (see build-notes.md). Load uses a scratch-then-commit pattern for safety. |
| `ui_config.h/.c` | Config page: New / Save As over an `lv_file_explorer` (LVGL's built-in FatFs driver, `lv_fs_fatfs_init()` in `main.c`) browsing `0:/wilicankit`; tap a file to Load, toolbar button to Delete. |
| `lv_conf.h` | Minimal LVGL v9.3.0 config — only overrides that differ from LVGL's own defaults are listed; everything else falls back to `lv_conf_internal.h`. |

## Status (as of this writing)

All modules implemented and building cleanly:
- `tools/build.ps1` links successfully.
- `tools/test.ps1` / the host CTest `can_pack` target passes (bit pack/unpack
  round trips, both endiannesses, signal scale/offset conversions).
- **Nothing has run on real hardware yet.** No touch/display/OneWili-link
  smoke test has been performed on a physical board. Do that before trusting
  any of the runtime behavior described here.

## Verification checklist for the next hardware session

1. `tools/flash.ps1` + `tools/rtt.ps1` — confirm the screen renders the tab
   bar, touch responds, and `can_link: link up` appears in RTT (retry-loop
   DIAG if MAIN isn't running stock firmware).
2. Create a signal, create a message placing it, enable periodic TX, verify
   with a CAN analyzer: arbitration ID, DLC, byte content, actual period.
3. Verify the assumed 500 kbit/s bitrate against the analyzer's own
   bit-rate detection (see `docs/known-risks.md`, item 1).
4. Move a slider bound to a signal; confirm the live bus frame updates
   within a perceptible delay and `ow_fwgui_dropped_frames()` stays low.
5. Save a config, power-cycle, load it back from the same stick, confirm
   everything (signals, messages, placements, sliders) restores exactly and
   periodic TX resumes as expected.
