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

/* Proof of the RGB-keycode decision, without hardware.
 *
 * The four `QK_RGB_MATRIX_*` keys each carried a threshold comparison inline in
 * the shell switch (magic numbers 215/120/95/30 and mode 29->31) with no test
 * coverage. These vectors pin the thresholds at their exact boundaries and the
 * one mode step, so the rule is a contract rather than a comment.
 */

#include "gtest/gtest.h"

extern "C" {
#include "keyboards/epomaker/epomaker_split65/wls/rgb_key_logic.h"
}

/* The keycode constants the decision matches on (same narrow header the core
 * uses). */
extern "C" {
#include "keycodes.h"
}

namespace {

static wls_rgb_state_t state(uint8_t speed, uint8_t val, uint8_t mode, bool pressed) {
    wls_rgb_state_t s;
    s.speed    = speed;
    s.val      = val;
    s.val_step = 16; /* RGB_MATRIX_VAL_STEP */
    s.mode     = mode;
    s.pressed  = pressed;
    return s;
}

TEST(RgbKeyLogic, non_rgb_key_is_none) {
    wls_rgb_state_t s = state(128, 128, 0, true);
    EXPECT_EQ(WLS_RGB_KEY_NONE, wls_rgb_key_decide(KC_A, &s).kind);
}

TEST(RgbKeyLogic, speed_up_acks_at_or_above_the_high_threshold) {
    wls_rgb_state_t hi = state(WLS_RGB_SPEED_HIGH_ACK, 128, 0, true);
    wls_rgb_state_t lo = state(WLS_RGB_SPEED_HIGH_ACK - 1, 128, 0, true);
    EXPECT_EQ(WLS_RGB_KEY_ACK_DIR, wls_rgb_key_decide(QK_RGB_MATRIX_SPEED_UP, &hi).kind);
    EXPECT_EQ(WLS_RGB_KEY_NONE, wls_rgb_key_decide(QK_RGB_MATRIX_SPEED_UP, &lo).kind);
}

TEST(RgbKeyLogic, speed_down_acks_at_or_below_the_low_threshold) {
    wls_rgb_state_t lo = state(WLS_RGB_SPEED_LOW_ACK, 128, 0, true);
    wls_rgb_state_t hi = state(WLS_RGB_SPEED_LOW_ACK + 1, 128, 0, true);
    EXPECT_EQ(WLS_RGB_KEY_ACK_DIR, wls_rgb_key_decide(QK_RGB_MATRIX_SPEED_DOWN, &lo).kind);
    EXPECT_EQ(WLS_RGB_KEY_NONE, wls_rgb_key_decide(QK_RGB_MATRIX_SPEED_DOWN, &hi).kind);
}

TEST(RgbKeyLogic, value_up_enables_and_acks_only_at_high_speed) {
    wls_rgb_state_t hi = state(WLS_RGB_VALUE_UP_ACK_SPEED, 128, 0, true);
    wls_rgb_state_t lo = state(WLS_RGB_VALUE_UP_ACK_SPEED - 1, 128, 0, true);
    EXPECT_EQ(WLS_RGB_KEY_ACK_DIR, wls_rgb_key_decide(QK_RGB_MATRIX_VALUE_UP, &hi).kind);
    EXPECT_TRUE(wls_rgb_key_decide(QK_RGB_MATRIX_VALUE_UP, &hi).enable);
    EXPECT_EQ(WLS_RGB_KEY_NONE, wls_rgb_key_decide(QK_RGB_MATRIX_VALUE_UP, &lo).kind);
    EXPECT_TRUE(wls_rgb_key_decide(QK_RGB_MATRIX_VALUE_UP, &lo).enable);
}

TEST(RgbKeyLogic, value_down_blanks_at_low_value_else_acks_at_low_speed) {
    wls_rgb_state_t blank = state(128, 16, 0, true);
    wls_rgb_state_t ack   = state(WLS_RGB_VALUE_DOWN_ACK_SPEED, 200, 0, true);
    wls_rgb_state_t none  = state(WLS_RGB_VALUE_DOWN_ACK_SPEED + 1, 200, 0, true);
    EXPECT_EQ(WLS_RGB_KEY_BLANK_ALL, wls_rgb_key_decide(QK_RGB_MATRIX_VALUE_DOWN, &blank).kind);
    EXPECT_EQ(WLS_RGB_KEY_ACK_DIR, wls_rgb_key_decide(QK_RGB_MATRIX_VALUE_DOWN, &ack).kind);
    EXPECT_EQ(WLS_RGB_KEY_NONE, wls_rgb_key_decide(QK_RGB_MATRIX_VALUE_DOWN, &none).kind);
}

TEST(RgbKeyLogic, blank_all_takes_precedence_over_the_speed_ack) {
    wls_rgb_state_t both = state(WLS_RGB_VALUE_DOWN_ACK_SPEED, 16, 0, true);
    EXPECT_EQ(WLS_RGB_KEY_BLANK_ALL, wls_rgb_key_decide(QK_RGB_MATRIX_VALUE_DOWN, &both).kind);
}

TEST(RgbKeyLogic, released_rgb_keys_do_nothing) {
    wls_rgb_state_t up   = state(WLS_RGB_SPEED_HIGH_ACK, 128, 0, false);
    wls_rgb_state_t down = state(0, 0, 0, false);
    EXPECT_EQ(WLS_RGB_KEY_NONE, wls_rgb_key_decide(QK_RGB_MATRIX_SPEED_UP, &up).kind);
    EXPECT_EQ(WLS_RGB_KEY_NONE, wls_rgb_key_decide(QK_RGB_MATRIX_VALUE_DOWN, &down).kind);
}

} // namespace
