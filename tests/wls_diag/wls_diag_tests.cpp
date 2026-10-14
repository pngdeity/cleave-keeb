/* Copyright 2026
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/* Proof of the diagnostic codec, without hardware.
 *
 * The shell's state snapshot is the project's observability seam: the only way
 * to see how the firmware reads the switch/host/device index without a working
 * USB line or radio link. These vectors pin the wire format so a host reader and
 * the firmware cannot drift, and pin the round-trip so an encode/decode pair
 * cannot silently disagree.
 */

#include "gtest/gtest.h"

extern "C" {
#include "keyboards/epomaker/epomaker_split65/wls/diag_logic.h"
}

namespace {

TEST(WlsDiag, encode_writes_the_fixed_layout_in_order) {
    wls_diag_t d                      = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    uint8_t    buf[WLS_DIAG_ENC_SIZE] = {0};

    EXPECT_EQ(WLS_DIAG_ENC_SIZE, wls_diag_encode(&d, buf));

    EXPECT_EQ(1, buf[0]);  // switch_pos
    EXPECT_EQ(2, buf[1]);  // devs
    EXPECT_EQ(3, buf[2]);  // host
    EXPECT_EQ(4, buf[3]);  // boot_count
    EXPECT_EQ(5, buf[4]);  // usb_line
    EXPECT_EQ(6, buf[5]);  // link_state
    EXPECT_EQ(7, buf[6]);  // pin_c14
    EXPECT_EQ(8, buf[7]);  // pin_c15
    EXPECT_EQ(9, buf[8]);  // pin_b9
    EXPECT_EQ(10, buf[9]); // reset_cause
}

TEST(WlsDiag, decode_is_the_inverse_of_encode) {
    wls_diag_t in                     = {3, 6, 5, 42, 0, 2, 1, 0, 1, 4};
    uint8_t    buf[WLS_DIAG_ENC_SIZE] = {0};
    wls_diag_t out                    = {0};

    wls_diag_encode(&in, buf);
    ASSERT_TRUE(wls_diag_decode(buf, &out));

    EXPECT_EQ(in.switch_pos, out.switch_pos);
    EXPECT_EQ(in.devs, out.devs);
    EXPECT_EQ(in.host, out.host);
    EXPECT_EQ(in.boot_count, out.boot_count);
    EXPECT_EQ(in.usb_line, out.usb_line);
    EXPECT_EQ(in.link_state, out.link_state);
    EXPECT_EQ(in.pin_c14, out.pin_c14);
    EXPECT_EQ(in.pin_c15, out.pin_c15);
    EXPECT_EQ(in.pin_b9, out.pin_b9);
    EXPECT_EQ(in.reset_cause, out.reset_cause);
}

TEST(WlsDiag, round_trip_preserves_every_field_at_the_extremes) {
    wls_diag_t in                     = {0xFF, 0, 0xFF, 0, 1, 0x7F, 1, 0, 0xFF, 0x3F};
    uint8_t    buf[WLS_DIAG_ENC_SIZE] = {0};
    wls_diag_t out                    = {0};

    wls_diag_encode(&in, buf);
    ASSERT_TRUE(wls_diag_decode(buf, &out));

    EXPECT_EQ(0xFF, out.switch_pos);
    EXPECT_EQ(0x00, out.devs);
    EXPECT_EQ(0xFF, out.host);
    EXPECT_EQ(0x00, out.boot_count);
    EXPECT_EQ(0x01, out.usb_line);
    EXPECT_EQ(0x7F, out.link_state);
    EXPECT_EQ(0x01, out.pin_c14);
    EXPECT_EQ(0x00, out.pin_c15);
    EXPECT_EQ(0xFF, out.pin_b9);
    EXPECT_EQ(0x3F, out.reset_cause); // the six causes fit in the low bits
}

TEST(WlsDiag, decode_rejects_null) {
    wls_diag_t out = {0};
    EXPECT_FALSE(wls_diag_decode(nullptr, &out));
}

/* The golden vector shared with `host/battery_polybar.py`.
 *
 * The 0xA4 report is written in C and read in Python, and nothing fails when the
 * two disagree -- the layout is a contract by convention. This is the one
 * checked-in statement of it: the host asserts the same literal bytes decode to
 * the same fields. See `docs/0xA4-golden-vector.md`. */
TEST(WlsDiag, golden_vector_diag_block_matches_the_host_fixture) {
    /* The diag block of the vector: bytes 11..20 of the report. */
    static const uint8_t vector_diag[WLS_DIAG_ENC_SIZE] = {0x02, 0x04, 0x04, 0x03, 0x01, 0x02, 0x01, 0x00, 0x01, 0x10};

    wls_diag_t d;
    ASSERT_TRUE(wls_diag_decode(vector_diag, &d));

    EXPECT_EQ(0x02, d.switch_pos); // BT
    EXPECT_EQ(0x04, d.devs);       // DEVS_BT4
    EXPECT_EQ(0x04, d.host);       // BLUETOOTH
    EXPECT_EQ(0x03, d.boot_count);
    EXPECT_EQ(0x01, d.usb_line);
    EXPECT_EQ(0x02, d.link_state); // MD_STATE_CONNECTED
    EXPECT_EQ(0x01, d.pin_c14);
    EXPECT_EQ(0x00, d.pin_c15);
    EXPECT_EQ(0x01, d.pin_b9);
    EXPECT_EQ(0x10, d.reset_cause); // bit 4 -> PORRSTF

    /* And the encoder reproduces the vector byte for byte, so the fixture cannot
     * stay green while `wls_diag_encode` moves an index. */
    uint8_t re_encoded[WLS_DIAG_ENC_SIZE] = {0};
    EXPECT_EQ(WLS_DIAG_ENC_SIZE, wls_diag_encode(&d, re_encoded));
    for (uint8_t i = 0; i < WLS_DIAG_ENC_SIZE; i++) {
        EXPECT_EQ(vector_diag[i], re_encoded[i]) << "byte " << (int)i;
    }
}

} // namespace
