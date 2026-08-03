#include "test_util.h"
#include "can_model.h"
#include <string.h>

static can_signal_t make_signal(uint8_t bit_length, double scale, double offset) {
    can_signal_t s;
    memset(&s, 0, sizeof s);
    s.in_use = true;
    s.bit_length = bit_length;
    s.scale = scale;
    s.offset = offset;
    return s;
}

int main(void) {
    // --- little-endian (Intel) round trip, byte-aligned ---
    {
        uint8_t buf[8] = {0};
        can_pack_bits(buf, 0, 8, false, 0xAB);
        ASSERT_EQ(buf[0], 0xAB);
        ASSERT_EQ(can_unpack_bits(buf, 0, 8, false), 0xAB);
    }

    // --- little-endian, crossing a byte boundary ---
    {
        uint8_t buf[8] = {0};
        // 12-bit value 0xABC starting at bit 4: low nibble of byte0 stays 0,
        // bits 4..15 hold the value.
        can_pack_bits(buf, 4, 12, false, 0xABC);
        ASSERT_EQ(can_unpack_bits(buf, 4, 12, false), 0xABC);
        ASSERT_EQ(buf[0] & 0x0F, 0x00);          // untouched low nibble
        ASSERT_EQ(buf[0] >> 4, 0xC);              // LSB nibble of value
    }

    // --- little-endian, non-byte-aligned 16-bit signal spanning 3 bytes ---
    // start_bit=44 is bit4 of buf[5]; the 16 bits land partly in buf[5]
    // (top nibble), all of buf[6], and partly in buf[7] (bottom nibble).
    {
        uint8_t buf[8] = {0};
        can_pack_bits(buf, 44, 16, false, 0x1234);
        ASSERT_EQ(buf[5], 0x40);
        ASSERT_EQ(buf[6], 0x23);
        ASSERT_EQ(buf[7], 0x01);
        ASSERT_EQ(buf[4], 0x00);                  // untouched
        ASSERT_EQ(can_unpack_bits(buf, 44, 16, false), 0x1234);
    }

    // --- little-endian, byte-aligned 16-bit signal starting mid-buffer ---
    // start_bit=24 is bit0 of buf[3]; a 16-bit signal there spans buf[3]
    // (low byte) and buf[4] (high byte) — standard little-endian layout.
    {
        uint8_t buf[8] = {0};
        can_pack_bits(buf, 24, 16, false, 0x1234);
        ASSERT_EQ(buf[3], 0x34);
        ASSERT_EQ(buf[4], 0x12);
        ASSERT_EQ(buf[0], 0x00);
        ASSERT_EQ(buf[1], 0x00);
        ASSERT_EQ(buf[2], 0x00);
        ASSERT_EQ(buf[5], 0x00);
        ASSERT_EQ(can_unpack_bits(buf, 24, 16, false), 0x1234);
    }

    // --- big-endian (Motorola) round trip, byte-aligned single byte ---
    // Real Motorola/DBC wire format preserves bit order within a byte, so a
    // byte-aligned placement leaves the value unmirrored.
    {
        uint8_t buf[8] = {0};
        can_pack_bits(buf, 0, 8, true, 0xAB);
        ASSERT_EQ(buf[0], 0xAB);
        ASSERT_EQ(can_unpack_bits(buf, 0, 8, true), 0xAB);
    }

    // --- big-endian, byte-aligned 16-bit signal: normal big-endian byte order ---
    {
        uint8_t buf[8] = {0};
        can_pack_bits(buf, 0, 16, true, 0x1234);
        ASSERT_EQ(buf[0], 0x12);
        ASSERT_EQ(buf[1], 0x34);
        ASSERT_EQ(can_unpack_bits(buf, 0, 16, true), 0x1234);
    }

    // --- big-endian, non-byte-aligned, crossing a byte boundary ---
    // Hand-derived exact expected bytes by walking the genuine DBC/Vehicle
    // Spy Motorola sequential bit numbering (see docs/data-model.md):
    // bit_number = start_bit+i, byte = bit_number/8, bit_in_byte = 7 -
    // (bit_number%8).
    {
        uint8_t buf[8] = {0};
        can_pack_bits(buf, 4, 12, true, 0xABC);
        ASSERT_EQ(buf[0], 0x0A);
        ASSERT_EQ(buf[1], 0xBC);
        ASSERT_EQ(can_unpack_bits(buf, 4, 12, true), 0xABC);
    }

    // --- 1-bit and 64-bit edge lengths ---
    {
        uint8_t buf[8] = {0};
        can_pack_bits(buf, 3, 1, false, 1);
        ASSERT_EQ(can_unpack_bits(buf, 3, 1, false), 1);
        ASSERT_EQ(can_unpack_bits(buf, 2, 1, false), 0);

        uint8_t buf64[8] = {0};
        can_pack_bits(buf64, 0, 64, false, 0x0102030405060708ull);
        // ASSERT_EQ truncates through `long` (32-bit on LLP64/Windows), so
        // compare the full 64-bit value directly.
        ASSERT_TRUE(can_unpack_bits(buf64, 0, 64, false) == 0x0102030405060708ull);
    }

    // --- out-of-range placements are a safe no-op ---
    {
        uint8_t buf[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        can_pack_bits(buf, 60, 8, false, 0x42);   // 60+8 > 64 -> must not write
        for (int i = 0; i < 8; i++) ASSERT_EQ(buf[i], 0xFF);
        ASSERT_EQ(can_unpack_bits(buf, 60, 8, false), 0);
    }

    // --- out-of-range big-endian (Motorola) placement is also a safe no-op ---
    // start_bit=60, length=8 -> 60+8 > 64, symmetric with the little-endian
    // out-of-range check above (Motorola's sequential numbering is a plain
    // linear-index check too now, not a byte-boundary walk).
    {
        uint8_t buf[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        can_pack_bits(buf, 60, 8, true, 0x42);
        for (int i = 0; i < 8; i++) ASSERT_EQ(buf[i], 0xFF);
        ASSERT_EQ(can_unpack_bits(buf, 60, 8, true), 0);
    }

    // --- signal scale/offset conversions ---
    {
        can_signal_t speed = make_signal(8, 1.0, 0.0);   // 0..255 km/h, 1:1
        ASSERT_EQ(can_signal_to_raw(&speed, 42.0), 42);
        ASSERT_NEAR(can_signal_from_raw(&speed, 42), 42.0, 1e-9);
        ASSERT_EQ(can_signal_to_raw(&speed, 999.0), 255);   // clamps to max unsigned range
        ASSERT_EQ(can_signal_to_raw(&speed, -5.0), 0);       // clamps to 0

        can_signal_t temp = make_signal(8, 0.5, -40.0);      // classic OBD-style coolant temp
        ASSERT_EQ(can_signal_to_raw(&temp, -40.0), 0);
        ASSERT_NEAR(can_signal_from_raw(&temp, 0), -40.0, 1e-9);
        ASSERT_NEAR(can_signal_from_raw(&temp, 100), 10.0, 1e-9);   // 100*0.5-40=10
    }

    // --- full message build from placed signals ---
    {
        can_signal_t signals[2];
        signals[0] = make_signal(8, 1.0, 0.0);
        signals[0].value = 0x12;
        signals[1] = make_signal(8, 1.0, 0.0);
        signals[1].value = 0x34;

        can_message_t msg;
        memset(&msg, 0, sizeof msg);
        msg.in_use = true;
        msg.dlc = 8;
        msg.placement_count = 2;
        msg.placements[0].signal_id = 0;
        msg.placements[0].start_bit = 0;
        msg.placements[0].big_endian = false;
        msg.placements[1].signal_id = 1;
        msg.placements[1].start_bit = 8;
        msg.placements[1].big_endian = false;

        uint8_t buf[8];
        can_message_build(&msg, signals, buf);
        ASSERT_EQ(buf[0], 0x12);
        ASSERT_EQ(buf[1], 0x34);
        ASSERT_EQ(buf[2], 0x00);
    }

    TEST_RETURN();
}
