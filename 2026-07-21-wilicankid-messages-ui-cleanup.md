# wilicankit — Messages tab UI cleanup (message name, modal split, scrollbar fix, frame view)

**Goal:** Make the Messages tab's Add/Edit Message flow usable on the 480x320
touchscreen: a named message, an uncramped signal-placement editor, no stray
per-row scrollbars, and an assessment of a visual bit-grid "frame view" of
the message (à la a DBC/canlab bit map).

**App:** `apps/wilicankit` (LVGL v9.2.2 GUI app, RP2350B DISPLAY chip,
`copy_to_ram`, screen 480x320, `ST7796`). Relevant files live under
`apps/wilicankit/` (`ui_messages.c`, `ui_signals.c`, `ui_controls.c`,
`ui_transmit.c`, `ui_common.c`, `can_model.h`, `storage.c`, `lv_conf.h`).

**Status:** Round 1 (message name field + Details/Signals modal split) is
**DONE** — implemented, built, and flashed to hardware. Round 2 (this doc's
"Phase A/B/C") is **planned but not yet implemented** — captured here so a
fresh session/agent has full context without needing prior chat history.

---

## Round 1 — DONE (for context only, do not redo)

- `can_model.h`: added `CAN_MESSAGE_NAME_MAX` (24) and a `name` field on
  `can_message_t`.
- `storage.c`: bumped `STORAGE_VERSION` 1→2 (old `.cfg` saves simply fail to
  load now — no migration was written, by design).
- `docs/data-model.md` (in `apps/wilicankit/docs/`): documented the new
  field/limit.
- `ui_messages.c`: split the single cramped "Add/Edit Message" modal into
  two: a small **Details modal** (name, CAN ID, extended, DLC, channel,
  Delete/Cancel/Save, plus a **"Manage Signals (N)"** button) and a larger
  **Signals modal** (opened from that button) holding just the placement
  list (`flex_grow`) and the add-signal row (dropdown/start-bit/endian/+),
  with its own Close button that refreshes the count label on the Details
  modal underneath.
- `fmt_msg_row()` (message list rows) and `ui_transmit.c`'s summary line
  both now show the message name.
- `lv_conf.h`: `LV_MEM_SIZE` trimmed 40 KB → 39 KB — the new `name[24]`
  field, multiplied by `CAN_MAX_MESSAGES` (8) and then DOUBLED again by
  `storage.c`'s load-into-scratch-then-commit pattern, was on its own
  enough to overflow the `copy_to_ram` RAM region by ~500 bytes at 40 KB.
  Documented in `apps/wilicankit/docs/build-notes.md`.
- Verified: `ninja -C build wilicankit` links clean; flashed to the DISPLAY
  chip via `python tools/fw.py flash wilicankit` successfully.

User feedback after seeing Round 1 on hardware → this doc (Round 2):
1. The "Manage Signals" modal (list + add-row) is still too cramped.
2. Many views show a vertical scrollbar next to controls that shouldn't
   scroll at all (e.g. next to the Close button, and next to every field
   row in the Edit Message modal) — these should be a simple, non-scrolling
   vertical layout.
3. Interest in a visual "frame view" of the message showing signal bit
   positions (like a DBC/canlab bit-map), example given as ASCII art with
   one letter per signal marking which bits it occupies across the frame's
   bytes, plus a legend line per signal (`[G] BattTrac_I_Actl, ... SB:8,
   Len:15, ...`).

---

## Root cause of the scrollbar bug (confirmed against vendored LVGL source)

Checked against the fetched LVGL v9.2.2 tree at
`build/_deps/lvgl-src/src/...` (this build dir is git-ignored but present
locally after any successful configure):

