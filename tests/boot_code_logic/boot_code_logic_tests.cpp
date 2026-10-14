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

/* Proof of the boot-code codec and sequencer, without hardware.
 *
 * The code exists because the diagnostic seam that rides the USB line is
 * unreachable exactly when the line is down. These vectors pin the mapping
 * (facts -> (amber, white)) and the played timing, so the post-mortem is read
 * from a tested specification rather than counted by eye -- which is what the
 * previous instrument got wrong.
 */

#include "gtest/gtest.h"

extern "C" {
#include "keyboards/epomaker/epomaker_split65/wls/boot_code_logic.h"
}

namespace {

wls_boot_code_t code(uint8_t stage, uint8_t c) {
    wls_boot_code_t v = {stage, c};
    return v;
}

/* Count how many lit pulses of `want_r/g/b` appear in one frame, by sampling.
 * `origin_ms` is the instant the code started -- passed explicitly because the
 * phase is a function of (origin, now), not of `now` alone. */
int lit_pulses_of(uint8_t r, uint8_t g, uint8_t b, wls_boot_code_counts_t counts, uint32_t origin_ms) {
    uint32_t frame = wls_boot_code_frame_ms(counts);
    int      n     = 0;
    bool     was   = false;
    for (uint32_t t = 0; t < frame; t++) {
        wls_boot_code_out_t o  = wls_boot_code_at(counts, origin_ms, origin_ms + t);
        bool                on = (o.phase == WLS_BOOT_CODE_PHASE_LIT) && o.r == r && o.g == g && o.b == b;
        if (on && !was) {
            n++;
        }
        was = on;
    }
    return n;
}

} // namespace

/* --- the codec ----------------------------------------------------------- */

TEST(BootCodeLogic, healthy_is_no_amber_and_a_readable_white_group) {
    wls_boot_code_counts_t c = wls_boot_code_counts(code(WLS_BOOT_STAGE_OK, 3));
    EXPECT_EQ(0, c.amber);
    EXPECT_EQ(4, c.white); /* code 3 speaks 3+1 whites */
}

TEST(BootCodeLogic, a_fault_stage_speaks_that_many_amber) {
    EXPECT_EQ(1, wls_boot_code_counts(code(WLS_BOOT_STAGE_PREINIT, 0)).amber);
    EXPECT_EQ(2, wls_boot_code_counts(code(WLS_BOOT_STAGE_BOOT_PASS, 0)).amber);
    EXPECT_EQ(3, wls_boot_code_counts(code(WLS_BOOT_STAGE_TRANSPORT, 0)).amber);
    EXPECT_EQ(4, wls_boot_code_counts(code(WLS_BOOT_STAGE_ROLE, 0)).amber);
    EXPECT_EQ(5, wls_boot_code_counts(code(WLS_BOOT_STAGE_INCOMPLETE, 0)).amber);
}

/* A white group is never empty inside a spoken code: zero would be
 * indistinguishable from a missing delimiter, and a group of pulses is the
 * only thing that delimits. Code 0 therefore speaks one white. */
TEST(BootCodeLogic, a_zero_code_still_speaks_one_white) {
    EXPECT_EQ(1, wls_boot_code_counts(code(WLS_BOOT_STAGE_TRANSPORT, 0)).white);
}

/* A pathological value cannot make the code unreadably long: the counts clamp. */
TEST(BootCodeLogic, a_group_clamps_to_the_maximum) {
    wls_boot_code_counts_t c = wls_boot_code_counts(code(200, 200));
    EXPECT_EQ(WLS_BOOT_CODE_MAX_GROUP, c.amber);
    EXPECT_EQ(WLS_BOOT_CODE_MAX_GROUP, c.white);
}

/* --- the frame arithmetic ------------------------------------------------ */

TEST(BootCodeLogic, frame_length_is_both_pulse_trains_plus_both_gaps) {
    wls_boot_code_counts_t c      = {2, 3};
    uint32_t               expect = (2u * 2u * WLS_BOOT_CODE_PULSE_MS) + WLS_BOOT_CODE_GROUP_GAP_MS + (3u * 2u * WLS_BOOT_CODE_PULSE_MS) + WLS_BOOT_CODE_FRAME_GAP_MS;
    EXPECT_EQ(expect, wls_boot_code_frame_ms(c));
}

