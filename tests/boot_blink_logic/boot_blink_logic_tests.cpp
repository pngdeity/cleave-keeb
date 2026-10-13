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

/* Proof of the boot-report blink sequencer, without hardware.
 *
 * The sequencer plays a fixed list of pulse counts one step at a time. These
 * vectors pin: an idle sequencer does nothing; a started one walks its steps in
 * order; each pulse alternates lit/dark; each step lasts `count` pulses; and the
 * schedule ends with exactly one DONE and never restarts itself.
 */

#include "gtest/gtest.h"

extern "C" {
#include "keyboards/epomaker/epomaker_split65/wls/boot_blink_logic.h"
}

namespace {

TEST(BootBlinkLogic, idle_sequencer_does_nothing) {
    wls_boot_blink_t            s   = WLS_BOOT_BLINK_IDLE;
    const uint8_t               steps[3] = {1, 1, 3};
    wls_boot_blink_out_t out = wls_boot_blink_tick(&s, 3, steps, 12345);
    EXPECT_EQ(WLS_BOOT_BLINK_IDLE_STATE, out.kind);
}

TEST(BootBlinkLogic, first_pulse_of_step_zero_is_lit) {
    wls_boot_blink_t s = WLS_BOOT_BLINK_IDLE;
    wls_boot_blink_start(&s, 1000);

    const uint8_t        steps[3] = {1, 1, 3};
    wls_boot_blink_out_t out      = wls_boot_blink_tick(&s, 3, steps, 1000);
    EXPECT_EQ(WLS_BOOT_BLINK_SHOW, out.kind);
    EXPECT_EQ(0, out.step);
    EXPECT_TRUE(out.pulse_on);
}

TEST(BootBlinkLogic, pulses_alternate_lit_and_dark) {
    wls_boot_blink_t s = WLS_BOOT_BLINK_IDLE;
    wls_boot_blink_start(&s, 0);

    const uint8_t steps[1] = {4}; /* four pulses: on, off, on, off */

    wls_boot_blink_out_t out = wls_boot_blink_tick(&s, 1, steps, 0 * WLS_BOOT_BLINK_STEP_MS);
    EXPECT_TRUE(out.pulse_on);
    out = wls_boot_blink_tick(&s, 1, steps, 1 * WLS_BOOT_BLINK_STEP_MS);
    EXPECT_FALSE(out.pulse_on);
    out = wls_boot_blink_tick(&s, 1, steps, 2 * WLS_BOOT_BLINK_STEP_MS);
    EXPECT_TRUE(out.pulse_on);
    out = wls_boot_blink_tick(&s, 1, steps, 3 * WLS_BOOT_BLINK_STEP_MS);
    EXPECT_FALSE(out.pulse_on);
}

TEST(BootBlinkLogic, steps_play_in_order_and_each_lasts_its_count) {
    wls_boot_blink_t s = WLS_BOOT_BLINK_IDLE;
    wls_boot_blink_start(&s, 0);

    const uint8_t steps[3] = {2, 1, 3}; /* step0: 2 pulses, step1: 1, step2: 3 */

    /* Pulses 0..1 -> step 0; pulse 2 -> step 1; pulses 3..5 -> step 2. */
    const uint8_t expect_step[6] = {0, 0, 1, 2, 2, 2};
    for (uint8_t i = 0; i < 6; i++) {
        wls_boot_blink_out_t out = wls_boot_blink_tick(&s, 3, steps, i * WLS_BOOT_BLINK_STEP_MS);
        EXPECT_EQ(WLS_BOOT_BLINK_SHOW, out.kind) << "pulse " << (int)i;
        EXPECT_EQ(expect_step[i], out.step) << "pulse " << (int)i;
    }
}

TEST(BootBlinkLogic, zero_count_step_still_shows_one_pulse) {
    wls_boot_blink_t s = WLS_BOOT_BLINK_IDLE;
    wls_boot_blink_start(&s, 0);

    const uint8_t steps[2] = {0, 1}; /* step 0 asks for zero pulses */

    /* Step 0 must still occupy one pulse so the field is visible, then step 1. */
    wls_boot_blink_out_t out = wls_boot_blink_tick(&s, 2, steps, 0);
    EXPECT_EQ(WLS_BOOT_BLINK_SHOW, out.kind);
    EXPECT_EQ(0, out.step);

    out = wls_boot_blink_tick(&s, 2, steps, 1 * WLS_BOOT_BLINK_STEP_MS);
    EXPECT_EQ(WLS_BOOT_BLINK_SHOW, out.kind);
    EXPECT_EQ(1, out.step);
}

TEST(BootBlinkLogic, finishes_with_one_done_then_goes_idle) {
    wls_boot_blink_t s = WLS_BOOT_BLINK_IDLE;
    wls_boot_blink_start(&s, 0);

    const uint8_t steps[1] = {1}; /* one pulse total */

    wls_boot_blink_out_t out = wls_boot_blink_tick(&s, 1, steps, 0);
    EXPECT_EQ(WLS_BOOT_BLINK_SHOW, out.kind);

    /* One pulse in, the schedule is over: DONE exactly once. */
    out = wls_boot_blink_tick(&s, 1, steps, 1 * WLS_BOOT_BLINK_STEP_MS);
    EXPECT_EQ(WLS_BOOT_BLINK_DONE, out.kind);

    /* And it stays idle (never restarts itself). */
    out = wls_boot_blink_tick(&s, 1, steps, 5 * WLS_BOOT_BLINK_STEP_MS);
    EXPECT_EQ(WLS_BOOT_BLINK_IDLE_STATE, out.kind);
}

TEST(BootBlinkLogic, empty_schedule_does_nothing_even_when_started) {
    wls_boot_blink_t s = WLS_BOOT_BLINK_IDLE;
    wls_boot_blink_start(&s, 0);
    wls_boot_blink_out_t out = wls_boot_blink_tick(&s, 0, NULL, 0);
    EXPECT_EQ(WLS_BOOT_BLINK_IDLE_STATE, out.kind);
}

} // namespace
