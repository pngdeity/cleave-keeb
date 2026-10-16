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

/* Frame ownership (artefact 2). The arbitration used to be positional -- the
 * frame pass returned early at the line after the boot code -- so the rule was
 * the line number and any painter moved across it changed ownership silently.
 * These vectors pin the rule itself: the boot code is exclusive, and the
 * exclusivity is decided from a fact the caller can evaluate without painting. */
TEST(IndicatorLogic, the_boot_code_owns_the_frame_exclusively) {
    EXPECT_TRUE(wls_frame_owner_is_exclusive(WLS_FRAME_OWNER_BOOT_CODE));
}

TEST(IndicatorLogic, no_owner_is_not_exclusive) {
    /* "No owner" must not suppress anything -- if it did, the whole pass after
     * the boot code would be unreachable permanently. */
    EXPECT_FALSE(wls_frame_owner_is_exclusive(WLS_FRAME_OWNER_NONE));
}

TEST(IndicatorLogic, a_live_exclusive_claim_owns_the_frame) {
    EXPECT_EQ(WLS_FRAME_OWNER_BOOT_CODE, wls_frame_owner_of(/*exclusive_claim_live=*/true));
}

TEST(IndicatorLogic, a_cleared_claim_leaves_the_frame_to_the_rest_of_the_pass) {
    EXPECT_EQ(WLS_FRAME_OWNER_NONE, wls_frame_owner_of(/*exclusive_claim_live=*/false));
}

TEST(IndicatorLogic, only_an_exclusive_owner_suppresses_the_frame) {
    /* The property the shell relies on: the pass suppresses the rest exactly
     * when the arbiter names an exclusive owner. Composed the way the pass
     * composes it, so the shell's `return true` cannot drift from the rule. */
    wls_frame_owner_t boot = wls_frame_owner_of(true);
    wls_frame_owner_t none = wls_frame_owner_of(false);
    EXPECT_TRUE(wls_frame_owner_is_exclusive(boot));
    EXPECT_FALSE(wls_frame_owner_is_exclusive(none));
}

/* LED-index ownership (D3/D4). The frame arbiter says who owns a FRAME; nothing
 * said who owns an LED, so two non-exclusive painters writing the same index
 * resolved by call order -- a line number -- and moving a paint call changed the
 * sight with nothing failing. These vectors pin the winner as a rule. */
TEST(IndicatorLogic, the_boot_code_owns_every_led_index_while_its_claim_lives) {
    EXPECT_EQ(WLS_LED_PAINTER_BOOT_CODE, wls_led_painter_of(0, true));
    EXPECT_EQ(WLS_LED_PAINTER_BOOT_CODE, wls_led_painter_of(17, true));
    EXPECT_EQ(WLS_LED_PAINTER_BOOT_CODE, wls_led_painter_of(64, true));
    /* The all-LEDs sentinel is owned by the all-LEDs painter too. */
    EXPECT_EQ(WLS_LED_PAINTER_BOOT_CODE, wls_led_painter_of(WLS_LED_INDEX_ALL, true));
}

TEST(IndicatorLogic, with_the_claim_cleared_the_leds_belong_to_their_own_painters) {
    /* The same indices now resolve to the non-exclusive precedence, which is the
     * property that keeps the boot code from owning the LEDs forever. */
    EXPECT_EQ(WLS_LED_PAINTER_LINK_STATE, wls_led_painter_of(17, false));
    EXPECT_EQ(WLS_LED_PAINTER_BATTERY, wls_led_painter_of(64, false));
}

TEST(IndicatorLogic, the_reject_cue_wins_the_channel_leds_it_shares) {
    /* 17/18/19 are written by the link-state cue, the channel flash and the
     * channel cue. REJECT must win -- that is the arbitration the shell performs
     * with an early return; here it is the precedence. */
    EXPECT_EQ(WLS_LED_PAINTER_LINK_STATE, wls_led_painter_of(17, false));
    EXPECT_EQ(WLS_LED_PAINTER_LINK_STATE, wls_led_painter_of(18, false));
    EXPECT_EQ(WLS_LED_PAINTER_LINK_STATE, wls_led_painter_of(19, false));
}