/* --- the played timing --------------------------------------------------- */

/* The count carries the value: a code speaks exactly as many lit pulses per
 * group as the codec says. */
TEST(BootCodeLogic, plays_exactly_the_counted_pulses) {
    wls_boot_code_counts_t c = {2, 3};
    EXPECT_EQ(2, lit_pulses_of(WLS_BOOT_CODE_AMBER_R, WLS_BOOT_CODE_AMBER_G, WLS_BOOT_CODE_AMBER_B, c, 0));
    EXPECT_EQ(3, lit_pulses_of(WLS_BOOT_CODE_WHITE_R, WLS_BOOT_CODE_WHITE_G, WLS_BOOT_CODE_WHITE_B, c, 0));
}

/* A pulse is lit for exactly one pulse width, then dark for one. */
TEST(BootCodeLogic, a_pulse_is_lit_for_one_width_then_dark) {
    wls_boot_code_counts_t c = {1, 1};
    EXPECT_EQ(WLS_BOOT_CODE_PHASE_LIT, wls_boot_code_at(c, 0, 0).phase);
    EXPECT_EQ(WLS_BOOT_CODE_PHASE_LIT, wls_boot_code_at(c, 0, WLS_BOOT_CODE_PULSE_MS - 1).phase);
    EXPECT_EQ(WLS_BOOT_CODE_PHASE_DARK, wls_boot_code_at(c, 0, WLS_BOOT_CODE_PULSE_MS).phase);
    EXPECT_EQ(WLS_BOOT_CODE_PHASE_DARK, wls_boot_code_at(c, 0, 2 * WLS_BOOT_CODE_PULSE_MS - 1).phase);
}

/* The first lit pulse is amber, the second group white -- colour partitions the
 * code, and the amber group precedes the white group. */
TEST(BootCodeLogic, amber_precedes_white) {
    wls_boot_code_counts_t c     = {1, 1};
    wls_boot_code_out_t    first = wls_boot_code_at(c, 0, 0);
    EXPECT_EQ(WLS_BOOT_CODE_AMBER_R, first.r);

    uint32_t            white_start = 2u * WLS_BOOT_CODE_PULSE_MS + WLS_BOOT_CODE_GROUP_GAP_MS;
    wls_boot_code_out_t w           = wls_boot_code_at(c, 0, white_start);
    EXPECT_EQ(WLS_BOOT_CODE_PHASE_LIT, w.phase);
    EXPECT_EQ(WLS_BOOT_CODE_WHITE_R, w.r);
}

/* The three time scales are distinct: the intra-code gap is longer than a pulse
 * period, and the frame gap is longer than the intra-code gap. That is what
 * makes the code self-delimiting. */
TEST(BootCodeLogic, the_three_time_scales_are_ordered) {
    EXPECT_LT(2 * WLS_BOOT_CODE_PULSE_MS, WLS_BOOT_CODE_GROUP_GAP_MS);
    EXPECT_LT(WLS_BOOT_CODE_GROUP_GAP_MS, WLS_BOOT_CODE_FRAME_GAP_MS);
}

/* The whole frame gap is dark: nothing is lit in the 3 s pause. */
TEST(BootCodeLogic, the_frame_gap_is_dark) {
    wls_boot_code_counts_t c      = {1, 1};
    uint32_t               gap_at = wls_boot_code_frame_ms(c) - WLS_BOOT_CODE_FRAME_GAP_MS;
    for (uint32_t t = gap_at; t < gap_at + WLS_BOOT_CODE_FRAME_GAP_MS; t++) {
        EXPECT_EQ(WLS_BOOT_CODE_PHASE_DARK, wls_boot_code_at(c, 0, t).phase);
    }
}

/* The frame loops: a paint a whole frame late is still the same phase as one at
 * zero. This is the property the previous instrument lacked (it played once, so
 * a late start was unrecoverable). */
