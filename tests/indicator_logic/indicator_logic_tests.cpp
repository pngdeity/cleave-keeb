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

/* Proof of the indicator decisions, without hardware.
 *
 * The three blink loops in the board file were one rule written three times.
 * These vectors pin that rule (interval elapsed -> advance; colour on the odd
 * half-period; DONE when the counter empties) and the battery colour table, so
 * the shell can hold no policy.
 */

#include "gtest/gtest.h"

extern "C" {
#include "keyboards/epomaker/epomaker_split65/wls/indicator_logic.h"
}

namespace {

TEST(IndicatorLogic, idle_flash_paints_nothing_and_stays_idle) {
    wls_blink_t     b   = WLS_BLINK_IDLE;
    wls_blink_out_t out = wls_blink_tick(&b, 1000);
    EXPECT_EQ(WLS_BLINK_PAINT, out.kind);
    EXPECT_FALSE(out.on);
    EXPECT_EQ(0u, b.interval_ms);
}

TEST(IndicatorLogic, arm_sets_two_half_periods_per_blink) {
    wls_blink_t b = WLS_BLINK_IDLE;
    wls_blink_arm(&b, 250, 3, 5000);
    EXPECT_EQ(250u, b.interval_ms);
    EXPECT_EQ(6u, b.remaining);
    EXPECT_EQ(5000u, b.started_ms);
}

TEST(IndicatorLogic, colour_on_odd_half_period_blank_on_even) {
    wls_blink_t b = WLS_BLINK_IDLE;
    wls_blink_arm(&b, 250, 1, 0); /* 2 half-periods: blank, then colour */

    /* remaining == 2 (even) -> blank, and the interval has not elapsed yet. */
    wls_blink_out_t out = wls_blink_tick(&b, 100);
    EXPECT_EQ(WLS_BLINK_PAINT, out.kind);
    EXPECT_FALSE(out.on);

    /* Advance one interval: remaining 2 -> 1 (odd) -> colour. */
    out = wls_blink_tick(&b, 300);
    EXPECT_EQ(WLS_BLINK_PAINT, out.kind);
    EXPECT_TRUE(out.on);
    EXPECT_EQ(1u, b.remaining);
}

TEST(IndicatorLogic, completes_after_its_blinks_and_stops) {
    wls_blink_t b = WLS_BLINK_IDLE;
    wls_blink_arm(&b, 250, 1, 0); /* two half-periods */

    wls_blink_out_t out = wls_blink_tick(&b, 250); /* remaining 2 -> 0? no: -> 1 */
    EXPECT_EQ(WLS_BLINK_PAINT, out.kind);

    out = wls_blink_tick(&b, 500); /* remaining 1 -> 0 -> DONE */
    EXPECT_EQ(WLS_BLINK_DONE, out.kind);
    EXPECT_EQ(0u, b.interval_ms);

    /* After DONE it is idle and paints nothing. */
    out = wls_blink_tick(&b, 1000);
    EXPECT_EQ(WLS_BLINK_PAINT, out.kind);
    EXPECT_FALSE(out.on);
}

TEST(IndicatorLogic, battery_colour_follows_charge_then_level) {
    EXPECT_EQ(WLS_BAT_GREEN, wls_bat_colour_of(true, true, 5));  /* charging+full beats level */
    EXPECT_EQ(WLS_BAT_BLUE, wls_bat_colour_of(true, false, 90)); /* charging beats high level */
    EXPECT_EQ(WLS_BAT_GREEN, wls_bat_colour_of(false, false, 50));
    EXPECT_EQ(WLS_BAT_GREEN, wls_bat_colour_of(false, false, 100));
    EXPECT_EQ(WLS_BAT_ORANGE, wls_bat_colour_of(false, false, 30));
    EXPECT_EQ(WLS_BAT_ORANGE, wls_bat_colour_of(false, false, 49));
    EXPECT_EQ(WLS_BAT_RED, wls_bat_colour_of(false, false, 29));
    EXPECT_EQ(WLS_BAT_RED, wls_bat_colour_of(false, false, 0));
}

} // namespace
