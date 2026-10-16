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

/* Proof of the low-battery sleep-order timing, without hardware.
 *
 * The rule was inline in the battery paint: arm a 60 s timer on first sight of a
 * critical level, order sleep once it elapses, clear on recovery. These vectors
 * pin arm/hold/fire/clear and the "does not fire early" boundary.
 */

#include "gtest/gtest.h"

extern "C" {
#include "keyboards/epomaker/epomaker_split65/wls/sleep_order_logic.h"
}

namespace {

TEST(SleepOrderLogic, first_critical_tick_arms_and_does_not_fire) {
    wls_sleep_arm_t s = WLS_SLEEP_ARM_IDLE;
    EXPECT_EQ(WLS_SLEEP_ARM_HOLD, wls_sleep_arm_tick(&s, true, 1000));
    EXPECT_EQ(1000u, s.armed_ms);
}

TEST(SleepOrderLogic, does_not_fire_before_the_arm_window_elapses) {
    wls_sleep_arm_t s = WLS_SLEEP_ARM_IDLE;
    (void)wls_sleep_arm_tick(&s, true, 1000);
    EXPECT_EQ(WLS_SLEEP_ARM_HOLD, wls_sleep_arm_tick(&s, true, 1000 + WLS_SLEEP_ORDER_ARM_MS - 1));
}

TEST(SleepOrderLogic, fires_exactly_at_the_arm_window_and_clears) {
    wls_sleep_arm_t s = WLS_SLEEP_ARM_IDLE;
    (void)wls_sleep_arm_tick(&s, true, 1000);
    EXPECT_EQ(WLS_SLEEP_ARM_NOW, wls_sleep_arm_tick(&s, true, 1000 + WLS_SLEEP_ORDER_ARM_MS));
    EXPECT_EQ(0u, s.armed_ms);
}

TEST(SleepOrderLogic, recovery_clears_the_arm) {
    wls_sleep_arm_t s = WLS_SLEEP_ARM_IDLE;
    (void)wls_sleep_arm_tick(&s, true, 1000);
    EXPECT_EQ(WLS_SLEEP_ARM_HOLD, wls_sleep_arm_tick(&s, false, 2000));
    EXPECT_EQ(0u, s.armed_ms);
    /* After recovery the window restarts from the new arm. */
    (void)wls_sleep_arm_tick(&s, true, 5000);
    EXPECT_EQ(WLS_SLEEP_ARM_HOLD, wls_sleep_arm_tick(&s, true, 5000 + WLS_SLEEP_ORDER_ARM_MS - 1));
}

TEST(SleepOrderLogic, never_critical_never_fires) {
    wls_sleep_arm_t s = WLS_SLEEP_ARM_IDLE;
    for (uint32_t t = 0; t < 5 * WLS_SLEEP_ORDER_ARM_MS; t += 1000) {
        EXPECT_EQ(WLS_SLEEP_ARM_HOLD, wls_sleep_arm_tick(&s, false, t));
    }
    EXPECT_EQ(0u, s.armed_ms);
}

TEST(SleepOrderLogic, arm_at_time_zero_still_fires) {
    wls_sleep_arm_t s = WLS_SLEEP_ARM_IDLE;
    EXPECT_EQ(WLS_SLEEP_ARM_HOLD, wls_sleep_arm_tick(&s, true, 0));
    EXPECT_NE(0u, s.armed_ms); /* the 0 sentinel is avoided */
    EXPECT_EQ(WLS_SLEEP_ARM_NOW, wls_sleep_arm_tick(&s, true, WLS_SLEEP_ORDER_ARM_MS + 1));
}

} // namespace
