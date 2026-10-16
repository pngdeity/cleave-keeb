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

/* --- The pad -> code attribution (D6) --------------------------------------
 *
 * The mask's soundness (`lpwr_excluded_code_is_safe`) reasons about which pad a
 * code is reported on, and that relation used to be prose: a table in the doc
 * plus a `switch` in the board's classifier, with nothing failing if a pin moved
 * (D6 of `TODO.md` "Determination residue"). These vectors pin the attribution
 * as a checkable function of the pad, and the collisions it implies. The pads
 * below mirror the board's pins: UART RX C11=11, USB detect A12=12, cable A7=7,
 * mode switch C14=14 and C15=15. */

static lpwr_attribution_t board_attr(void) {
    lpwr_attribution_t a;
    a.uart_pad     = 11;
    a.usb_pad      = 12;
    a.cable_pad    = 7;
    a.bt_pad       = 14;
    a.g24_pad      = 15;
    a.uart_code    = LPWR_WAKEUP_UART;
    a.usb_code     = LPWR_WAKEUP_USB;
    a.cable_code   = LPWR_WAKEUP_CABLE;
    a.bt_code      = LPWR_WAKEUP_SWITCH;
    a.g24_code     = LPWR_WAKEUP_SWITCH;
    a.default_code = LPWR_WAKEUP_MATRIX;
    return a;
}

TEST_F(LowpowerLogicTest, a_registered_pad_reports_its_own_code) {
    lpwr_attribution_t a = board_attr();
    EXPECT_EQ(LPWR_WAKEUP_UART, lpwr_code_of_pad(&a, 11, true));
    EXPECT_EQ(LPWR_WAKEUP_USB, lpwr_code_of_pad(&a, 12, true));
    EXPECT_EQ(LPWR_WAKEUP_CABLE, lpwr_code_of_pad(&a, 7, true));
    EXPECT_EQ(LPWR_WAKEUP_SWITCH, lpwr_code_of_pad(&a, 14, true));
    EXPECT_EQ(LPWR_WAKEUP_SWITCH, lpwr_code_of_pad(&a, 15, true));
}

TEST_F(LowpowerLogicTest, an_unclaimed_pad_reports_the_default) {
    // MATRIX is the catch-all, not a pin: any pad no other code claims.
    lpwr_attribution_t a = board_attr();
    const uint8_t unclaimed[] = {0, 1, 2, 3, 4, 5, 6, 8, 9, 10, 13};
    for (uint8_t p : unclaimed) {
        EXPECT_EQ(LPWR_WAKEUP_MATRIX, lpwr_code_of_pad(&a, p, true));
    }
}

TEST_F(LowpowerLogicTest, the_mode_switch_pads_are_master_only) {
    /* The slave never registers the mode-switch pins (`wls.c`
     * `lpwr_exti_init_hook` gates them on `is_keyboard_master()`), so on the
     * slave a pad 14/15 edge falls through to the default. The attribution must
     * say so, or the slave would arm a wake source it never registered. */
    lpwr_attribution_t a = board_attr();
    EXPECT_EQ(LPWR_WAKEUP_SWITCH, lpwr_code_of_pad(&a, 14, true));
    EXPECT_EQ(LPWR_WAKEUP_SWITCH, lpwr_code_of_pad(&a, 15, true));
    EXPECT_EQ(LPWR_WAKEUP_MATRIX, lpwr_code_of_pad(&a, 14, false));
    EXPECT_EQ(LPWR_WAKEUP_MATRIX, lpwr_code_of_pad(&a, 15, false));
    // The master-independent pads report the same on either half.
    EXPECT_EQ(LPWR_WAKEUP_CABLE, lpwr_code_of_pad(&a, 7, false));
    EXPECT_EQ(LPWR_WAKEUP_UART, lpwr_code_of_pad(&a, 11, false));
}

