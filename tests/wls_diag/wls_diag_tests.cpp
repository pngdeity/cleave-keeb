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
    wls_diag_t d                      = {1, 2, 3, 4, 5, 6};
    uint8_t    buf[WLS_DIAG_ENC_SIZE] = {0};

    EXPECT_EQ(WLS_DIAG_ENC_SIZE, wls_diag_encode(&d, buf));

    EXPECT_EQ(1, buf[0]); // switch_pos
    EXPECT_EQ(2, buf[1]); // devs
    EXPECT_EQ(3, buf[2]); // host
    EXPECT_EQ(4, buf[3]); // boot_count
    EXPECT_EQ(5, buf[4]); // usb_line
    EXPECT_EQ(6, buf[5]); // link_state
}

TEST(WlsDiag, decode_is_the_inverse_of_encode) {
    wls_diag_t in                     = {3, 6, 5, 42, 0, 2};
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
}

TEST(WlsDiag, round_trip_preserves_every_field_at_the_extremes) {
    wls_diag_t in                     = {0xFF, 0, 0xFF, 0, 1, 0x7F};
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
}

TEST(WlsDiag, decode_rejects_null) {
    wls_diag_t out = {0};
    EXPECT_FALSE(wls_diag_decode(nullptr, &out));
}

} // namespace