- `lv_textarea_set_one_line(ta, true)` sets the textarea's own height to
  `LV_SIZE_CONTENT` (`widgets/textarea/lv_textarea.c`, inside
  `lv_textarea_set_one_line`). Its rendered height ends up being
  font-line-height + the active theme's textarea vertical padding, which
  is commonly **more** than the hardcoded `34`px row height used by
  `add_labeled_ta()` / `add_labeled_switch()` in `ui_messages.c` and
  `ui_signals.c` (`lv_obj_set_size(row, LV_PCT(100), 34)`).
- Base `lv_obj` (used for every plain `lv_obj_create(parent)` row/container
  in this codebase) has `LV_OBJ_FLAG_SCROLLABLE` **on** by default. When a
  child is taller than its fixed-height parent row, LVGL renders an
  internal vertical scrollbar for that row instead of clipping — this is
  the literal, reproducible cause of "scrollbar next to every control".
- `lv_button`'s default height is also `LV_SIZE_CONTENT`
  (`widgets/button/lv_button.c`) — lower risk than textareas, but the same
  class of bug can hit `btn_row` / `close_row` if theme button padding
  pushes a button's natural height past its container's fixed 34/40px.
- `lv_switch`'s default height is `4*LV_DPI_DEF/17` ≈ 30px (`LV_DPI_DEF` is
  130 by default, not overridden in `lv_conf.h`) — fits comfortably under
  the 34px rows, so lower risk, but worth fixing defensively for
  consistency.
- `ui_transmit.c`'s rows/cards **already** use `LV_SIZE_CONTENT` heights
  (not hardcoded pixels) — this file is likely **not** actually affected by
  the bug; don't spend time on it beyond a quick defensive pass if easy.
