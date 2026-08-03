# wilicankit — build notes

CMake/LVGL/cJSON/memory specifics worth knowing before touching the build.

## LVGL + cJSON via FetchContent

`CMakeLists.txt` pins LVGL to `v9.2.2` and cJSON to `v1.7.18` via
`FetchContent`. These are the repo's **only** network-fetched build
dependencies — every other dependency in this repo is harvested source (see
AGENTS.md). Accepted deliberately for this app; if offline/reproducible
builds ever become a hard requirement, consider vendoring both instead.
cJSON backs `storage_json.c` (config save/load, see `docs/known-risks.md`
and the storage_json row in `architecture.md`) — its own test/utils targets
and strict upstream compiler flags (`-Werror` tuned for host GCC/Clang, not
arm-none-eabi) are disabled via `CACHE ... FORCE` overrides before
`FetchContent_MakeAvailable(cjson)`.

Key CMake variables set **before** `FetchContent_MakeAvailable(lvgl)`:
- `LV_CONF_PATH` → points at `apps/wilicankit/lv_conf.h`.
- `LV_CONF_BUILD_DISABLE_EXAMPLES` / `_DEMOS` / `_THORVG_INTERNAL` → all
  `ON`, to keep the fetched tree lean (we only need the core+widgets lib).

`lv_conf.h` is intentionally minimal: LVGL's `lv_conf_internal.h` supplies a
sane default for every macro NOT defined in `lv_conf.h` (mirroring
`lv_conf_template.h`), so only actual overrides need to be listed. Current
overrides: `LV_COLOR_DEPTH=16`, `LV_USE_STDLIB_MALLOC=LV_STDLIB_BUILTIN`,
`LV_MEM_SIZE=40*1024`, `LV_USE_OS=LV_OS_NONE`, `LV_USE_LOG=0`,
`LV_BUILD_EXAMPLES=0`.

## Pixel format / byte order

Display is RGB565, but the ST7796 driver (`bsp/display/st7796.h`) wants
**big-endian** 16-bit words on the wire, while LVGL renders in native
(little-endian, on this ARM target) RGB565. There is **no**
`LV_COLOR_16_SWAP` macro in LVGL v9 (that was removed after v8) — the fix
is a manual call to `lv_draw_sw_rgb565_swap(px_map, pixel_count)` inside the
`disp_flush_cb` in `lvgl_port.c`, right before handing the buffer to
`st7796_flush_async`. If you ever see color channels look swapped/wrong on
hardware, check that this call is still in place.

## Stack size: hard-capped at 4 KB

Default `PICO_STACK_SIZE` is `0x800` (2 KB) — see `apps/toggleled/main.c`'s
comment. This app overrides it via
`target_compile_definitions(wilicankit PRIVATE PICO_STACK_SIZE=0x1000)`
(4 KB). This works because `pico_crt0`'s `crt0.S` is an `INTERFACE` source
(compiled per-target, not a prebuilt lib) — the override only affects this
app, not others in the repo.

**4 KB (`0x1000`) is the practical maximum**, not an arbitrary choice: for
`copy_to_ram` binaries, the core0 stack (`.stack_dummy`) is placed in the
RP2350's dedicated `SCRATCH_Y` SRAM bank (NOT the main striped 512 KB SRAM
used for `.data`/`.bss`), and `SCRATCH_Y` is only 4 KB total. Requesting
more fails the link with
`section '.stack_dummy' will not fit in region 'SCRATCH_Y'`. Don't try to
raise this further without a custom linker script.

Because of this cap, UI code (`ui_*.c`) consistently uses `static` (not
stack-local) buffers for anything non-trivial: `snprintf` scratch buffers,
dropdown option strings (`rebuild_signal_dropdown` in `ui_messages.c` /
`ui_controls.c`), etc. **Keep doing this in new code** — a stack overflow
here is silent memory corruption, not a clean crash.

## PSRAM vs SRAM budget

- LVGL's **draw buffers** are plain small **SRAM** static arrays
  (`s_buf1`/`s_buf2` in `lvgl_port.c`), NOT PSRAM — measured on-hardware to
  be ~2x slower per redraw out of PSRAM (QSPI per-access latency dominates
  this hot per-pixel path), so they were moved back to SRAM. `psram_init()`
  is still called at the top of `lvgl_port_init()` (mainly to log the
  detected size) even though nothing in `lvgl_port.c` itself claims PSRAM
  today.
- `storage.c`'s cJSON parse tree and raw JSON text buffer DO live in PSRAM
  (`__uninitialized_psram("wilicankit_json_arena"/"wilicankit_json_text")`)
  — cJSON is pointed at a trivial bump allocator over that PSRAM region via
  `cJSON_InitHooks()`, so parsing/building a config never touches the tight
  SRAM budget. See `docs/known-risks.md` for why this mattered.
- LVGL's **object heap** (`LV_MEM_SIZE`, 1 MB) also lives in **PSRAM** via
  `LV_MEM_POOL_ALLOC`/`lv_psram_pool.c` — moved out of SRAM entirely after
  repeated `LV_MEM_SIZE`-exhaustion freezes made clear that rationing it in
  SRAM (radius=0 style workarounds, shaving a few KB at a time) was treating
  the symptom, not the cause; see repo memory `wilicankit-lvgl-mem-crash.md`.
- `g_signals`/`g_messages`/`g_controls` (`app_state.c`) and `storage.c`'s
  load-into-scratch-then-commit copies of them are ALSO in PSRAM
  (`__uninitialized_psram`), for the same reason — the `CAN_MAX_*` table
  capacities (`can_model.h`) no longer need to be rationed against the
  tight SRAM budget, so they were bumped to comfortably generous sizes
  (200 signals / 64 messages / 32 placements-per-msg / 200 controls)
  instead of staying small to fit `.bss`.

## Known IDE noise

`get_errors` (VS Code's C/C++ IntelliSense) has shown a pile of
"identifier X is undefined" false positives on files in this app — these
were traced to a **stale IntelliSense cache**, not real errors (the actual
`arm-none-eabi-gcc` toolchain build, `fw build wilicankit`, is authoritative
and passed at the same time IntelliSense was complaining). If this recurs,
reload/reset the C/C++ IntelliSense database or confirm `compileCommands`
points at `build/compile_commands.json` — don't "fix" code based on these
without first confirming with a real build.

Separately: earlier in development, a **VS Code extension** (not this
tool) was auto-corrupting header guards on save (inserting a duplicate
`#ifndef`/`#define` pair, sometimes with a matching stray `#endif`,
sometimes leaving an orphaned one). The user identified and disabled the
extension. If a header guard duplication bug shows up again, suspect an
installed extension before assuming it's a tool or code issue — see
`/memories/tool-notes.md` (user-scoped agent memory, not in this repo) for
the full incident notes.
