# AGENTS.md

Drop-in operating instructions for coding agents working in `wilicankit`.
Read this before every task. Working code only. Finish the job. Plausibility
is not correctness.

This file follows the [AGENTS.md](https://agents.md) open standard. Claude
Code, Codex, Cursor, Windsurf, Copilot, Aider read it natively.

---

## 0. Non-negotiables

These override everything else in this file when in conflict:

1. **No flattery, no filler.** Start with the answer or the action.
2. **Disagree when you disagree.** Agreeing with a wrong premise to be polite
   is the worst failure mode.
3. **Never fabricate.** Not file paths, not commit hashes, not API names, not
   test results, not library functions. If you don't know, read the file, run
   the command, or say so.
4. **Stop when confused.** Two plausible interpretations → ask, don't pick
   silently.
5. **Touch only what you must.** Every changed line traces to the user's
   request. No drive-by refactors or "while I was in there" cleanups.

---

## 1. Before writing code

- State your plan in one or two sentences before editing. Non-trivial tasks
  get a numbered list of steps with a verification check for each.
- Read the files you will touch, and the files that call them.
- Match existing patterns in the codebase (see Section 10) even if you'd do
  it differently in a greenfield repo.
- Surface assumptions out loud instead of burying them in the diff.
- If two approaches exist, present both with tradeoffs, unless the task is
  trivial (typo, rename, log line).

## 2. Writing code: simplicity first

- No features beyond what was asked. No speculative abstractions or
  configurability that wasn't requested.
- No error handling for scenarios that can't happen on this hardware.
- Bias toward deleting code over adding it.

## 3. Surgical changes

- Don't "improve" adjacent code, comments, formatting, or imports outside
  the task.
- Don't delete pre-existing dead code unless asked — mention it instead.
- Do clean up orphans your own edit created (unused includes, now-dead
  helpers).
- Match this project's style exactly: indentation, naming, file layout.

## 4. Goal-driven execution

Rewrite vague asks into verifiable goals before starting:
- "Fix the bug" → reproduce it (RTT log, host test, or build error), fix the
  cause, confirm the symptom is gone.
- "Add a signal field" → confirm `storage_json.c` round-trips it (host test)
  and the UI form validates it.

For every task: state success criteria, verify (build/test/flash as
applicable), read the actual output, don't claim success from a
plausible-looking diff alone.

## 5. Tool use and verification

- Run `tools/build.ps1` / `tools/test.ps1` rather than guessing whether code
  compiles or a bit-pack round-trips.
- Root-cause build and link errors; don't stub around them. If a needed
  `wilibsp`/OneWili function doesn't exist upstream, say so — don't invent
  one (see Section 10, Forbidden).
- Read full RTT logs and build errors, not the first few lines.

## 6. Session hygiene

- After two failed corrections on the same issue, stop, summarize what was
  learned, and ask for a sharper prompt rather than continuing to thrash.
- Descriptive commit messages (subject <72 chars, body explains why). No
  "update file" / "fix bug" commits.

## 7. Communication style

- Direct, concise, no ceremonial openers or closings.
- Two or three short paragraphs unless asked for depth.
- No excessive bullets or headers in chat responses; prose over structure
  for short answers.

## 8. When to ask, when to proceed

**Ask before proceeding when:**
- The request has two plausible interpretations that materially change the
  output.
- The change touches a hard architecture constraint (Section 10) or a
  shared/upstream repo (`wilibsp`, `onewili`).
- A credential, secret, or a push/publish action is involved.

**Proceed without asking when:**
- The task is trivial and reversible.
- The ambiguity is resolved by reading the code or running a command.
- The user already answered the question once this session.

## 9. Self-improvement loop

This file is living. After a session where the agent did something wrong:
add a concrete rule to Section 11 ("Always use X for Y", not "be careful
with Y"). If a rule already covers it, tighten that rule instead of adding a
new one. Prune rules that no longer apply.

---

## 10. Project context

### What this is

`wilicankit` is a general-purpose, user-configurable CAN device driver/GUI
app for the FreeWili 2 **display** processor (RP2350B). Signals, messages,
and UI controls are defined entirely at runtime through the touch UI and
saved to a USB stick as JSON — nothing is hardcoded to a specific vehicle.
See `docs/architecture.md` (hard constraints, module map), `docs/data-model.md`,
`docs/build-notes.md`, and `docs/known-risks.md` for full detail.

This repo is standalone: `wilibsp` (board support: display/touch/LED
drivers, the OneWili API) is a git submodule, not a parent repo. Clone with
`git clone --recurse-submodules`; if you forget, run
`git submodule update --init --recursive`.

### Hard architecture constraints (do not violate these)

1. **No native CAN on the display processor.** All CAN TX goes through
   OneWili commands (`wilibsp/libs/onewili`) to the **MAIN CPU** over the
   FwGUI UART0 link. `ow_link.c` owns the single `ow_device` (there is one
   link per board) and `main()` hands the pointer to `can_link.c`, the only
   module allowed to talk to MAIN. Never add MAIN-facing OneWili calls
   outside those two files, and never invent/stub a MAIN-facing OneWili
   function that doesn't exist upstream. If a needed OneWili function is
   missing, surface it to the user instead of guessing at a wire protocol.
   Note the `ow_gui_*` family (wire `g\...`) is **not** usable from this app
   at all: those are host→MAIN→display GUI commands, and wilicankit *is* the
   display app. The board's RGB LEDs are handled by cutting their power zone
   instead — see `device_leds.c`.
2. **Core0 stack is capped at 4 KB** (`PICO_STACK_SIZE=0x1000`) — use
   `static` buffers for anything non-trivial, not stack.
3. **The app is PSRAM-resident, built with wilibsp's native `fw2_psram_app()`
   contract macro** (`CMakeLists.txt`), not a hand-rolled `no_flash` build.
   It links to run at `0x11000000` (`.text`/`.rodata`/`.data` all in PSRAM,
   via `wilibsp/bsp/app/psram_link/`), is loaded from the SD card's `/apps`
   by MAIN's PSRAM app loader, and must never write display flash. The UF2
   is generated/validated by wilibsp's own `tools/make_app_uf2.py`/
   `check_app_uf2.py`, not picotool (picotool rejects a PSRAM entry point).
   Don't add a custom linker override or UF2 step here without reading
   `docs/build-notes.md` — the previous hand-rolled `linker_overrides/` +
   `tools/elf2uf2_psram.py` were removed in favor of the upstream mechanism.
4. **The PSRAM bootstrap (clock/QMI retime, `.data`/`.bss` init) lives in
   wilibsp, not `main.c`.** `bsp/app/psram_bootstrap.c`/`psram_startup.S`
   (pulled in by `fw2_psram_app()`) run from a dedicated `.sram_bootstrap`
   SRAM section — along with the SDK's `clocks.c.obj`/`psram.c.obj` — so the
   entire clk_sys raise + QMI re-time executes with the CPU fetching from
   real SRAM, never from the PSRAM window being reconfigured, before jumping
   into this app's PSRAM-resident `main()`. `main()` does **not** call
   `board_init()` (a PSRAM app inherits its effect already applied) and must
   not reintroduce a hand-rolled clock/QMI retime. Filed upstream as
   `freewili/wilibsp` #16, #17, #18 (superseded — do not resurrect them).
5. **`clk_sys` is 250 MHz** (`BOARD_SYS_CLOCK_KHZ`, `wilibsp/bsp/platform/board.h`),
   set by wilibsp's own `board_init_psram()` inside the SRAM bootstrap above.
   This app has no QMI timing math of its own to maintain any more; if
   `clk_sys` or PSRAM timing ever needs to change, that change belongs in
   `wilibsp`, not here.

See `docs/architecture.md` for the complete numbered list (MAIN CPU firmware
dependency, CAN bitrate assumption, etc.).

### Stack

- Language: C11, bare-metal (no RTOS).
- Framework(s): Pico SDK 2.3.0, LVGL 9.3.0 (UI), OneWili (CAN bridge to MAIN
  CPU over UART0 FwGUI link), cJSON 1.7.18 (config serialization).
- Package manager: CMake + git submodules (`wilibsp`, `third_party/lvgl`) +
  CMake `FetchContent` for cJSON only.
- Runtime / deployment target: FreeWili 2 display CPU (RP2350B), deployed as
  a PSRAM app to the SD card's `/apps` folder.

### Commands

All run from the repo root (PowerShell):

| Command           | What it does                                                                |
| ------------------ | ---------------------------------------------------------------------------- |
| `tools/build.ps1`  | Configure + build the on-target firmware (RelWithDebInfo). `-Clean` wipes `build/` first — required after *removing* a source file, not just adding one. |
| `tools/deploy-sd.ps1` | **The only supported way to deploy this app.** Copies the UF2 to the SD card's `/apps` through MAIN's serial CLI. Pass `-ComPort` — auto-detect is unreliable on a multi-device bench. |
| `tools/flash.ps1`  | Programs the DISPLAY RP2350 directly over SWD. **Never use this for wilicankit** — it overwrites the bootloader that loads apps from SD. |
| `tools/rtt.ps1`    | Stream SEGGER RTT diagnostics from the target.                               |
| `tools/test.ps1`   | Build + run the standalone host CTest tree in `test/` (`can_pack`, `storage_json`). Requires MSYS2 mingw64 gcc. |

Pico SDK 2.3.0 / arm-none-eabi-gcc / picotool are pinned under
`~/.pico-sdk` (Windows: `$env:USERPROFILE/.pico-sdk`); `tools/build.ps1`
points at these explicitly. No lint/typecheck tooling is configured.

### Layout

- Source lives in the repo root as flat C files (`main.c`, `can_link.c`,
  `can_model.c`, `app_state.c`, `storage.c`/`storage_json.c`,
  `lvgl_port.c`, `ui_*.c`).
- Tests live in `test/` (`test_can_pack.c`, `test_storage_json.c`,
  `test_util.h`).
- Do not modify `wilibsp/` or `third_party/lvgl` in place — they're git
  submodules; changes belong upstream in their own repos. `build/` is
  generated, never hand-edited.

### Conventions specific to this repo

- Host-testable modules (`can_model.c`, `storage_json.c`) must stay pure C
  logic with no `wilibsp`/Pico SDK includes, guarded by `#ifndef HOST_TEST`
  where a hardware-only include is otherwise needed.
- `wilibsp` and `third_party/lvgl` (pinned to `v9.3.0`) are git submodules.
  `wilibsp` has its own nested submodule (`libs/onewili`).
- cJSON is fetched via CMake `FetchContent` (not a submodule) — intentional,
  kept separate from the submodule set above.
- Testing pattern: custom `test_util.h` macros (`ASSERT_EQ`, `ASSERT_TRUE`,
  `ASSERT_NEAR`, `CHECK`), run via CTest — no external framework.

### Forbidden

- Calling a MAIN-facing `onewili` function from anywhere other than
  `ow_link.c` or `can_link.c`, or inventing one that doesn't exist upstream.
- Running `tools/flash.ps1` against this app — it destroys the display
  bootloader. Deploy with `tools/deploy-sd.ps1`.
- Calling `set_sys_clock_khz()`, `board_init_clk()`, `board_init()`, or
  `psram_reinitialize()` from `main.c` — the PSRAM bootstrap in `wilibsp`
  already did this from SRAM before `main()` runs; see hard constraints 4-5.
- Moving the LVGL draw buffers (`s_buf1`/`s_buf2` in `lvgl_port.c`) or
  growing `LV_MEM_SIZE` without reading `docs/build-notes.md` first. These
  are two different things: the draw buffers are deliberately in **SRAM**,
  while `LV_MEM_SIZE` is the LVGL heap, placed in PSRAM by `lv_psram_pool.c`.
- Using stack buffers for non-trivial data — Core0's stack is a hard 4 KB
  cap.
- Committing directly to `wilibsp`'s or `onewili`'s `origin/master` from
  within this repo's submodule checkout — changes go upstream via PR in
  their own repos.

---

## 11. Project Learnings

Accumulated corrections. When the user corrects an approach, append a
one-line rule here before ending the session. Write it concretely.

- Deploy with `tools/deploy-sd.ps1 -ComPort <port>`, never `tools/flash.ps1`.
- Write terminal commands on a single line using `;` separators — multi-line
  PowerShell gets mangled in the agent terminal.
- When the board hangs before `main()` prints anything, halt over SWD and read
  CFSR/HFSR/VTOR instead of reaching for RTT; the lockup precedes any output.
  `openocd -f wilibsp/tools/openocd/freewili2.cfg -c init -c "targets
  rp2350.dap.core0" -c halt -c "mdw 0xE000ED28 5" -c shutdown`. `pc=0xEFFFFFFE`
  means LOCKUP; resolve faulting addresses with `arm-none-eabi-addr2line`.
- An SDK `__weak` function (e.g. `runtime_init_early_resets`) can be replaced
  by a strong definition in app code, with no CMake changes needed.
- App SRAM ends at `0x20070000` (wilibsp's `__StackTop`; the display loader
  owns SRAM above it) and the 4 KB core0 stack sits just below that. The link
  does NOT fail if `.bss` runs through the stack. After any submodule bump or
  new static buffer, check `arm-none-eabi-nm build/wilicankit.elf | grep
  __bss_end__` is below `0x2006f000` (minus the 2 KB heap). Overlap shows up
  as a LOCKUP with ASCII OneWili response text in the stacked frame.
- Never seed a picpwr awake mask from a single status frame — require two
  agreeing frames. Echoing back one under-reported snapshot switches off every
  rail it failed to report (this cut the SD card rail once).