TEST(BootCodeLogic, the_frame_repeats) {
    wls_boot_code_counts_t c = {2, 1};
    uint32_t               f = wls_boot_code_frame_ms(c);
    for (uint32_t t = 0; t < f; t += 37) {
        wls_boot_code_out_t a = wls_boot_code_at(c, 0, t);
        wls_boot_code_out_t b = wls_boot_code_at(c, 0, t + f);
        EXPECT_EQ(a.phase, b.phase);
        EXPECT_EQ(a.r, b.r);
        EXPECT_EQ(a.g, b.g);
        EXPECT_EQ(a.b, b.b);
    }
}

/* The phase depends only on (now - origin), so shifting the origin by a whole
 * frame changes nothing. */
TEST(BootCodeLogic, the_origin_shifts_the_frame_without_changing_it) {
    wls_boot_code_counts_t c = {2, 1};
    uint32_t               f = wls_boot_code_frame_ms(c);
    for (uint32_t t = 0; t < f; t += 41) {
        wls_boot_code_out_t a = wls_boot_code_at(c, 0, t);
        wls_boot_code_out_t b = wls_boot_code_at(c, f * 3, f * 3 + t);
        EXPECT_EQ(a.phase, b.phase);
        EXPECT_EQ(a.r, b.r);
    }
}

/* --- the show window ----------------------------------------------------- */

/* THE DEFECT THIS PINS (2026-10-14): the shell held a 10 000 ms show constant
 * while a `(7,5)` frame is 11 700 ms, so the window ended mid-frame and the code
 * was truncated. On the board that read as five amber instead of seven -- a
 * miscount caused by a bound that disagreed with the thing it bounded.
 *
 * The invariant is EQUALITY, not merely "longer": a longer window lets the next
 * frame's amber group into the count (measured: twelve amber instead of seven),
 * so exactly one frame -- the self-delimiting unit -- is the correct bound. */
TEST(BootCodeLogic, the_show_window_is_exactly_one_frame) {
    for (uint8_t stage = 0; stage <= WLS_BOOT_STAGE_RSTCAUSE; stage++) {
        wls_boot_code_counts_t c = wls_boot_code_counts(code(stage, stage));
        EXPECT_EQ(wls_boot_code_frame_ms(c), wls_boot_code_show_ms(c));
    }
    /* And specifically for the case that was miscounted on hardware. */
    wls_boot_code_counts_t worst = wls_boot_code_counts(wls_boot_code_rstcause(1u << 4));
    EXPECT_EQ(7, worst.amber);
    EXPECT_EQ(5, worst.white);
    EXPECT_EQ(wls_boot_code_show_ms(worst), 11700u);
}

/* A late start must not change the count. However long the RGB pipeline takes to
 * become visible, a viewer sampling from the ORIGIN over the show window counts
 * exactly the code's groups -- because the origin is the first visible frame, so
 * the lead-in is before the code rather than eating its leading pulses. This is
 * the miscount the hardware exposed, as a test. */
TEST(BootCodeLogic, the_count_does_not_depend_on_a_late_first_paint) {
    wls_boot_code_counts_t c     = wls_boot_code_counts(wls_boot_code_rstcause(1u << 4));
    uint32_t               show  = wls_boot_code_show_ms(c);
    uint32_t               frame = wls_boot_code_frame_ms(c);

    /* The origin is the first visible frame; `lead` only moves it. */
    for (uint32_t lead = 0; lead <= 4 * frame; lead += 600) {
        int  amber = 0, white = 0;
        bool was = false;
        for (uint32_t t = lead; t < lead + show; t += 16) {
            wls_boot_code_out_t o  = wls_boot_code_at(c, lead, t);
            bool                on = (o.phase == WLS_BOOT_CODE_PHASE_LIT);
            if (on && !was) {
                if (o.r == WLS_BOOT_CODE_AMBER_R && o.g == WLS_BOOT_CODE_AMBER_G) {
                    amber++;
                } else if (o.r == WLS_BOOT_CODE_WHITE_R) {
                    white++;
                }
            }
            was = on;
        }
        EXPECT_EQ((int)c.amber, amber) << "lead-in " << lead << " ms changed the amber count";
        EXPECT_EQ((int)c.white, white) << "lead-in " << lead << " ms changed the white count";
    }
}

