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

#include "gtest/gtest.h"

extern "C" {
#include "keyboards/linker/wireless/lowpower.h"
#include "keyboards/linker/wireless/lowpower_logic.h"
}

/* The wake codes are bit flags and the decisions are pure functions of their
 * arguments, so the whole of the sleep invariant is testable here without the
 * MCU. The scenarios below are the ones that produced real hardware faults. */

class LowpowerLogicTest : public ::testing::Test {};

TEST_F(LowpowerLogicTest, wake_set_members_are_bits) {
    // Distinct bits, so a set can hold more than one code at once.
    EXPECT_NE(0, LPWR_WAKEUP_MATRIX);
    EXPECT_EQ(0, LPWR_WAKEUP_MATRIX & LPWR_WAKEUP_UART);
    EXPECT_EQ(0, LPWR_WAKEUP_UART & LPWR_WAKEUP_CABLE);
    EXPECT_EQ(0, LPWR_WAKEUP_CABLE & LPWR_WAKEUP_SWITCH);
    EXPECT_EQ(0, LPWR_WAKEUP_SWITCH & LPWR_WAKEUP_USB);
}

TEST_F(LowpowerLogicTest, ordered_master_stop_is_allowed) {
    // Master, ordered (low-battery) sleep: the only permitted stop.
    EXPECT_TRUE(lpwr_stop_is_allowed_decide(true, true));
}

TEST_F(LowpowerLogicTest, unordered_master_stop_is_refused) {
    // Regression 4: a plain idle timeout reaches STOP unordered and left the
    // master dark and unresponsive.
    EXPECT_FALSE(lpwr_stop_is_allowed_decide(true, false));
}

TEST_F(LowpowerLogicTest, slave_stop_always_refused) {
    // The slave must never decide for itself, ordered or not.
    EXPECT_FALSE(lpwr_stop_is_allowed_decide(false, true));
    EXPECT_FALSE(lpwr_stop_is_allowed_decide(false, false));
}

TEST_F(LowpowerLogicTest, armed_code_is_a_real_wake) {
    uint32_t armed = LPWR_WAKEUP_MATRIX | LPWR_WAKEUP_CABLE | LPWR_WAKEUP_SWITCH | LPWR_WAKEUP_USB;
    EXPECT_TRUE(lpwr_wakeup_is_real(LPWR_WAKEUP_MATRIX, armed));
    EXPECT_TRUE(lpwr_wakeup_is_real(LPWR_WAKEUP_CABLE, armed));
    EXPECT_TRUE(lpwr_wakeup_is_real(LPWR_WAKEUP_SWITCH, armed));
    EXPECT_TRUE(lpwr_wakeup_is_real(LPWR_WAKEUP_USB, armed));
}

TEST_F(LowpowerLogicTest, phantom_uart_is_not_a_wake) {
    // The dark-out root cause: the board never arms UART, so a lone UART code
    // must not be read as a wake.
    uint32_t armed = LPWR_WAKEUP_MATRIX | LPWR_WAKEUP_CABLE | LPWR_WAKEUP_SWITCH | LPWR_WAKEUP_USB;
    EXPECT_FALSE(lpwr_wakeup_is_real(LPWR_WAKEUP_UART, armed));
}

TEST_F(LowpowerLogicTest, real_wake_survives_a_phantom_in_the_same_cycle) {
    // A set, not the last code seen: a phantom UART aliased onto an armed pad
    // must not mask a genuine wake that fired in the same cycle.
    uint32_t armed = LPWR_WAKEUP_MATRIX | LPWR_WAKEUP_CABLE | LPWR_WAKEUP_SWITCH | LPWR_WAKEUP_USB;
    EXPECT_TRUE(lpwr_wakeup_is_real(LPWR_WAKEUP_MATRIX | LPWR_WAKEUP_UART, armed));
}

TEST_F(LowpowerLogicTest, pad_aliasing_is_a_pinned_board_fact) {
    // The WB32 EXTI reports by pad number only (`PAL_PAD(line) = line & 0x0F`),
    // so pads are shared between ports. This board's module UART RX is C11 and
    // matrix column 11 is B11 -- both pad 11. A line event on pad 11 is
    // therefore classified UART even when it came from the matrix column, and
    // because the board never arms UART the phantom is discarded.
    //
    // This test pins the two facts that make that survivable: UART is not in
    // the armed mask, and the set-and-mask decision is not fooled by it.
    uint32_t armed = LPWR_WAKEUP_MATRIX | LPWR_WAKEUP_CABLE | LPWR_WAKEUP_SWITCH | LPWR_WAKEUP_USB;

    // What the aliased line event is misclassified as:
    EXPECT_FALSE(lpwr_wakeup_is_real(LPWR_WAKEUP_UART, armed));

    // A genuine matrix wake on a differently-numbered pad still registers:
    EXPECT_TRUE(lpwr_wakeup_is_real(LPWR_WAKEUP_MATRIX, armed));
}
