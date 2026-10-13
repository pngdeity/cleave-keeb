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

/* Proof of the boot-report codec and blink schedule, without hardware.
 *
 * The report exists because the primary diagnostic seam (the 0xA4 reply) needs
 * the USB line up, and the fault it diagnoses is the line going down. These
 * vectors pin the wire format and the visible schedule, so the post-mortem is
 * read from a tested mapping rather than counted by eye.
 */

#include "gtest/gtest.h"

extern "C" {
#include "keyboards/epomaker/epomaker_split65/wls/boot_report_logic.h"
}

namespace {

wls_boot_report_t report(uint8_t switch_pos, uint8_t host, bool settled) {
    wls_boot_report_t r = {switch_pos, host, settled};
    return r;
}

TEST(BootReportLogic, encode_writes_the_fixed_layout) {
    uint8_t           buf[WLS_BOOT_REPORT_ENC_SIZE] = {0};
    wls_boot_report_t r                             = report(1, 3, true);
    EXPECT_EQ(WLS_BOOT_REPORT_ENC_SIZE, wls_boot_report_encode(&r, buf));
    EXPECT_EQ(1, buf[0]); /* switch_pos */
    EXPECT_EQ(3, buf[1]); /* host */
    EXPECT_EQ(0x80, buf[2]);
}

TEST(BootReportLogic, decode_is_the_inverse_of_encode) {
    for (uint8_t pos = 0; pos <= 3; pos++) {
        for (uint8_t host = 0; host <= 5; host++) {
            for (int s = 0; s <= 1; s++) {
                wls_boot_report_t src                           = report(pos, host, s != 0);
                uint8_t           buf[WLS_BOOT_REPORT_ENC_SIZE] = {0};
                wls_boot_report_t got                           = {0, 0, false};
                wls_boot_report_encode(&src, buf);
                ASSERT_TRUE(wls_boot_report_decode(buf, &got));
                EXPECT_EQ(src.switch_pos, got.switch_pos);
                EXPECT_EQ(src.host, got.host);
                EXPECT_EQ(src.settled, got.settled);
            }
        }
    }
}

TEST(BootReportLogic, decode_rejects_null) {
    wls_boot_report_t out = {0, 0, false};
    EXPECT_FALSE(wls_boot_report_decode(nullptr, &out));
    uint8_t buf[WLS_BOOT_REPORT_ENC_SIZE] = {0};
    EXPECT_FALSE(wls_boot_report_decode(buf, nullptr));
}

TEST(BootReportLogic, switch_step_count_encodes_position_plus_one) {
    /* A zero value (UNKNOWN position) must still be visible: count = pos + 1. */
    wls_boot_report_t p0 = report(0, 0, false);
    wls_boot_report_t p1 = report(1, 0, false);
    wls_boot_report_t p2 = report(2, 0, false);
    wls_boot_report_t p3 = report(3, 0, false);
    EXPECT_EQ(1, wls_boot_report_blink_step(&p0, 0).count);
    EXPECT_EQ(2, wls_boot_report_blink_step(&p1, 0).count);
    EXPECT_EQ(3, wls_boot_report_blink_step(&p2, 0).count);
    EXPECT_EQ(4, wls_boot_report_blink_step(&p3, 0).count);
}

TEST(BootReportLogic, unreadable_switch_shows_red_and_three_pulses) {
    /* The case that matters: an unsettled read. Step 0 is RED, step 2 is the
     * three-pulse "not settled" marker. */
    wls_boot_report_t r = report(0, 0, false);
    EXPECT_EQ(0xFF, wls_boot_report_blink_step(&r, 0).r);
    EXPECT_EQ(0x00, wls_boot_report_blink_step(&r, 0).g);
    EXPECT_EQ(0x00, wls_boot_report_blink_step(&r, 0).b);
    EXPECT_EQ(3, wls_boot_report_blink_step(&r, 2).count);
    EXPECT_EQ(0xFF, wls_boot_report_blink_step(&r, 2).r);
    EXPECT_EQ(0x00, wls_boot_report_blink_step(&r, 2).g);
}

TEST(BootReportLogic, settled_shows_a_single_white_pulse) {
    wls_boot_report_t     r = report(1, 3, true);
    wls_boot_blink_step_t s = wls_boot_report_blink_step(&r, 2);
    EXPECT_EQ(1, s.count);
    EXPECT_EQ(0xFF, s.r);
    EXPECT_EQ(0xFF, s.g);
    EXPECT_EQ(0xFF, s.b);
}

TEST(BootReportLogic, out_of_range_step_is_a_skip) {
    wls_boot_report_t r = report(1, 3, true);
    EXPECT_EQ(0, wls_boot_report_blink_step(&r, WLS_BOOT_REPORT_BLINK_STEPS).count);
    EXPECT_EQ(0, wls_boot_report_blink_step(&r, 200).count);
}

} // namespace
