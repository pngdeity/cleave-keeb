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

/* Byte-by-byte proof of the module UART frame grammar, without hardware.
 *
 * The framing rule the receive path must hold: a frame starts only on a known
 * command byte; a fixed command frame is exactly 3 bytes ([cmd][b1][checksum]);
 * a RAW pair (`AF 60`) declares its length and completes after that many bytes
 * following the header; anything else at a start resyncs. These vectors pin the
 * grammar down so the shell's interpretation can never be blamed for a framing
 * error (and vice versa). */

#include "gtest/gtest.h"

extern "C" {
#include "keyboards/linker/wireless/md_frame.h"
}

namespace {

/* Feed a whole byte sequence, returning the result of the *final* byte (or a
 * completed=false result if the sequence did not finish a frame). */
md_frame_result_t feed_all(md_frame_state_t *st, const uint8_t *bytes, size_t n) {
    md_frame_result_t last = {false, MD_FRAME_NONE, 0};
    for (size_t i = 0; i < n; i++) {
        last = md_frame_feed(st, bytes[i]);
    }
    return last;
}

TEST(MdFrame, AckIsAThreeByteFrameOfItsOwnKind) {
    md_frame_state_t  st    = {};
    const uint8_t     ack[] = {0x61, 0x0D, 0x0A};
    md_frame_result_t r     = feed_all(&st, ack, sizeof(ack));
    EXPECT_TRUE(r.complete);
    EXPECT_EQ(MD_FRAME_ACK, r.kind);
    EXPECT_EQ(3, r.length);
}

TEST(MdFrame, FixedCommandFrameIsExactlyThreeBytes) {
    md_frame_state_t st = {};
    /* e.g. BATVOL `5C <percent> <sum>`. */
    const uint8_t bytes[] = {0x5C, 0x64, 0x1C};
    EXPECT_FALSE(md_frame_feed(&st, bytes[0]).complete);
    EXPECT_FALSE(md_frame_feed(&st, bytes[1]).complete);
    md_frame_result_t r = md_frame_feed(&st, bytes[2]);
    EXPECT_TRUE(r.complete);
    EXPECT_EQ(MD_FRAME_CMD, r.kind);
    EXPECT_EQ(3, r.length);
}

TEST(MdFrame, RawFrameCompletesAfterItsDeclaredLength) {
    md_frame_state_t st = {};
    /* AF 60 <len=2> <d0> <d1> <sum> -> 6 bytes total. */
    const uint8_t     bytes[] = {0xAF, 0x60, 0x02, 0xAA, 0xBB, 0x00};
    md_frame_result_t r       = feed_all(&st, bytes, sizeof(bytes));
    EXPECT_TRUE(r.complete);
    EXPECT_EQ(MD_FRAME_RAW, r.kind);
    EXPECT_EQ(6, r.length);
}

TEST(MdFrame, RawZeroLengthStillCarriesItsChecksum) {
    md_frame_state_t st = {};
    /* AF 60 00 <sum> -> 4 bytes. */
    const uint8_t     bytes[] = {0xAF, 0x60, 0x00, 0x00};
    md_frame_result_t r       = feed_all(&st, bytes, sizeof(bytes));
    EXPECT_TRUE(r.complete);
    EXPECT_EQ(MD_FRAME_RAW, r.kind);
    EXPECT_EQ(4, r.length);
}

TEST(MdFrame, UnknownByteAtStartIsDiscardedAndResyncs) {
    md_frame_state_t st = {};
    EXPECT_FALSE(md_frame_feed(&st, 0x00).complete);
    EXPECT_FALSE(md_frame_feed(&st, 0xFF).complete);
    /* Still idle, so a real frame now frames correctly. */
    const uint8_t     bytes[] = {0x5B, 0x32, 0x00};
    md_frame_result_t r       = feed_all(&st, bytes, sizeof(bytes));
    EXPECT_TRUE(r.complete);
    EXPECT_EQ(MD_FRAME_CMD, r.kind);
}

TEST(MdFrame, BackToBackFramesOnOneStream) {
    md_frame_state_t st = {};
    /* A BATVOL then a DEVCTRL, no gap. */
    const uint8_t     bytes[] = {0x5C, 0x64, 0x1C, 0x5B, 0x32, 0x00};
    md_frame_result_t r1      = md_frame_feed(&st, bytes[0]);
    r1                        = md_frame_feed(&st, bytes[1]);
    r1                        = md_frame_feed(&st, bytes[2]);
    ASSERT_TRUE(r1.complete);
    md_frame_result_t r2 = md_frame_feed(&st, bytes[3]);
    EXPECT_FALSE(r2.complete);
    r2 = md_frame_feed(&st, bytes[4]);
    EXPECT_FALSE(r2.complete);
    r2 = md_frame_feed(&st, bytes[5]);
    EXPECT_TRUE(r2.complete);
    EXPECT_EQ(MD_FRAME_CMD, r2.kind);
    EXPECT_EQ(3, r2.length);
}

TEST(MdFrame, RawIsDistinguishedFromAFixedCommandByItsSecondByte) {
    /* `AF` alone is a fixed 3-byte command (MD_REV_CMD_RAW as a lone lead); only
     * `AF 60` opens the variable-length grammar. */
    md_frame_state_t  st_a    = {};
    const uint8_t     fixed[] = {0xAF, 0x11, 0x00};
    md_frame_result_t ra      = feed_all(&st_a, fixed, sizeof(fixed));
    EXPECT_TRUE(ra.complete);
    EXPECT_EQ(MD_FRAME_CMD, ra.kind);

    md_frame_state_t  st_b  = {};
    const uint8_t     raw[] = {0xAF, 0x60, 0x01, 0x77, 0x00};
    md_frame_result_t rb    = feed_all(&st_b, raw, sizeof(raw));
    EXPECT_TRUE(rb.complete);
    EXPECT_EQ(MD_FRAME_RAW, rb.kind);
    EXPECT_EQ(5, rb.length);
}

TEST(MdFrame, KnownCommandSetMatchesTheGrammar) {
    EXPECT_TRUE(md_frame_is_known_cmd(0x61)); /* ACK lead */
    EXPECT_TRUE(md_frame_is_known_cmd(0xAF)); /* RAW */
    EXPECT_TRUE(md_frame_is_known_cmd(0x5A)); /* INDICATOR */
    EXPECT_TRUE(md_frame_is_known_cmd(0x5B)); /* DEVCTRL */
    EXPECT_TRUE(md_frame_is_known_cmd(0x5C)); /* BATVOL */
    EXPECT_TRUE(md_frame_is_known_cmd(0x5D)); /* FW_VERSION */
    EXPECT_TRUE(md_frame_is_known_cmd(0x60)); /* HOST_STATE */
    EXPECT_FALSE(md_frame_is_known_cmd(0x00));
    EXPECT_FALSE(md_frame_is_known_cmd(0xFF));
    EXPECT_FALSE(md_frame_is_known_cmd(0x5F));
}

} // namespace
