# wilicankit — known risks / open questions

Things that are implemented but **unverified**, or deliberately scoped out.
Check these off (or fix them) as hardware testing happens.

## 1. Periodic TX is display-CPU-resident, not MAIN-side — UNVERIFIED

Periodic sends are driven by a timer loop on the display CPU
(`can_link_periodic_poll()` in `can_link.c`, polled from `main.c`'s super
loop), not by MAIN's OneWili periodic-slot API. This was a deliberate
change: handing a message to MAIN to resend at a fixed rate means this app
loses the ability to update that message's *contents* between sends (a
bound signal changing, a slider moving, etc. would not reach the bus until
the next explicit re-arm) — resending it ourselves keeps this app in
control of payload freshness on every TX. `can_link_arm_periodic()` /
`can_link_disable_periodic()` (the old MAIN-side-periodic path, via
`ow_io_canfd_write_canfd_periodic()`) are kept in `can_link.c`/`can_link.h`
but are currently unused — retained for a possible future revisit rather
than removed outright.

**This has never been tested against real hardware.** The timer uses a real
`to_ms_since_boot()` timestamp (not a fixed per-loop-iteration assumption),
but actual jitter depends on the super loop's iteration time (LVGL render
load, USB storage task, the FwGUI round-trip inside `can_link_send_once()`
itself, etc.) — short periods (well under the loop's typical iteration
time) will be inaccurate. If a CAN analyzer shows jitter or missed periods
under load, that loop-timing coupling is the first thing to check.

## 2. 500 kbit/s bitrate — assumed pre-configured on MAIN, UNVERIFIED

OneWili exposes no "set CAN bitrate" command — only a raw register poke
(`ow_io_canfd_set_can_register`), and there's no register map for MAIN's
CAN controller in this repo. This app assumes MAIN's firmware already has
the CAN channel running at 500 kbit/s. **Verify with a CAN analyzer's own
bit-rate detection before trusting any capture.** If wrong, fixing it from
this app would need the register map (out of reach right now) — this may
need to be a MAIN-firmware-side fix instead, which is explicitly out of
scope for this app (see `architecture.md` constraint #1).

## 3. CAN channel index (0 or 1) — user-selectable, not auto-detected

The Messages tab's "Channel" field lets the user pick 0 or 1 per message.
Nothing in this app knows which physical channel the Xterra (or any other
target) harness is actually wired to — that's determined at hardware
bring-up time, by trial or by knowing the board's wiring.

## 4. No overlap validation in the Messages tab

See `data-model.md`'s "Known gap" section. Two signals placed at
overlapping bit ranges will silently clobber each other in
`can_message_build` — no error is shown to the user. Not a crash risk, just
a UX gap.

## 5. No signed (two's-complement) signal support

`can_model.h` documents this explicitly as a v1 scope exclusion — all raw
values are unsigned. A signal that needs to represent negative physical
values must do so entirely through `offset` (e.g. temperature in `°C` via
`offset=-40`), not via a signed raw encoding. If a target message truly
needs two's-complement bits, this app can't produce them yet.

## 6. No CAN FD support

`can_fd` is always passed as `0` (classic CAN) to every OneWili call —
matches the 2006 Xterra (predates CAN FD) and keeps `can_message_t` simpler
(max 8-byte payload). Would need a deliberate scope expansion (DLC up to
64 bytes, `can_model.h`'s 8-byte buffer assumptions revisited) to add.

## 7. No USB-stick-independent storage

Configs only exist on whatever USB stick is plugged in
(`0:/wilicankit/*.json`, FatFs). No internal-flash fallback. If the stick
isn't present, `storage_list_configs`/`_save_config`/`_load_config` fail
gracefully (return false/0, DIAG a message) but nothing persists across a
power cycle without one.

Separately: the schema (v2) is sparse and id-keyed — only in-use signal/
message/control entries are ever written, each carrying its own `id`, so a
fully-populated config (up to 200 signals / 64 messages ×32 placements /
200 controls) still only builds a node tree sized to what's actually in
use, not the full capacity. `STORAGE_JSON_MAX_BYTES`/`JSON_ARENA_BYTES`
(`storage.c`) are sized for the fully-populated worst case regardless. This
is mitigated by pointing cJSON's allocator (`cJSON_InitHooks`) at a
PSRAM-backed bump arena (`storage.c`) rather than the default heap, so it
never competes with the tight SRAM budget — but the mitigation itself
(arena sizing, PSRAM read/write correctness for FatFs I/O) is **unverified
on real hardware** as of this writing; see risk #8.

## 8. Nothing has run on real hardware yet

This is the big one — re-stated from `architecture.md`. Everything above
is "should work based on reading the OneWili API and BSP driver headers
carefully," not "confirmed working." Run the verification checklist in
`architecture.md` before relying on any of this for actual instrument
cluster (or other device) control.