- The bug is concentrated in: `ui_messages.c` (`add_labeled_ta`,
  `add_labeled_switch`, `btn_row`, `add_row`, `close_row`, and
  `open_message_form`'s `signals_btn`), `ui_signals.c` (identical
  duplicated `add_labeled_ta`/`add_labeled_switch` + its own `btn_row`),
  and `ui_controls.c` (`open_add_slider_form`'s `btn_row`).

---

## Correct fix (revised): pure flex layout, no fixed heights, no flag removal

Revised per user feedback — the earlier draft of this plan treated
"remove `LV_OBJ_FLAG_SCROLLABLE`" as part of the fix. **It isn't needed
and should not be added.** The actual, intended pattern for every
window/modal in this app is simply: create the panel → put a flex layout
on it → insert child controls that size themselves via `LV_SIZE_CONTENT`
/ `lv_obj_set_flex_grow`. Nothing else. Specifically:

- **No hand-picked fixed pixel heights** for rows *or* modal panels
  (`lv_obj_set_size(row, LV_PCT(100), 34)`-style code is the actual bug —
  not a missing flag). A flex container sized to `LV_SIZE_CONTENT` grows
  to exactly fit its children, so a child can never end up "taller than
  its parent" in the first place.
- **No disabling of `LV_OBJ_FLAG_SCROLLABLE` anywhere**, including as a
  "defensive/safety net" step. It's unnecessary once sizing is correct —
  the overflow condition that triggers LVGL's internal scrollbar simply
  can't occur when the parent auto-fits its content — and adding it
  everywhere as a blanket patch was papering over the real issue instead
  of fixing it.
- The **only** legitimate use of a fixed height in this whole flow is a
  modal panel that hosts an intentionally-scrolling `lv_list`
  (`s_placement_list` today) — that list needs a bounded area to grow
  into via `lv_obj_set_flex_grow(list, 1)` inside a fixed-height parent.
  That's a real, deliberate exception (the list is *supposed* to scroll),
  not a workaround, and its container still doesn't need the
  `SCROLLABLE` flag touched.

---

## Decisions already made (do not re-ask the user these)

- **Scrollbar fix scope:** fix in place, per-file, by correcting sizing
  (switch fixed pixel heights to `LV_SIZE_CONTENT` and let the flex
  layout do the work) — **not** by removing `LV_OBJ_FLAG_SCROLLABLE`
  anywhere (see revised root-cause section above). **No** shared
  `ui_common.c` row-building helper/refactor — user explicitly chose this
  over consolidating the duplicated `add_labeled_ta`/`add_labeled_switch`
  code. (This doesn't block folding `LV_FLEX_FLOW_COLUMN` into the
  already-shared `ui_common_open_modal()` itself, below — that's existing
  shared infra, not a new row-building helper.)
- **Manage Signals redesign:** drill-down. Move the always-visible add-row
  (dropdown/start-bit/endian/+) out of the Signals modal into its own small
  3rd-level "**+ Add Signal**" modal, so the Signals modal's placement list
  gets almost the full modal height.
- **Bit-grid "frame view":** feasibility write-up only for this round —
  **do not implement it yet**. When it does get built, the user wants
  **`lv_canvas`** (not `lv_table`, not a grid of individual `lv_obj`
  cells — those were the two options offered; the user overrode both),
  specifically because CAN-FD frames can be up to 64 bytes and the
  renderer should be able to draw multiple 8-byte rows, not just one.

---

## Phase A — Fix layout: content-sized rows and panels, no fixed heights, no flag removal

For every pure-layout row/container below (explicitly **not** the
`lv_list`s used for the message/signal/placement lists, which are supposed
to scroll), do this **one** thing:

1. Change from a hardcoded pixel height (e.g.
   `lv_obj_set_size(row, LV_PCT(100), 34)`) to
   `lv_obj_set_width(x, LV_PCT(100)); lv_obj_set_height(x, LV_SIZE_CONTENT);`
   so the row auto-fits its tallest child instead of guessing a px number.

That's it — do **not** also add
`lv_obj_remove_flag(x, LV_OBJ_FLAG_SCROLLABLE)`. It isn't needed: once a
container auto-sizes to its content it can never be smaller than that
content, so the overflow condition that produces an internal scrollbar
can't occur. Leaving `SCROLLABLE` alone (the LVGL default) is correct and
simpler.

Apply to:

- [ ] `apps/wilicankit/ui_common.c` — `ui_common_open_modal(w, h)`: fold
  `lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN)` into the function
  itself (every call site sets this identically today — window → layout →
  insert controls should be the *only* thing a caller has to do after
  calling it). Callers that don't host a scrolling list should then pass
  `h = LV_SIZE_CONTENT` so the panel itself is purely content-driven, with
  no guessed pixel height at all. **Note:** for modals that contain a
  `flex_grow` list (the Signals modal today; the future Add Signal
  sub-modal doesn't need this), the panel still needs an **explicit fixed
  height** — not `LV_SIZE_CONTENT` — so the list has a bounded area to
  grow into. That's a deliberate exception for intentionally-scrolling
  lists, not a workaround, and it still doesn't need `SCROLLABLE` touched.
- [ ] `apps/wilicankit/ui_messages.c`:
  - `add_labeled_ta()`'s `row`
  - `add_labeled_switch()`'s `row`
  - `open_message_form()`'s `signals_btn` (also hardcoded to `34`) and its
    `btn_row`
  - `open_message_form()`'s call site: `ui_common_open_modal(360, 310)` →
    `ui_common_open_modal(360, LV_SIZE_CONTENT)` (no scrolling list in
    this modal)
  - `open_signals_modal()`'s `add_row` and `close_row` (these two are also
    slated for removal by Phase B — if Phase A lands first/independently,
    fix them anyway since they still exist until Phase B is done); its
    call site `ui_common_open_modal(440, 280)` **keeps** an explicit
    height (hosts the `flex_grow` placement list)
- [ ] `apps/wilicankit/ui_signals.c` — same duplicated `add_labeled_ta()`
  `row`, plus `open_signal_form()`'s `btn_row` and its
  `ui_common_open_modal(380, 300)` call site → `LV_SIZE_CONTENT` height
  (no scrolling list in this form).
- [ ] `apps/wilicankit/ui_controls.c` — `open_add_slider_form()`'s
  `btn_row` and its modal-open call site's height (lower risk, fix for
  consistency since the user said "many views").

