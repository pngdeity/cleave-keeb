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
    // The WB32 EXTI reports by pad number only (`PAL_PAD(line) = line & 0x0F`,
    // chibios-contrib WB32 GPIOv1), so pads are shared between ports. This
    // board's module UART RX is C11 and matrix column 11 is B11 -- both pad 11.
    // A line event on pad 11 is therefore classified UART even when it came from
    // the matrix column, and because the board never arms UART the phantom is
    // discarded.
    //
    // This test pins the two facts that make that survivable: UART is not in
    // the armed mask, and the set-and-mask decision is not fooled by it.
    uint32_t armed = LPWR_WAKEUP_MATRIX | LPWR_WAKEUP_CABLE | LPWR_WAKEUP_SWITCH | LPWR_WAKEUP_USB;

    // What the aliased line event is misclassified as:
    EXPECT_FALSE(lpwr_wakeup_is_real(LPWR_WAKEUP_UART, armed));

    // A genuine matrix wake on a differently-numbered pad still registers:
    EXPECT_TRUE(lpwr_wakeup_is_real(LPWR_WAKEUP_MATRIX, armed));
}

/* --- Pad-aliasing soundness: the rule the mask's comment only stated ---------
 *
 * The mask above excludes UART. *Why that is safe* was comment prose: because
 * UART's reporting pad is also reachable through a code the board arms. That
 * relation -- the excluded code's pad coverage -- had no home; the predicate
 * `lpwr_excluded_code_is_safe` is it. These vectors pin it, and the board's
 * `lpwr_wakeup_armed_mask()` now applies it where the mask is built. */

TEST_F(LowpowerLogicTest, arming_a_code_needs_no_coverage_argument) {
    // `excluded == false`: nothing is being left out, so no coverage is owed.
    EXPECT_TRUE(lpwr_excluded_code_is_safe(/*excluded=*/false, /*covered=*/false));
    EXPECT_TRUE(lpwr_excluded_code_is_safe(/*excluded=*/false, /*covered=*/true));
}

TEST_F(LowpowerLogicTest, excluding_a_code_whose_pad_is_otherwise_armed_is_sound) {
    // UART's case: excluded, but its pad (11) is also matrix column B11, so an
    // edge there still wakes via MATRIX.
    EXPECT_TRUE(lpwr_excluded_code_is_safe(/*excluded=*/true, /*covered=*/true));
}

TEST_F(LowpowerLogicTest, excluding_a_code_whose_pad_is_not_armed_drops_a_wake) {
    // The unsafe case: excluded AND its pad is not reachable through any armed
    // code, so a real edge on that pad would be silently discarded. The
    // predicate refuses it, and the board's mask builder then arms the code.
    EXPECT_FALSE(lpwr_excluded_code_is_safe(/*excluded=*/true, /*covered=*/false));
}

TEST_F(LowpowerLogicTest, the_board_mask_builder_arms_uart_only_when_its_exclusion_is_unsound) {
    // Composed the way `lpwr_wakeup_armed_mask()` composes it, for the two
    // attributions. On this board UART is excluded and its pad is covered, so
    // UART is NOT armed (its phantom stays discardable). If the coverage fact
    // were false, the exclusion would be unsound and the code must be armed
    // instead -- failing toward waking, never toward a silent discard.
    auto uart_effective = [](bool uart_is_armed, bool pad_covered) {
        bool may_be_excluded = lpwr_excluded_code_is_safe(!uart_is_armed, pad_covered);
        return uart_is_armed || !may_be_excluded;
    };

    EXPECT_FALSE(uart_effective(/*uart_is_armed=*/false, /*pad_covered=*/true)); // this board
    EXPECT_TRUE(uart_effective(/*uart_is_armed=*/false, /*pad_covered=*/false)); // would drop wakes: arm it
    EXPECT_TRUE(uart_effective(/*uart_is_armed=*/true, /*pad_covered=*/false));  // armed by design
}

TEST_F(LowpowerLogicTest, timeout_is_allowed_only_when_a_stop_can_be_taken) {
    // Hook forbids: never allowed, regardless of the other facts.
    EXPECT_FALSE(lpwr_timeout_allowed_decide(false, false, true, true));
    EXPECT_FALSE(lpwr_timeout_allowed_decide(false, false, false, true));

    // A live USB host is never timed out, even with a manual override pending.
    EXPECT_FALSE(lpwr_timeout_allowed_decide(true, true, true, true));
    EXPECT_FALSE(lpwr_timeout_allowed_decide(true, true, false, true));

    // Otherwise: a pending manual override, or an elapsed idle timeout.
    EXPECT_TRUE(lpwr_timeout_allowed_decide(true, false, true, false));
    EXPECT_TRUE(lpwr_timeout_allowed_decide(true, false, false, true));
    EXPECT_FALSE(lpwr_timeout_allowed_decide(true, false, false, false));
}

/* --- The PRESLEEP sequencing invariant (regression 2026-10-15) -------------
 *
 * PRESLEEP disables the RGB matrix and the ONLY thing that re-enables it runs in
 * LPWR_WAKEUP -- a state a refused stop never reaches, because the refusal
 * returns to LPWR_NORMAL. Entering PRESLEEP for a stop the policy will decline
 * therefore stranded the board dark with the matrix off, permanently, until a
 * power cycle (the 30 s dark board, `TODO.md` § 8).
 *
 * The rule is stated here as a pure decision so the sequence is testable, rather
 * than living as an unguarded `if` in the imperative shell where the fault
 * happened. */

TEST_F(LowpowerLogicTest, presleep_requires_both_the_timeout_and_a_permitted_stop) {
    // The fault case: the idle timeout fired, but the board's policy would
    // refuse the stop. Entering PRESLEEP here takes the matrix down for a stop
    // that will be declined -- the dark-board bug.
    EXPECT_FALSE(lpwr_presleep_is_allowed_decide(true, false));
}

TEST_F(LowpowerLogicTest, presleep_is_allowed_when_the_stop_will_be_taken) {
    EXPECT_TRUE(lpwr_presleep_is_allowed_decide(true, true));
}

TEST_F(LowpowerLogicTest, presleep_is_refused_without_the_timeout) {
    // No reason to sleep at all: never presleep, permitted stop or not.
    EXPECT_FALSE(lpwr_presleep_is_allowed_decide(false, false));
    EXPECT_FALSE(lpwr_presleep_is_allowed_decide(false, true));
}

TEST_F(LowpowerLogicTest, an_unordered_idle_timeout_cannot_reach_presleep) {
    /* Composed from the real deciders, the way the shell composes them: an idle
     * timeout on the master with no ordering. `timeout_allowed` is true, but the
     * stop policy refuses (`lpwr_stop_is_allowed_decide(master, ordered=false)`),
     * so PRESLEEP must not be entered. This is the exact board state that
     * produced the 30 s dark board. */
    bool timeout_allowed = lpwr_timeout_allowed_decide(/*hook*/ true, /*usb*/ false, /*manual*/ false, /*idle*/ true);
    bool stop_allowed    = lpwr_stop_is_allowed_decide(/*master*/ true, /*ordered*/ false);
    EXPECT_TRUE(timeout_allowed);
    EXPECT_FALSE(stop_allowed);
    EXPECT_FALSE(lpwr_presleep_is_allowed_decide(timeout_allowed, stop_allowed));
}
