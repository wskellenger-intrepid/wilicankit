// apps/wilicankit/can_model.h — signal/message data model + bit pack/unpack.
//
// Bit numbering convention matches genuine DBC/Vehicle Spy 3 semantics.
// `start_bit` (0..63) always names where the signal begins, but the two
// endiannesses number bits completely differently — this is standard DBC
// behavior, not an app-local quirk:
//   - little_endian (Intel): bit 0 = LSB of buf[0], bit 7 = MSB of buf[0],
//     bit 8 = LSB of buf[1], ... bit 63 = MSB of buf[7] (right-to-left
//     within a byte, then on to the next byte). `start_bit` names the
//     signal's LSB; successive (more-significant) bits fill upward —
//     `start_bit`, `start_bit+1`, ... — straight through this numbering.
//   - big_endian (Motorola): bit 0 = MSB of buf[0], bit 7 = LSB of buf[0],
//     bit 8 = MSB of buf[1], ... bit 63 = LSB of buf[7] (sequential
//     numbering across the whole frame — each next number is one bit less
//     significant, wrapping to the next byte's MSB at each 8-bit
//     boundary). `start_bit` names the signal's MSB; successive
//     (less-significant) bits fill via `start_bit+1`, `start_bit+2`, ...
//     in that same sequential numbering.
// Since wilicankit is self-contained (no DBC import/export), pick start_bit
// + endianness for a signal and check the result against a bus analyzer if
// unsure, but the convention itself matches genuine DBC/Vehicle Spy 3
// bit numbering, so start bits taken directly from a real DBC/Vehicle Spy
// signal definition should carry over unchanged.
//
// v1 scope: unsigned raw values only (no two's-complement/signed signals).
//
// CAN-FD note: the OneWili transport (`can_link.c`) already calls the
// CAN-FD TX functions (`ow_io_canfd_write_canfd*`), so the board/link
// layer supports CAN-FD frames. This data model does NOT yet: buffers here
// are capped at 8 bytes/64 bits (`start_bit` is 0..63, `dlc` is clamped to
// 0..8), matching classic CAN only. Supporting full 64-byte CAN-FD frames
// is a deferred, separately-scoped follow-up (wider start-bit/length
// representation, `can_pack_bits`/`can_unpack_bits` extended to 512 bits,
// CAN-FD's non-linear DLC-to-byte-length table, etc.) — see
// `docs/data-model.md`.
#ifndef WILICANKIT_CAN_MODEL_H
#define WILICANKIT_CAN_MODEL_H
#include <stdint.h>
#include <stdbool.h>

#define CAN_SIGNAL_NAME_MAX   24
#define CAN_SIGNAL_UNITS_MAX  12
#define CAN_MESSAGE_NAME_MAX  24
// PSRAM-backed (app_state.c) so these are sized generously, not for the
// tight SRAM budget; kept below CAN_SIGNAL_ID_NONE so uint8_t ids stay safe.
#define CAN_MAX_SIGNALS       200
#define CAN_MAX_MESSAGES      64
#define CAN_MAX_PLACEMENTS    32
#define CAN_MAX_CONTROLS      200
#define CAN_SIGNAL_ID_NONE    0xFFu

// can_control_t.control_type — which widget kind a control slot renders as.
#define CAN_CONTROL_SLIDER    0u
#define CAN_CONTROL_TOGGLE    1u

typedef struct {
    bool    in_use;
    char    name[CAN_SIGNAL_NAME_MAX];
    char    units[CAN_SIGNAL_UNITS_MAX];
    uint8_t bit_length;   // 1..64
    double  scale;
    double  offset;
    double  min_value;    // physical range (slider bounds, etc.)
    double  max_value;
    double  value;        // current physical value
} can_signal_t;

typedef struct {
    uint8_t signal_id;    // index into a can_signal_t[]; CAN_SIGNAL_ID_NONE = unused slot
    uint8_t start_bit;    // 0..63
    bool    big_endian;   // true = Motorola-style (MSB-first), false = Intel-style (LSB-first)
} can_placement_t;

typedef struct {
    bool            in_use;
    char            name[CAN_MESSAGE_NAME_MAX];
    uint32_t        can_id;
    bool            extended_id;  // 29-bit vs 11-bit
    uint8_t         dlc;           // 0..8
    uint8_t         channel;       // OneWili CAN channel index (0 or 1)
    uint32_t        period_us;     // periodic TX period; 0 = one-shot only
    bool            enabled;       // periodic slot armed
    uint8_t         placement_count;
    can_placement_t placements[CAN_MAX_PLACEMENTS];
} can_message_t;

typedef struct {
    bool    in_use;
    uint8_t signal_id;
    uint8_t control_type : 1; // CAN_CONTROL_SLIDER or CAN_CONTROL_TOGGLE
    uint8_t sine_enabled : 1; // transient exercise mode, not persisted to JSON
} can_control_t;

// Pack `value` (raw, unsigned) into `buf` (>= 8 bytes) at the given bit
// placement. No-op if the placement would overrun the 64-bit buffer.
void can_pack_bits(uint8_t *buf, uint8_t start_bit, uint8_t length, bool big_endian, uint64_t value);

// Unpack an unsigned raw value from `buf` (>= 8 bytes) at the given bit
// placement. Returns 0 if the placement would overrun the 64-bit buffer.
uint64_t can_unpack_bits(const uint8_t *buf, uint8_t start_bit, uint8_t length, bool big_endian);

// physical -> raw, clamped to what fits in sig->bit_length unsigned bits.
uint64_t can_signal_to_raw(const can_signal_t *sig, double physical_value);

// raw -> physical.
double can_signal_from_raw(const can_signal_t *sig, uint64_t raw_value);

// Rebuild the 8-byte message buffer (bytes beyond msg->dlc are left zeroed)
// from the current physical value of every signal placed in `msg`.
// `signals` is the owning signal table, indexed by placement.signal_id.
void can_message_build(const can_message_t *msg, const can_signal_t *signals, uint8_t *buf);

#endif // WILICANKIT_CAN_MODEL_H