### Open risk — re-verify modal sizes on real hardware

With content-driven heights, a modal simply grows to whatever height its
rows actually need — there's no more hand-picked number to second-guess.
The real remaining risk is whether that natural height **fits the fixed
320px-tall screen**:

- `ui_messages.c` Details modal (name/CAN ID/extended/DLC/channel/manage
  signals/buttons) should have headroom — Phase B further shrinks it once
  signal-placement UI is fully out of it. If content height still exceeds
  ~320px: consider putting the "Extended (29b)" switch on the same row as
  the CAN ID field (side by side) to reclaim a row, rather than reverting
  to a fixed/guessed panel height.
- `apps/wilicankit/ui_signals.c`'s Add/Edit Signal form has **7** labeled
  rows (Name/Units/Bits/Scale/Offset/Min/Max) — this is the highest-risk
  candidate for genuinely not fitting a 320px screen even after content-
  sizing, since it has more rows than the message form. This form's
  overall cramped-ness is **not** part of this round's scope — only fix
  its layout per Phase A; flag the "may need a 2-column layout" concern
  as a candidate follow-up but don't act on it unless asked. If it
  doesn't fit, the right fix is fewer/narrower rows (or a follow-up
  2-column layout), not a fixed panel height with internal scrolling.
- This can't be fully resolved with pixel math alone — build, flash, and
  visually check on the actual display (per `AGENTS.md`: on-hardware
  verification, "Task 9", was still pending as of Round 1 landing), then
  iterate on row content (not panel size guesses) from what's actually
  seen.

---

## Phase B — Manage Signals drill-down (ui_messages.c only)

- [ ] Add a new static function, e.g. `open_add_signal_modal(void)`: a
  small 3rd-level modal (width `~320`, height `LV_SIZE_CONTENT` — no
  scrolling list in this sub-modal, so no fixed height needed) containing
  exactly the 4 widgets currently inline in `open_signals_modal()`'s
  `add_row` — signal dropdown (via the existing
  `rebuild_signal_dropdown()`), start-bit textarea, big-endian switch —
  plus "Add"/"Cancel" buttons.
- [ ] "Add" reuses the existing `add_placement_btn_cb()` validation/append
  logic (dropdown selection check, start-bit clamp, `CAN_MAX_PLACEMENTS`
  check, append to `s_edit_msg.placements[]`) — just triggered from this
  new sub-modal's "Add" button instead of an always-visible "+". On
  success, close this sub-modal and refresh the placement list + the
  "Manage Signals (N)" label back on the Signals modal (both existing
  helper calls: `refresh_placement_list()`, `update_signals_btn_label()`).
- [ ] `open_signals_modal()` shrinks to: title, placement list
  (`flex_grow`, now gets nearly the entire modal height since the add-row
  is gone), a new "**+ Add Signal**" button, and the existing "Close"
  button.
- [ ] Needs a 3rd tracked panel pointer alongside the existing
  `s_form_panel` (Details) / `s_signals_panel` (Signals) — e.g.
  `s_add_signal_panel` — so Close/Cancel/Add only tear down the correct
  modal layer.
- [ ] **Re-verify** the shared on-screen keyboard singleton
  (`ui_common_init`/`s_keyboard` in `ui_common.c`) still binds/shows
  correctly for the start-bit textarea at this **3rd** nesting depth
  (Details → Signals → Add Signal) — it's a single screen-level object
  moved to the foreground each time a textarea focuses; each additional
  modal layer stacks another backdrop+panel on top via
  `ui_common_open_modal`, worth a deliberate on-hardware check once this
  lands, not just an assumption it'll keep working.

---

## Phase C — Bit-grid "frame view": feasibility write-up only (do NOT build yet)

This phase is exploratory per the user's request ("consider what a visual
view... might take to implement") — write it up, don't implement, until
explicitly asked to build it as its own follow-up task.

