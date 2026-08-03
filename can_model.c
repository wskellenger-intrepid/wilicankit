// apps/wilicankit/can_model.c — see can_model.h.
#include "can_model.h"
#include <string.h>

#ifndef HOST_TEST
#include "platform/diag.h"
#else
#define DIAG(...) ((void)0)
#endif

static void set_bit(uint8_t *buf, unsigned bit_pos, unsigned bit_val) {
    unsigned byte_idx = bit_pos / 8u;
    unsigned bit_in_byte = bit_pos % 8u;
    if (bit_val) buf[byte_idx] = (uint8_t)(buf[byte_idx] | (1u << bit_in_byte));
    else         buf[byte_idx] = (uint8_t)(buf[byte_idx] & ~(1u << bit_in_byte));
}

static unsigned get_bit(const uint8_t *buf, unsigned bit_pos) {
    unsigned byte_idx = bit_pos / 8u;
    unsigned bit_in_byte = bit_pos % 8u;
    return (unsigned)((buf[byte_idx] >> bit_in_byte) & 1u);
}

// Converts a Motorola (big_endian) sequential bit number to a physical bit
// position (0 = LSB of buf[0] ... 63 = MSB of buf[7]), matching genuine
// DBC/Vehicle Spy Motorola numbering: bit_number 0 is the MSB of byte 0,
// and incrementing always steps to the next less-significant bit, wrapping
// cleanly to the MSB of the next byte at each 8-bit boundary.
static unsigned moto_bit_number_to_pos(unsigned bit_number) {
    unsigned byte_idx = bit_number / 8u;
    unsigned bit_in_byte = 7u - (bit_number % 8u);
    return byte_idx * 8u + bit_in_byte;
}

void can_pack_bits(uint8_t *buf, uint8_t start_bit, uint8_t length, bool big_endian, uint64_t value) {
    if (length == 0 || length > 64) {
        DIAG("can_model: invalid placement start_bit=%u length=%u big_endian=%u\n", start_bit, length, big_endian);
        return;
    }
    if ((unsigned)start_bit + (unsigned)length > 64u) {
        DIAG("can_model: invalid placement start_bit=%u length=%u big_endian=%u\n", start_bit, length, big_endian);
        return;
    }
    if (big_endian) {
        for (uint8_t i = 0; i < length; i++) {
            unsigned bit = (unsigned)((value >> (length - 1 - i)) & 1u);
            set_bit(buf, moto_bit_number_to_pos((unsigned)start_bit + i), bit);
        }
    } else {
        for (uint8_t i = 0; i < length; i++) {
            unsigned bit = (unsigned)((value >> i) & 1u);
            set_bit(buf, (unsigned)start_bit + i, bit);
        }
    }
}

uint64_t can_unpack_bits(const uint8_t *buf, uint8_t start_bit, uint8_t length, bool big_endian) {
    if (length == 0 || length > 64) {
        DIAG("can_model: invalid placement start_bit=%u length=%u big_endian=%u\n", start_bit, length, big_endian);
        return 0;
    }
    if ((unsigned)start_bit + (unsigned)length > 64u) {
        DIAG("can_model: invalid placement start_bit=%u length=%u big_endian=%u\n", start_bit, length, big_endian);
        return 0;
    }
    uint64_t value = 0;
    if (big_endian) {
        for (uint8_t i = 0; i < length; i++) {
            uint64_t bit = get_bit(buf, moto_bit_number_to_pos((unsigned)start_bit + i));
            value |= (bit << (length - 1 - i));
        }
    } else {
        for (uint8_t i = 0; i < length; i++) {
            uint64_t bit = get_bit(buf, (unsigned)start_bit + i);
            value |= (bit << i);
        }
    }
    return value;
}

uint64_t can_signal_to_raw(const can_signal_t *sig, double physical_value) {
    double scale = (sig->scale != 0.0) ? sig->scale : 1.0;
    double raw_d = (physical_value - sig->offset) / scale;
    if (raw_d < 0.0) raw_d = 0.0;
    uint64_t max_raw = (sig->bit_length >= 64) ? UINT64_MAX : (((uint64_t)1 << sig->bit_length) - 1u);
    if (raw_d > (double)max_raw) raw_d = (double)max_raw;
    return (uint64_t)(raw_d + 0.5);
}

double can_signal_from_raw(const can_signal_t *sig, uint64_t raw_value) {
    return (double)raw_value * sig->scale + sig->offset;
}

void can_message_build(const can_message_t *msg, const can_signal_t *signals, uint8_t *buf) {
    memset(buf, 0, 8);
    for (uint8_t i = 0; i < msg->placement_count; i++) {
        const can_placement_t *p = &msg->placements[i];
        if (p->signal_id == CAN_SIGNAL_ID_NONE) continue;
        const can_signal_t *sig = &signals[p->signal_id];
        uint64_t raw = can_signal_to_raw(sig, sig->value);
        can_pack_bits(buf, p->start_bit, sig->bit_length, p->big_endian, raw);
    }
}