TEST(IndicatorLogic, an_led_only_the_flash_writes_belongs_to_the_flash) {
    /* 20 (2.4G) and the BT4/BT5 literals 41/42 are flash-only. */
    EXPECT_EQ(WLS_LED_PAINTER_CHANNEL_FLASH, wls_led_painter_of(20, false));
    EXPECT_EQ(WLS_LED_PAINTER_CHANNEL_FLASH, wls_led_painter_of(41, false));
    EXPECT_EQ(WLS_LED_PAINTER_CHANNEL_FLASH, wls_led_painter_of(42, false));
}

TEST(IndicatorLogic, the_battery_and_marker_leds_have_their_owners) {
    EXPECT_EQ(WLS_LED_PAINTER_BATTERY, wls_led_painter_of(63, false));
    EXPECT_EQ(WLS_LED_PAINTER_BATTERY, wls_led_painter_of(64, false));
    EXPECT_EQ(WLS_LED_PAINTER_BATTERY, wls_led_painter_of(65, false));
    EXPECT_EQ(WLS_LED_PAINTER_MARKER, wls_led_painter_of(32, false));
}

TEST(IndicatorLogic, an_unlisted_index_belongs_to_no_enumerated_painter) {
    /* The keymap's and rgblight's LEDs are open-ended, so the core claims none
     * of them -- they are not an unprotected overlap, they are simply not this
     * table's to arbitrate. */
    EXPECT_EQ(WLS_LED_PAINTER_NONE, wls_led_painter_of(1, false));
    EXPECT_EQ(WLS_LED_PAINTER_NONE, wls_led_painter_of(2, false));
    EXPECT_EQ(WLS_LED_PAINTER_NONE, wls_led_painter_of(99, false));
}

TEST(IndicatorLogic, only_the_boot_code_is_exclusive_over_leds) {
    EXPECT_TRUE(wls_led_painter_is_exclusive(WLS_LED_PAINTER_BOOT_CODE));
    EXPECT_FALSE(wls_led_painter_is_exclusive(WLS_LED_PAINTER_LINK_STATE));
    EXPECT_FALSE(wls_led_painter_is_exclusive(WLS_LED_PAINTER_CHANNEL_FLASH));
    EXPECT_FALSE(wls_led_painter_is_exclusive(WLS_LED_PAINTER_CHANNEL_CUE));
    EXPECT_FALSE(wls_led_painter_is_exclusive(WLS_LED_PAINTER_BATTERY));
    EXPECT_FALSE(wls_led_painter_is_exclusive(WLS_LED_PAINTER_SET_FLASH));
    EXPECT_FALSE(wls_led_painter_is_exclusive(WLS_LED_PAINTER_MARKER));
    EXPECT_FALSE(wls_led_painter_is_exclusive(WLS_LED_PAINTER_NONE));
}

TEST(IndicatorLogic, the_real_overlaps_are_the_ones_the_precedence_resolves) {
    /* The three channel painters share LEDs; the set-flash pass shares with
     * everything (it can write any index); the rest are disjoint. This is what
     * makes the precedence do real work rather than restate a partition. */
    EXPECT_TRUE(wls_led_painters_overlap(WLS_LED_PAINTER_LINK_STATE, WLS_LED_PAINTER_CHANNEL_FLASH));
    EXPECT_TRUE(wls_led_painters_overlap(WLS_LED_PAINTER_LINK_STATE, WLS_LED_PAINTER_CHANNEL_CUE));
    EXPECT_TRUE(wls_led_painters_overlap(WLS_LED_PAINTER_CHANNEL_FLASH, WLS_LED_PAINTER_CHANNEL_CUE));
    EXPECT_TRUE(wls_led_painters_overlap(WLS_LED_PAINTER_SET_FLASH, WLS_LED_PAINTER_BATTERY));

    EXPECT_FALSE(wls_led_painters_overlap(WLS_LED_PAINTER_BATTERY, WLS_LED_PAINTER_CHANNEL_CUE));
    EXPECT_FALSE(wls_led_painters_overlap(WLS_LED_PAINTER_BATTERY, WLS_LED_PAINTER_MARKER));
    EXPECT_FALSE(wls_led_painters_overlap(WLS_LED_PAINTER_MARKER, WLS_LED_PAINTER_CHANNEL_FLASH));
    /* No enumerated painter overlaps "none". */
    EXPECT_FALSE(wls_led_painters_overlap(WLS_LED_PAINTER_NONE, WLS_LED_PAINTER_BOOT_CODE));
}

} // namespace
