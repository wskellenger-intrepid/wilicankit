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