### Chosen approach: `lv_canvas`

- `LV_USE_CANVAS` is `1` by default in this LVGL build (no override needed
  in `apps/wilicankit/lv_conf.h` — confirmed via
  `build/_deps/lvgl-src/src/lv_conf_internal.h`).
- User explicitly prefers canvas drawing over the two options offered
  (`lv_table` with a custom per-cell-color draw callback, or a grid of
  individual `lv_obj` cells) because CAN-FD frames can be up to 64 bytes,
  and a canvas can draw an arbitrary number of 8-byte rows in one pass
  without per-row/per-cell LVGL object or table-API bookkeeping.

### RAM plan — canvas buffer MUST live in PSRAM, not the LVGL heap

- This app's `LV_MEM_SIZE` (LVGL's object heap, plain SRAM) is currently
  **39 KB** and already had to be trimmed once (Round 1, see
  `apps/wilicankit/docs/build-notes.md`) just to fit an 8-byte struct field
  addition — there is essentially no slack left for a large buffer there.
- This app already establishes the correct pattern for big buffers:
  `apps/wilicankit/lvgl_port.c` claims LVGL's own render/draw buffers
  directly from `PSRAM_BASE` (see `apps/wilicankit/docs/build-notes.md`,
  "PSRAM vs SRAM budget" section) rather than from SRAM. The frame-view
  canvas's pixel buffer should follow the **same** pattern: claim its own
  region of PSRAM (check `lvgl_port.c` for the exact `PSRAM_BASE` offset/
  size already in use so the new claim doesn't collide with it, or route
  through `bsp/platform/psram_layout.h`'s named regions if that ends up
  cleaner than another raw `PSRAM_BASE`-relative claim).
- Rough size check: a ~`440x260`px canvas at RGB565 (`LV_COLOR_DEPTH=16`
  in this app) ≈ `440 * 260 * 2` ≈ 229 KB. Trivial for the 8 MB PSRAM
  budget; would never fit in the 39 KB LVGL heap or the already-tight
  512 KB main SRAM (`copy_to_ram`) region.

### Rendering approach

- LVGL v9's canvas drawing API differs from v8's simpler
  `lv_canvas_draw_rect`/`lv_canvas_draw_text` calls — **verify exact
  function names/signatures** against the vendored header at
  `build/_deps/lvgl-src/src/widgets/canvas/lv_canvas.h` at implementation
  time rather than guessing from memory of v8.
- Draw an 8-column × N-row grid, one cell per bit, where
  `N = ceil(dlc / 8)` (today always `1`, see CAN-FD caveat below).
- For each byte-row, for each of its 8 bit columns: look up which
  placement (if any) in `s_edit_msg.placements[]` covers that absolute bit.
  - Occupied: fill the cell with a color from a small fixed palette
    (cycle `lv_palette_main(LV_PALETTE_*)`, up to `CAN_MAX_PLACEMENTS`
    (16) distinct colors) and draw a single-character label
    (`'A' + placement_slot_index`) centered in the cell — mirrors the
    ASCII example's one-letter-per-signal convention.
  - Unoccupied: draw an empty/outlined cell (like the ASCII example's
    `-`).
- **Legend:** reuse the existing placement `lv_list` from the Signals
  modal (Phase B) as the legend — just prefix each row's label with its
  matching `[A]`-style bracketed letter (mirrors the user's ASCII example
  exactly, e.g. `[G] BattTrac_I_Actl, ...`) and optionally a small
  color-swatch icon matching the grid's fill color for that signal.
- Proposed integration: a "**List**" / "**Frame**" view toggle **inside**
  the existing Signals modal (same modal shell, switch which body is
  shown), rather than yet another (4th) stacked modal layer — avoids
  compounding the modal-stacking/keyboard-focus verification concerns
  already flagged in Phase B.

### Important caveat — CAN-FD is NOT supported by the data model today