/* The host-visible encode carries the SAME pair the LED plays, in the order the
 * eye counts them: amber first, white second. This is what keeps the two
 * readers (eye and `0xA4`) from drifting. */
TEST(BootCodeLogic, the_encode_carries_the_played_pair) {
    wls_boot_code_counts_t c                           = {3, 2};
    uint8_t                out[WLS_BOOT_CODE_ENC_SIZE] = {0};
    wls_boot_code_encode(c, out);
    EXPECT_EQ(3, out[0]);
    EXPECT_EQ(2, out[1]);
}

/* The encode of a clamped code matches what the sequencer would play, so a host
 * never counts a group the LED will not show. */
TEST(BootCodeLogic, the_encode_matches_the_clamped_counts) {
    wls_boot_code_counts_t c                           = wls_boot_code_counts((wls_boot_code_t){WLS_BOOT_STAGE_TRANSPORT, 200});
    uint8_t                out[WLS_BOOT_CODE_ENC_SIZE] = {0};
    wls_boot_code_encode(c, out);
    EXPECT_EQ(c.amber, out[0]);
    EXPECT_EQ(c.white, out[1]);
    EXPECT_EQ(WLS_BOOT_CODE_MAX_GROUP, out[1]);
}

/* The boot-count stage is how a reset loop is proved on the LED: six amber, then
 * `n` white for boot number `n`. The white group must equal the count exactly, so
 * a reader watching successive boots sees the number climb. */
TEST(BootCodeLogic, the_boot_count_stage_speaks_the_count_as_whites) {
    wls_boot_code_counts_t one   = wls_boot_code_counts(wls_boot_code_bootcount(1));
    wls_boot_code_counts_t three = wls_boot_code_counts(wls_boot_code_bootcount(3));
    EXPECT_EQ(WLS_BOOT_STAGE_BOOTCOUNT, one.amber);
    EXPECT_EQ(1, one.white);
    EXPECT_EQ(WLS_BOOT_STAGE_BOOTCOUNT, three.amber);
    EXPECT_EQ(3, three.white);
}

/* A count of zero cannot happen (`wls_note_boot()` runs before the first paint),
 * but the encode must not produce a zero-length white group if it somehow did --
 * zero whites is indistinguishable from a missing delimiter. It reads as 1. */
TEST(BootCodeLogic, a_zero_boot_count_still_speaks_one_white) {
    wls_boot_code_counts_t c = wls_boot_code_counts(wls_boot_code_bootcount(0));
    EXPECT_EQ(WLS_BOOT_STAGE_BOOTCOUNT, c.amber);
    EXPECT_EQ(1, c.white);
}

/* A runaway loop clamps at the maximum: the count pins at 9 rather than making an
 * unreadable frame. Seeing the pin IS the reading -- it says "still looping". */
TEST(BootCodeLogic, a_runaway_boot_count_clamps_at_the_maximum) {
    wls_boot_code_counts_t c = wls_boot_code_counts(wls_boot_code_bootcount(250));
    EXPECT_EQ(WLS_BOOT_STAGE_BOOTCOUNT, c.amber);
    EXPECT_EQ(WLS_BOOT_CODE_MAX_GROUP, c.white);
}

/* The reset cause is a stage, so its amber count is the stage number and its
 * white count names the cause. The six named bits map to white 1..6 in the CMSIS
 * header's own bit order, so a reader can look them up. */
TEST(BootCodeLogic, each_named_reset_cause_speaks_its_own_white) {
    struct {
        uint32_t bit;
        uint8_t  white;
    } cases[] = {
        {1u << 0, 1}, /* low-power      */
        {1u << 1, 2}, /* window wdt     */
        {1u << 2, 3}, /* independent wdt*/
        {1u << 3, 4}, /* software       */
        {1u << 4, 5}, /* power-on       */
        {1u << 5, 6}, /* external pin   */
    };
    for (auto &tc : cases) {
        wls_boot_code_counts_t c = wls_boot_code_counts(wls_boot_code_rstcause(tc.bit));
        EXPECT_EQ(WLS_BOOT_STAGE_RSTCAUSE, c.amber);
        EXPECT_EQ(tc.white, c.white);
    }
}

