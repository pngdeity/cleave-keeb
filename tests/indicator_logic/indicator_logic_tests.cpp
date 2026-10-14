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

/* F3 extracted `wls_bat_rgb_of` from the shell but shipped it without a test
 * (a V10 gap in our own fix); these pin the byte map so the visible colour
 * cannot drift. */
TEST(IndicatorLogic, battery_rgb_map_is_pinned) {
    wls_bat_rgb_t green  = wls_bat_rgb_of(WLS_BAT_GREEN);
    wls_bat_rgb_t blue   = wls_bat_rgb_of(WLS_BAT_BLUE);
    wls_bat_rgb_t orange = wls_bat_rgb_of(WLS_BAT_ORANGE);
    wls_bat_rgb_t red    = wls_bat_rgb_of(WLS_BAT_RED);
    wls_bat_rgb_t none   = wls_bat_rgb_of(WLS_BAT_NONE);

    EXPECT_EQ(0x00, green.r);
    EXPECT_EQ(0xFF, green.g);
    EXPECT_EQ(0x00, green.b);

    EXPECT_EQ(0x00, blue.r);
    EXPECT_EQ(0x40, blue.g);
    EXPECT_EQ(0xFF, blue.b);

    EXPECT_EQ(0xFF, orange.r);
    EXPECT_EQ(0x80, orange.g);
    EXPECT_EQ(0x00, orange.b);

    EXPECT_EQ(0xFF, red.r);
    EXPECT_EQ(0x00, red.g);
    EXPECT_EQ(0x00, red.b);

    EXPECT_EQ(0x00, none.r);
    EXPECT_EQ(0x00, none.g);
    EXPECT_EQ(0x00, none.b);
}

/* The charging-LED action table (F10): the shell's last real branch, now a pure
 * decision. Mirrors the branch it replaced, including its asymmetry -- only the
 * two charging cases gate on `request`; the warning blink does not. */
TEST(IndicatorLogic, battery_action_follows_the_replaced_branch) {
    const uint8_t LOW = 15;

    /* Charging + full: red when requested, otherwise nothing. */
    EXPECT_EQ(WLS_BAT_ACT_PAINT_RED, wls_bat_action_of(true, true, 50, true, LOW));
    EXPECT_EQ(WLS_BAT_ACT_NONE, wls_bat_action_of(true, true, 50, false, LOW));

    /* Charging, not full: green when requested, otherwise nothing. */
    EXPECT_EQ(WLS_BAT_ACT_PAINT_GREEN, wls_bat_action_of(true, false, 50, true, LOW));
    EXPECT_EQ(WLS_BAT_ACT_NONE, wls_bat_action_of(true, false, 50, false, LOW));

    /* Not charging, at or below the low level: the warning blink, and `request`
     * is deliberately ignored on this path. */
    EXPECT_EQ(WLS_BAT_ACT_BLINK_RED, wls_bat_action_of(false, false, 15, true, LOW));
    EXPECT_EQ(WLS_BAT_ACT_BLINK_RED, wls_bat_action_of(false, false, 0, false, LOW));

    /* Not charging, above the low level: nothing. */
    EXPECT_EQ(WLS_BAT_ACT_NONE, wls_bat_action_of(false, false, 16, true, LOW));
    EXPECT_EQ(WLS_BAT_ACT_NONE, wls_bat_action_of(false, false, 100, true, LOW));

    /* Charging wins over the low level, even at 0%: the branch order. */
    EXPECT_EQ(WLS_BAT_ACT_PAINT_GREEN, wls_bat_action_of(true, false, 0, true, LOW));
}

} // namespace