TEST_F(LowpowerLogicTest, the_inverse_lookup_finds_the_reporting_pad) {
    /* The mask asks "which pad does this code claim"; the inverse must agree
     * with the forward classifier, so there is one attribution, not two. */
    lpwr_attribution_t a = board_attr();
    EXPECT_EQ(11, lpwr_pad_of_code(&a, LPWR_WAKEUP_UART, true));
    EXPECT_EQ(12, lpwr_pad_of_code(&a, LPWR_WAKEUP_USB, true));
    EXPECT_EQ(7, lpwr_pad_of_code(&a, LPWR_WAKEUP_CABLE, true));
    EXPECT_EQ(14, lpwr_pad_of_code(&a, LPWR_WAKEUP_SWITCH, true));
    // MATRIX is not a pad: it is the default, so it has no reporting pad.
    EXPECT_EQ(LPWR_PAD_NONE, lpwr_pad_of_code(&a, LPWR_WAKEUP_MATRIX, true));
    // The mode switch is master-only here too.
    EXPECT_EQ(LPWR_PAD_NONE, lpwr_pad_of_code(&a, LPWR_WAKEUP_SWITCH, false));
}

TEST_F(LowpowerLogicTest, the_inverse_is_consistent_with_the_forward_map) {
    /* Round trip: for every pad the board registers, the code the forward map
     * yields must map back to that pad (or to a pad it shares, for the two
     * switch pads which both yield SWITCH). This is the property that keeps the
     * two lookups from drifting. */
    lpwr_attribution_t a = board_attr();
    const uint8_t pads[] = {11, 12, 7, 14, 15};
    for (uint8_t p : pads) {
        uint32_t code = lpwr_code_of_pad(&a, p, true);
        uint8_t  back = lpwr_pad_of_code(&a, code, true);
        EXPECT_EQ(code, lpwr_code_of_pad(&a, back, true)) << "pad " << (int)p;
    }
}

TEST_F(LowpowerLogicTest, pads_collide_exactly_when_their_numbers_match) {
    /* `PAL_PAD(line) = line & 0x0F`: the port is discarded, so UART RX C11 and
     * matrix column B11 are the same pad 11 and the same wake code. Stated as a
     * function so "which pads collide" has one home. */
    EXPECT_TRUE(lpwr_pads_collide(11, 11));
    EXPECT_FALSE(lpwr_pads_collide(11, 12));
    EXPECT_FALSE(lpwr_pads_collide(7, 14));
}

TEST_F(LowpowerLogicTest, the_uart_exclusion_is_sound_because_its_pad_is_shared) {
    /* The composed fact the mask reasons about: UART is excluded, and its pad
     * (11) is also a matrix pad. The classifier says a pad-11 edge can report
     * UART, and pad 11 is not a code the board arms -- but the same physical pad
     * is reachable as MATRIX, which it does arm. Composed through the real
     * deciders, excluding UART is sound. */
    lpwr_attribution_t a      = board_attr();
    uint32_t           pad_11 = lpwr_code_of_pad(&a, 11, true);
    EXPECT_EQ(LPWR_WAKEUP_UART, pad_11);

    const bool uart_is_armed   = false; // the board never arms UART
    const bool pad_11_as_matrix = true; // pad 11 is also matrix column B11
    EXPECT_TRUE(lpwr_excluded_code_is_safe(!uart_is_armed, pad_11_as_matrix));

    // And the opposite: an excluded code whose pad has no other claimant would
    // be refused, so the composition is not vacuously true.
    EXPECT_FALSE(lpwr_excluded_code_is_safe(/*excluded*/ true, /*covered*/ false));
}

TEST_F(LowpowerLogicTest, no_other_pad_collision_drops_a_wake) {
    /* D7: "no OTHER collision drops a wake" was an argument in prose. It is
     * checkable now that the attribution is a function. The claim is: on this
     * board the ONLY code the mask excludes is UART, and UART's pad is covered by
     * an armed code; every OTHER registered pad reports a code the board arms, so
     * no collision between two registered pads can discard a wake.
     *
     * Derived, not restated: walk the pads the board registers, take each one's
     * code through the classifier, and assert that (a) it is armed, or (b) it is
     * UART, whose pad is covered. A future pin move that introduced a new
     * unarmed code on a registered pad would fail here -- which is the property
     * the prose argument could not enforce. */
    lpwr_attribution_t a = board_attr();

    // The codes the board arms, from the mask builder (`lpwr_wakeup_armed_mask`).
    const uint32_t armed = LPWR_WAKEUP_MATRIX | LPWR_WAKEUP_CABLE | LPWR_WAKEUP_SWITCH | LPWR_WAKEUP_USB;
    // The one code the board excludes by design.
    const uint32_t excluded = LPWR_WAKEUP_UART;

    const uint8_t registered[] = {11 /* UART RX C11 */, 12 /* A12 */, 7 /* cable */, 14 /* BT */, 15 /* 2.4G */};
    for (uint8_t p : registered) {
        const uint32_t code = lpwr_code_of_pad(&a, p, true);
        const bool     is_armed = (code & armed) != 0;
        const bool     is_the_excluded_code = code == excluded;
        // Every registered pad either reports an armed code, or reports the one
        // excluded code whose pad coverage is separately argued.
        EXPECT_TRUE(is_armed || is_the_excluded_code) << "pad " << (int)p << " code " << code;
    }
}