/* If more than one cause bit is latched at once (it should not be, for a single
 * reset), the LOWEST set bit wins -- a deterministic reading rather than a
 * synthesis, so the operator still sees one of the named causes. */
TEST(BootCodeLogic, the_lowest_set_cause_bit_wins) {
    wls_boot_code_counts_t c = wls_boot_code_counts(wls_boot_code_rstcause((1u << 2) | (1u << 4)));
    EXPECT_EQ(WLS_BOOT_STAGE_RSTCAUSE, c.amber);
    EXPECT_EQ(3, c.white); /* the independent-watchdog bit, position 2 -> white 3 */
}

/* Nothing latched (or only bits above the six we name) must still speak a
 * non-zero group -- a spoken code never has a zero-length group -- and it must
 * not collide with a real cause, so it uses the reserved maximum. */
TEST(BootCodeLogic, an_unlatched_or_unknown_cause_still_speaks) {
    wls_boot_code_counts_t none = wls_boot_code_counts(wls_boot_code_rstcause(0));
    EXPECT_EQ(WLS_BOOT_STAGE_RSTCAUSE, none.amber);
    EXPECT_EQ(WLS_BOOT_CODE_MAX_GROUP, none.white);

    wls_boot_code_counts_t unknown = wls_boot_code_counts(wls_boot_code_rstcause(1u << 7));
    EXPECT_EQ(WLS_BOOT_STAGE_RSTCAUSE, unknown.amber);
    EXPECT_EQ(WLS_BOOT_CODE_MAX_GROUP, unknown.white);
}

/* --- the stage precedence (`wls_boot_facts`) ----------------------------- */

/* This rule IS the instrument's contract: it decides what every reading means.
 * It used to live in `wls/wls.c` (the composition root), where no test could
 * reach it -- the last decision in the board glue that was untestable. These
 * vectors pin every branch, in precedence order. */

static wls_boot_inputs_t inputs(bool master, bool settled, uint8_t host) {
    wls_boot_inputs_t in = {master, settled, host};
    return in;
}

/* A healthy boot: master, settled, a real transport. No amber; the white group
 * names the transport so a WORKING board is also readable. */
TEST(BootCodeLogic, boot_facts_healthy_names_the_transport) {
    EXPECT_EQ(WLS_BOOT_STAGE_OK, wls_boot_facts(inputs(true, true, WLS_BOOT_HOST_USB), 0, 1).stage);
    EXPECT_EQ(1, wls_boot_facts(inputs(true, true, WLS_BOOT_HOST_USB), 0, 1).code);
    EXPECT_EQ(2, wls_boot_facts(inputs(true, true, WLS_BOOT_HOST_BLUETOOTH), 0, 1).code);
    EXPECT_EQ(3, wls_boot_facts(inputs(true, true, WLS_BOOT_HOST_2P4GHZ), 0, 1).code);
}

/* A latched reset cause outranks everything -- including a boot that looks
 * healthy, and including a boot count of 1. UNGATED on purpose: the cause latch
 * is hardware and survives the reset it describes, so gating on the RAM boot
 * counter would disable the reading on exactly the boots it exists to measure. */
TEST(BootCodeLogic, boot_facts_latched_cause_outranks_everything) {
    wls_boot_code_t c = wls_boot_facts(inputs(true, true, WLS_BOOT_HOST_USB), 1u << 4, 1);
    EXPECT_EQ(WLS_BOOT_STAGE_RSTCAUSE, c.stage);
    EXPECT_EQ(4, c.code); /* bit 4 -> white 5, code 4 */
}

/* With no latched cause, a boot count above 1 reports the counter. */
TEST(BootCodeLogic, boot_facts_reports_a_boot_count_above_one) {
    wls_boot_code_t c = wls_boot_facts(inputs(true, true, WLS_BOOT_HOST_USB), 0, 3);
    EXPECT_EQ(WLS_BOOT_STAGE_BOOTCOUNT, c.stage);
    EXPECT_EQ(2, c.code); /* count 3 -> code 2 -> white 3 */
}

