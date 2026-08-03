# wilicankit — data model & bit-packing convention

Full detail behind `can_model.h`/`can_model.c`. Read this before touching
`can_pack_bits`/`can_unpack_bits`/`can_message_build` or the Messages tab's
placement editor.

## Structs (can_model.h)

- `can_signal_t` — `name`, `units`, `bit_length` (1..64), `scale`, `offset`,
  `min_value`/`max_value` (physical range, used for slider bounds), `value`
  (current physical value). **v1 scope: unsigned raw values only** — no
  two's-complement/signed signal support.
- `can_placement_t` — `signal_id` (index into a `can_signal_t[]`,
  `CAN_SIGNAL_ID_NONE` = unused slot), `start_bit` (0..63), `big_endian`.
- `can_message_t` — `name` (user-facing label, `CAN_MESSAGE_NAME_MAX` chars,
  not required/validated), `can_id`, `extended_id`, `dlc`, `channel` (OneWili
  CAN channel index, 0 or 1), `period_us`, `enabled`, and up to
  `CAN_MAX_PLACEMENTS` (16) `can_placement_t` entries.
- `can_control_t` — `signal_id` plus `control_type` (`CAN_CONTROL_SLIDER` or
  `CAN_CONTROL_TOGGLE`); GUI-position/layout state is NOT persisted (the
  Controls tab just lists all in-use controls in table order).

Limits (all in `can_model.h`, bump if needed — nothing else assumes these
exact values beyond fixed-size array declarations):
`CAN_MAX_SIGNALS=200`, `CAN_MAX_MESSAGES=64`, `CAN_MAX_PLACEMENTS=32`,
`CAN_MAX_CONTROLS=200`, `CAN_SIGNAL_NAME_MAX=24`, `CAN_MESSAGE_NAME_MAX=24`.
All three tables (and `storage.c`'s scratch-copy buffers used while
loading) live in PSRAM (`__uninitialized_psram`, `app_state.c`), not the
tight SRAM budget, so these are sized generously rather than rationed.

On-disk format (`storage_json.c`, `STORAGE_JSON_FORMAT_VERSION=2`): sparse
and id-keyed — only `in_use` entries are written, each carrying its own
`"id"` (the table slot index; signal_id references elsewhere point at this
same id). A message's `placements` array is trimmed to its actual count
(no padding to `CAN_MAX_PLACEMENTS`); `placement_count` itself isn't
stored — it's derived from the array length on load. The old v1 (fixed-
position, every slot written) format is no longer understood by the
firmware; `tools/wilicankit_json_migrate_v1_to_v2.py` converts an old file
offline.

## CAN-FD: transport supports it, this data model does not (yet)

The FreeWili 2 board/OneWili transport already supports CAN-FD —
`can_link.c` calls the CAN-FD TX functions (`ow_io_canfd_write_canfd*`)
today. This app's **data model** does not yet take advantage of that:
buffers are capped at 8 bytes/64 bits (`can_placement_t.start_bit` is a
`uint8_t`, 0..63; `ui_messages.c` clamps `dlc` to `0..8`), i.e. classic-CAN
framing only.

Extending to full CAN-FD (up to 64-byte frames) is a deferred,
separately-scoped follow-up, not implemented today. It would need: a wider
start-bit/length representation, `can_pack_bits`/`can_unpack_bits`
extended to handle up to 512 bits, CAN-FD's non-linear DLC-to-byte-length
table (DLC 9..15 map to 12/16/20/24/32/48/64 bytes, not `dlc` bytes
directly), a bumped `CAN_MAX_PLACEMENTS`, and re-validating every place
that currently assumes an 8-byte frame (including `tests/test_can_pack.c`).

## Bit numbering convention — IMPORTANT, read before editing

`start_bit` (0..63) always names where the signal begins, but Intel and
Motorola number bits completely differently across the frame — this is
standard DBC/Vehicle Spy 3 behavior, not an app-local quirk, so start bits
taken directly from a real DBC/Vehicle Spy signal definition carry over
unchanged.

- **little_endian (Intel)**: bit 0 = LSB of `buf[0]`, bit 7 = MSB of
  `buf[0]`, bit 8 = LSB of `buf[1]`, ... bit 63 = MSB of `buf[7]` (as if the
  whole buffer were one little-endian 64-bit integer). `start_bit` names
  the signal's LSB; successive (more-significant) bits fill upward —
  `start_bit`, `start_bit+1`, ... straight through this numbering.
- **big_endian (Motorola)**: bit 0 = MSB of `buf[0]`, bit 7 = LSB of
  `buf[0]`, bit 8 = MSB of `buf[1]`, ... bit 63 = LSB of `buf[7]` —
  sequential numbering across the whole frame, where each next bit number
  is one bit *less* significant, wrapping cleanly to the next byte's MSB
  at every 8-bit boundary. `start_bit` names the signal's MSB; successive
  (less-significant) bits fill via `start_bit+1`, `start_bit+2`, ... in
  that same sequential numbering.

As of 2026-07-27 this was fixed to match genuine DBC/Vehicle Spy 3
Motorola bit numbering (it previously used an app-local "LSB of buf[0] is
position 0" anchor for *both* endiannesses, which only agreed with the
real convention for Intel — a Motorola signal's `start_bit` had to be
offset by 7 within its starting byte to land where a real DBC file would
call it `start_bit=0`; see git history for `can_model.c`/`can_model.h` if
you need the old behavior). Since wilicankit never imports/exports DBC
files this was never a decode-format-compatibility requirement, but it's
the convention users bringing signal definitions over from Vehicle Spy 3
(or any DBC) will expect, so it's now DBC-accurate rather than app-local.

**Concrete examples**:
- A byte-aligned single-byte signal (`start_bit=0, length=8`) packed as
  `big_endian=true` leaves the byte **unmirrored**: `0xAB` packs to
  `buf[0]==0xAB`.
- A byte-aligned 16-bit signal (`start_bit=0, length=16`) packed as
  `big_endian=true` lays out normal big-endian byte order: `0x1234` packs
  to `buf[0]==0x12, buf[1]==0x34`.

## Pack/unpack functions (can_model.c)

- `can_pack_bits(buf, start_bit, length, big_endian, value)` /
  `can_unpack_bits(...)` — the primitives above. Both are a safe no-op
  (return without writing / return 0) if `start_bit + length > 64` for
  *either* endianness (this is a plain linear-index check for both now —
  Motorola's sequential numbering never has to "jump past byte 7" the way
  the old anchor-and-walk scheme did). Also logs via `DIAG()` (RTT) so an
  invalid placement is visible, not silent.
- `can_signal_to_raw(sig, physical_value)` — `raw = (physical - offset) /
  scale`, clamped to `[0, 2^bit_length - 1]` (unsigned only, per v1 scope).
- `can_signal_from_raw(sig, raw_value)` — `physical = raw * scale + offset`.
- `can_message_build(msg, signals, buf)` — zeroes `buf[8]`, then for every
  placement in `msg` (skipping `CAN_SIGNAL_ID_NONE` slots), converts that
  signal's current `.value` to raw and packs it in. This is what
  `can_link_send_once`/`can_link_arm_periodic` call before every OneWili TX.

## Known gap: no overlap validation

Nothing currently stops the user from placing two signals at overlapping
bit ranges in the Messages tab form — `can_message_build` will just let the
later-processed placement's bits clobber the earlier one's silently. This
works (no crash, no UB) but gives no warning. Candidate future improvement:
validate placements at Save time in `ui_messages.c` and show an error label
instead of closing the modal.