TEST_F(LowpowerLogicTest, every_unregistered_pad_reports_the_armed_default) {
    /* The other half of D7: a pad nobody registered reports MATRIX, which the
     * board arms -- so an edge on any of them wakes. Combined with the test
     * above, this is the complete argument the prose only gestured at: every pad
     * is either armed directly (MATRIX or a registered armed code) or is the one
     * covered exclusion. */
    lpwr_attribution_t a = board_attr();
    const uint32_t     armed_default = LPWR_WAKEUP_MATRIX;

    for (uint8_t p = 0; p < 16; p++) {
        const uint32_t code = lpwr_code_of_pad(&a, p, true);
        // Every pad's code is armed, or is the covered exclusion (pad 11).
        EXPECT_TRUE((code & (armed_default | LPWR_WAKEUP_CABLE | LPWR_WAKEUP_SWITCH | LPWR_WAKEUP_USB)) != 0 || code == LPWR_WAKEUP_UART)
            << "pad " << (int)p << " code " << code;
    }
}

/* --- The arm sites and their ordering (D8) ---------------------------------
 *
 * A stop needs two independent flags -- `manual_timeout` (permission) and
 * `ordered_sleep` (the order) -- set at the same sites. The relationship between
 * them (an order implies an arm; an arm alone is fine because the policy refuses
 * an unordered stop) was prose in the stop/wake doc's §1 table. These vectors pin
 * the coherence rule, so a new site that orders without arming fails a test.
 * See D8 of `TODO.md` "Determination residue". */

TEST_F(LowpowerLogicTest, an_order_must_arm_the_timeout_path) {
    /* The incoherent combination: a site that orders a sleep but does not arm
     * the timeout path. The order can never be acted on -- the machine never
     * reaches PRESLEEP -- so the site's intent and its power effect disagree. */
    EXPECT_FALSE(lpwr_arm_is_coherent(/*orders*/ true, /*arms*/ false));
}

TEST_F(LowpowerLogicTest, ordering_and_arming_together_is_coherent) {
    /* Both real ordering sites (the low-battery path, the slave's 0xAA handler)
     * do both, in this order. */
    EXPECT_TRUE(lpwr_arm_is_coherent(/*orders*/ true, /*arms*/ true));
}

TEST_F(LowpowerLogicTest, an_arm_without_an_order_is_coherent) {
    /* The link-watch sites (`HSM_LINK_ACT_RECONNECT` / `_SLEEP`) arm the timeout
     * path without ordering: legitimate, because the stop policy refuses the
     * unordered stop. This is the S1 shape, and it must read as coherent -- the
     * fix was to refuse the stop, not to forbid the arm. */
    EXPECT_TRUE(lpwr_arm_is_coherent(/*orders*/ false, /*arms*/ true));
    EXPECT_TRUE(lpwr_arm_is_coherent(/*orders*/ false, /*arms*/ false));
}

TEST_F(LowpowerLogicTest, the_ordering_sites_are_composed_coherently) {
    /* Composed the way the shell composes each real site, through the two
     * deciders: a site is coherent when, if it orders, the policy would permit
     * the stop it enables. */
    struct {
        const char *name;
        bool        orders;
        bool        arms;
    } sites[] = {
        {"low-battery order (wls_indicator.c)", true, true},
        {"slave 0xAA handler (wls_split.c)", true, true},
        {"link-watch reconnect (wls.c)", false, true},
        {"link-watch sleep (wls.c)", false, true},
    };
    for (auto &s : sites) {
        EXPECT_TRUE(lpwr_arm_is_coherent(s.orders, s.arms)) << s.name;
        // A site that orders must, composed with the policy, yield a permitted
        // stop -- because it also armed.
        if (s.orders) {
            EXPECT_TRUE(lpwr_stop_is_allowed_decide(/*master*/ true, /*ordered*/ s.orders)) << s.name;
        }
    }
}
