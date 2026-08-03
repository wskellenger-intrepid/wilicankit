# wilicankit

General-purpose, user-configurable CAN device driver/GUI app for the
FreeWili 2 display processor. Not tied to any specific vehicle or device —
signals (name, bit length, scale, offset, units) and messages (CAN ID,
length, signal placements) are defined entirely at runtime through the touch
UI and saved to a USB stick (FatFs). The first validation target is a 2006
Nissan Xterra instrument cluster at 500 kbit/s, but the app can drive any
CAN device built from user-defined signals/messages.

CAN TX goes through OneWili commands to the MAIN CPU over the FwGUI link
(UART0, 8 Mbaud) — the RP2350B display processor has no native CAN
controller, and this app never touches MAIN CPU firmware, only the existing
`onewili`/`onewili_fwgui` API (see AGENTS.md).

GUI is built with LVGL (fetched via CMake FetchContent — see CMakeLists.txt).

## Status

Fully implemented (Signals/Messages/Transmit/Controls data screens plus a
Config screen, reached through an `lv_menu` sidebar rather than a flat tab
bar — see `docs/architecture.md`'s module map; Monitor/One-Shot Transmit/
Fuzz Transmit/Diagnostics are sidebar placeholders, content not yet built),
CAN pack/unpack, OneWili TX, USB config save/load, building cleanly.
**Nothing has run on real hardware yet** — see `docs/known-risks.md`.

## Docs

Start a new session by reading `docs/architecture.md` first, then
`docs/data-model.md` (bit-packing convention — read before touching
`can_model.c`), `docs/build-notes.md` (CMake/LVGL/stack/PSRAM specifics),
and `docs/known-risks.md` (unverified assumptions to check on hardware).

## Building

Needs the Pico SDK and ARM GCC under `~/.pico-sdk` (the layout the official
VS Code extension installs), plus CMake and Ninja.

```
git clone --recurse-submodules https://github.com/freewili/wilicankit.git
cd wilicankit
powershell -File tools/build.ps1          # -> build/wilicankit.uf2
powershell -File tools/flash.ps1          # program + verify + reset over a CMSIS-DAP probe
powershell -File tools/rtt.ps1            # stream SEGGER RTT diagnostics
```

Add `-Clean` to `build.ps1` for a from-scratch build. Drag-and-drop also
works: hold BOOTSEL, then copy `build/wilicankit.uf2` to the mass-storage
device.

### Host tests

```
powershell -File tools/test.ps1           # configure, build, ctest
```

Needs a host GCC (MSYS2 mingw64 works). Covers `can_model.c`'s bit
pack/unpack (`test/test_can_pack.c`) and `storage_json.c`'s JSON encode/
decode (`test/test_storage_json.c`), no hardware required.

## Credits

Built on [wilibsp](https://github.com/freewili/wilibsp) (git submodule).
UI by [LVGL](https://lvgl.io/) (git submodule, pinned to v9.3.0). Config
save/load uses [cJSON](https://github.com/DaveGamble/cJSON) (fetched via
CMake `FetchContent`, pinned to v1.7.18).

### Third-party components

wilicankit's own code is MIT (see `LICENSE`). These vendored components
keep their own terms:

| Component | Where | License |
|---|---|---|
| wilibsp | `wilibsp` submodule | see that repository |
| LVGL | `third_party/lvgl` submodule | MIT |
| cJSON | fetched at configure time | MIT |
| Pico SDK import shim | `pico_sdk_import.cmake` | BSD-3-Clause — © Raspberry Pi Ltd |
