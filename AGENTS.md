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
   FwGUI UART0 link. `can_link.c` is the *only* file allowed to talk to
   MAIN — never add code elsewhere that calls a `wilibsp`/`onewili`
   function, and never invent/stub a MAIN-facing OneWili function that
   doesn't exist upstream. If a needed OneWili function is missing, surface
   it to the user instead of guessing at a wire protocol.
2. **Core0 stack is capped at 4 KB** (`PICO_STACK_SIZE=0x1000`) — use
   `static` buffers for anything non-trivial, not stack.
3. **LVGL draw buffers live in PSRAM**, not SRAM — see `lvgl_port.c` and
   `docs/build-notes.md` before changing `LV_MEM_SIZE` in `lv_conf.h`.

See `docs/architecture.md` for the complete numbered list (MAIN CPU firmware
dependency, CAN bitrate assumption, etc.).

### Stack

- Language: C11, bare-metal (no RTOS).
- Framework(s): Pico SDK 2.3.0, LVGL 9.3.0 (UI), OneWili (CAN bridge to MAIN
  CPU over UART0 FwGUI link), cJSON 1.7.18 (config serialization).
- Package manager: CMake + git submodules (`wilibsp`, `third_party/lvgl`) +
  CMake `FetchContent` for cJSON only.
- Runtime / deployment target: FreeWili 2 display CPU (RP2350B), flashed
  over CMSIS-DAP or drag-and-drop UF2.

### Commands

All run from the repo root (PowerShell):

| Command           | What it does                                                                |
| ------------------ | ---------------------------------------------------------------------------- |
| `tools/build.ps1`  | Configure + build the on-target firmware (RelWithDebInfo). `-Clean` wipes `build/` first. |
| `tools/flash.ps1`  | Program the built firmware over the CMSIS-DAP debug probe via OpenOCD.        |
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

- Calling a `wilibsp`/`onewili` function from anywhere other than
  `can_link.c`, or inventing one that doesn't exist upstream.
- Growing `LV_MEM_SIZE` or moving LVGL draw buffers to SRAM without reading
  `docs/build-notes.md` first (previously caused a `.bss` overflow).
- Using stack buffers for non-trivial data — Core0's stack is a hard 4 KB
  cap.
- Committing directly to `wilibsp`'s or `onewili`'s `origin/master` from
  within this repo's submodule checkout — changes go upstream via PR in
  their own repos.

---

## 11. Project Learnings

Accumulated corrections. When the user corrects an approach, append a
one-line rule here before ending the session. Write it concretely.

- (empty)