- `apps/wilicankit/can_model.h`'s own doc comment states buffers are "up
  to 8 bytes (64 bits)"; `can_placement_t.start_bit` is a `uint8_t`
  (0..63); `ui_messages.c`'s `save_btn_cb` clamps `dlc` to `0..8`.
- Actually supporting 64-byte CAN-FD frames is a **separate, much larger,
  not-yet-scoped** data-model change: widening the start-bit/length
  representation, rewriting `can_pack_bits`/`can_unpack_bits` (currently
  `tests/test_can_pack.c`-covered) for up to 512 bits, handling CAN-FD's
  non-linear DLC-to-byte-length table, bumping `CAN_MAX_PLACEMENTS`, and
  re-validating everything that assumes an 8-byte frame.
- **Recommendation:** write the frame-view renderer generically (loop over
  `ceil(dlc/8)` rows already, don't hardcode "1 row of 8") so it's
  CAN-FD-shaped in the drawing code, **without** committing to full
  CAN-FD data-model support in the same pass. Today it will simply always
  render exactly 1 row, since `dlc` is capped at 8, until/unless CAN-FD
  support is separately scoped and built as its own project.

### Effort estimate

Moderate. Most of the risk/unknown is the LVGL v9 canvas draw-call API
specifics (worth a short spike reading the vendored header before
committing to exact function names) and the PSRAM-region bookkeeping; the
bit → cell → color → letter logic and tap-to-highlight wiring are
standard-shaped, similar in spirit to code already in this file
(`refresh_placement_list`, `rebuild_signal_dropdown`).

**Recommendation:** prototype as its own follow-up task **after** Phases A
and B are implemented, built, flashed, and visually confirmed on real
hardware — don't bundle into the same change as A/B.

---

## Relevant files

- `apps/wilicankit/ui_common.c` — `ui_common_open_modal` (Phase A: fold
  `LV_FLEX_FLOW_COLUMN` into it; support `h = LV_SIZE_CONTENT` callers).
- `apps/wilicankit/ui_messages.c` — bulk of Phase A, all of Phase B, most
  of Phase C if/when it's built.
- `apps/wilicankit/ui_signals.c` — Phase A only (fixes the duplicated
  row-building bug); do **not** touch its separate 7-row cramped-form
  issue unless separately asked.
- `apps/wilicankit/ui_controls.c` — Phase A only
  (`open_add_slider_form`'s `btn_row`).
- `apps/wilicankit/lvgl_port.c` — reference for the existing `PSRAM_BASE`
  claim pattern, needed before Phase C's canvas buffer placement.
- `apps/wilicankit/docs/build-notes.md` — "PSRAM vs SRAM budget" section;
  update again once/if Phase C's canvas buffer lands.
- `build/_deps/lvgl-src/src/widgets/canvas/lv_canvas.h` — LVGL v9 canvas
  API reference for Phase C (vendored/fetched, git-ignored, present after
  any successful CMake configure — do not edit).

## Verification (for whichever phase is implemented)

1. Build: `& "$env:USERPROFILE\.pico-sdk\ninja\v1.12.1\ninja.exe" -C
   "<repo>\build" wilicankit` (or the "Compile Project" VS Code task) —
   must link clean within the RAM budget (watch for the same
   `.heap`/`RAM` region overflow class of error seen in Round 1 if any new
   `static`/`.bss` growth creeps in).
2. Flash: `python tools/fw.py flash wilicankit` (DISPLAY chip, OpenOCD
   interface 0) — per `AGENTS.md`, this repo covers the display processor
   only.
3. On-hardware visual check: open Messages tab → Add/Edit Message → Manage
   Signals → (Phase B) Add Signal sub-modal → confirm no stray scrollbars
   anywhere (Phase A), confirm the placement list has comfortably more
   room (Phase B), and (once Phase C is eventually built) confirm the
   frame view renders/updates correctly as placements are added/removed.