/* The very first boot (count 1) is not itself a fault, so it falls through to
 * the transport reading rather than speaking BOOTCOUNT. */
TEST(BootCodeLogic, boot_facts_first_boot_is_not_a_count_fault) {
    EXPECT_NE(WLS_BOOT_STAGE_BOOTCOUNT, wls_boot_facts(inputs(true, true, WLS_BOOT_HOST_USB), 0, 1).stage);
}

/* The slave half: its own keys do not type. This outranks the transport read. */
TEST(BootCodeLogic, boot_facts_slave_speaks_role) {
    wls_boot_code_t c = wls_boot_facts(inputs(false, true, WLS_BOOT_HOST_USB), 0, 1);
    EXPECT_EQ(WLS_BOOT_STAGE_ROLE, c.stage);
    EXPECT_EQ(1, c.code);
}

/* An unreadable switch (not settled) is a BOOT_PASS fault, and it outranks the
 * transport read (nothing was applied, so any retained host is stale). */
TEST(BootCodeLogic, boot_facts_unsettled_switch_speaks_boot_pass) {
    wls_boot_code_t c = wls_boot_facts(inputs(true, false, WLS_BOOT_HOST_USB), 0, 1);
    EXPECT_EQ(WLS_BOOT_STAGE_BOOT_PASS, c.stage);
    EXPECT_EQ(1, c.code);
}

/* Settled, master, but no transport chosen at all: the value is not one of the
 * three real transports. */
TEST(BootCodeLogic, boot_facts_no_transport_speaks_transport) {
    wls_boot_code_t c = wls_boot_facts(inputs(true, true, 0), 0, 1);
    EXPECT_EQ(WLS_BOOT_STAGE_TRANSPORT, c.stage);
    EXPECT_EQ(3, c.code);
}

/* The precedence is total and ordered: with several conditions true at once, the
 * highest-precedence one wins. This is the property that made the rule worth
 * extracting -- it is a total function over the inputs, so it can be exhausted. */
TEST(BootCodeLogic, boot_facts_precedence_order) {
    /* cause > bootcount */
    EXPECT_EQ(WLS_BOOT_STAGE_RSTCAUSE, wls_boot_facts(inputs(false, false, 0), 1u << 3, 9).stage);
    /* bootcount > role */
    EXPECT_EQ(WLS_BOOT_STAGE_BOOTCOUNT, wls_boot_facts(inputs(false, false, 0), 0, 9).stage);
    /* role > boot_pass */
    EXPECT_EQ(WLS_BOOT_STAGE_ROLE, wls_boot_facts(inputs(false, false, 0), 0, 1).stage);
    /* boot_pass > transport */
    EXPECT_EQ(WLS_BOOT_STAGE_BOOT_PASS, wls_boot_facts(inputs(true, false, 0), 0, 1).stage);
    /* transport is last */
    EXPECT_EQ(WLS_BOOT_STAGE_TRANSPORT, wls_boot_facts(inputs(true, true, 0), 0, 1).stage);
}

/* The golden vector shared with `host/battery_polybar.py`.
 *
 * The 0xA4 report is written in C and read in Python; nothing fails when the two
 * disagree. This pins the boot-code pair (report bytes 21..22) to the same literal
 * the host asserts. See `docs/0xA4-golden-vector.md`. */
TEST(BootCodeLogic, golden_vector_boot_code_pair_matches_the_host_fixture) {
    /* Reset cause bit 4 (PORRSTF) -> stage RSTCAUSE, white 5. */
    wls_boot_code_t        c      = wls_boot_code_rstcause(1u << 4);
    wls_boot_code_counts_t counts = wls_boot_code_counts(c);
    EXPECT_EQ(7, counts.amber);
    EXPECT_EQ(5, counts.white);

    uint8_t out[WLS_BOOT_CODE_ENC_SIZE] = {0};
    wls_boot_code_encode(counts, out);
    EXPECT_EQ(0x07, out[0]);
    EXPECT_EQ(0x05, out[1]);
}
