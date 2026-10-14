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

/* Proof of the jump-journal encoder, without hardware.
 *
 * The journal is how a boot tells whether *our* `QK_BOOT` path ran: the magic
 * word itself is cleared before our code runs, so the only surviving evidence is
 * what we wrote to `.ram0`. The encoder is the wire format both the firmware and
 * `host/battery_polybar.py` must agree on, so it is pinned here exactly (the
 * firmware half of the cross-language fixture -- see
 * docs/0xA4-golden-vector.md).
 */

#include "gtest/gtest.h"

extern "C" {
#include "keyboards/epomaker/epomaker_split65/wls/jump_journal_logic.h"
}

namespace {

/* The golden vector shared with `host/battery_polybar.py` (report bytes 23..31):
 * presence 1, version 1, generation 9, stage 0, code 0, reset_cause 0x10,
 * uptime_ms 0x00B4D2 (46354). The host asserts the same literal decodes to these
 * fields; this asserts the encoder produces them. */
TEST(WlsJumpJournal, golden_vector_journal_block_matches_the_host_fixture) {
    uint8_t out[WLS_JUMP_JOURNAL_ENC_SIZE] = {0};

    wls_jump_journal_encode_parts(WLS_JUMP_JOURNAL_MAGIC, 1, 9, 0, 0, 0x00000010u, 0x00B4D2u, out);

    EXPECT_EQ(0x01, out[0]); /* presence */
    EXPECT_EQ(0x01, out[1]); /* version */
    EXPECT_EQ(0x09, out[2]); /* generation */
    EXPECT_EQ(0x00, out[3]); /* stage */
    EXPECT_EQ(0x00, out[4]); /* code */
    EXPECT_EQ(0x10, out[5]); /* reset_cause (low byte; a six-bit latch) */
    EXPECT_EQ(0xD2, out[6]); /* uptime_ms LE */
    EXPECT_EQ(0xB4, out[7]);
    EXPECT_EQ(0x00, out[8]);
}

/* The presence flag is the discriminator: a journal field that does not carry
 * the magic must encode as absent, so the host can tell "our jump ran" from "a
 * stale or zeroed record". */
TEST(WlsJumpJournal, presence_flag_is_set_only_by_the_magic) {
    uint8_t out[WLS_JUMP_JOURNAL_ENC_SIZE] = {0};

    wls_jump_journal_encode_parts(WLS_JUMP_JOURNAL_MAGIC, 1, 1, 0, 0, 0, 0, out);
    EXPECT_EQ(0x01, out[0]);

    wls_jump_journal_encode_parts(0u, 1, 1, 0, 0, 0, 0, out);
    EXPECT_EQ(0x00, out[0]);

    wls_jump_journal_encode_parts(0xDEADBEEFu, 1, 1, 0, 0, 0, 0, out);
    EXPECT_EQ(0x00, out[0]);
}

/* The reset cause is reported as its low byte only (the latch has six bits) and
 * the uptime as its low three bytes, each little-endian and independent (a swap
 * in one must not be masked by the other). */
TEST(WlsJumpJournal, byte_fields_are_little_endian) {
    uint8_t out[WLS_JUMP_JOURNAL_ENC_SIZE] = {0};

    wls_jump_journal_encode_parts(WLS_JUMP_JOURNAL_MAGIC, 0x11, 0x22, 0x33, 0x44, 0x11223344u, 0x55667788u, out);

    EXPECT_EQ(0x11, out[1]);
    EXPECT_EQ(0x22, out[2]);
    EXPECT_EQ(0x33, out[3]);
    EXPECT_EQ(0x44, out[4]);
    EXPECT_EQ(0x44, out[5]); /* reset_cause: the low byte */
    EXPECT_EQ(0x88, out[6]); /* uptime_ms LE */
    EXPECT_EQ(0x77, out[7]);
    EXPECT_EQ(0x66, out[8]);
}

} // namespace
