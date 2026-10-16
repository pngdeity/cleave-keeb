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

/* Proof of the split-link slave decision, without hardware.
 *
 * The slave's handler was a switch over the command byte plus a body read; these
 * vectors pin what each command must do, so the master's senders and the slave's
 * handler cannot drift apart:
 *   - 0x55 syncs the device index (body[0]=last, body[1]=now);
 *   - 0xAA orders sleep (body[0] is the flag);
 *   - 0xBB (master suspending) is a deliberate no-op on the slave (defect 5);
 *   - 0xCC re-applies the LED rail;
 *   - 0xDD resets settings;
 *   - any other byte decodes to NONE (the slave must not act on the unknown).
 */

#include "gtest/gtest.h"

extern "C" {
#include "keyboards/epomaker/epomaker_split65/wls/split_logic.h"
}

namespace {

TEST(SplitLogic, sync_multimode_carries_both_device_indices) {
    const uint8_t      body[4] = {2, 6, 0, 0};
    wls_split_action_t a       = wls_split_decide_slave(0x55, body);
    EXPECT_EQ(WLS_SPLIT_ACT_SYNC_DEVICE_INDEX, a.kind);
    EXPECT_EQ(2, a.last_devs);
    EXPECT_EQ(6, a.now_devs);
}

TEST(SplitLogic, sleep_decodes_the_order_flag) {
    const uint8_t body_true[4]  = {1, 0, 0, 0};
    const uint8_t body_false[4] = {0, 0, 0, 0};
    EXPECT_EQ(WLS_SPLIT_ACT_ORDER_SLEEP, wls_split_decide_slave(0xAA, body_true).kind);
    EXPECT_TRUE(wls_split_decide_slave(0xAA, body_true).sleep_flag);
    EXPECT_FALSE(wls_split_decide_slave(0xAA, body_false).sleep_flag);
}

TEST(SplitLogic, power_down_is_a_deliberate_no_op_on_the_slave) {
    /* Defect 5: the slave must not force its own rail off when the master
     * suspends. The decision is NONE, not an action. */
    const uint8_t body[4] = {0, 0, 0, 0};
    EXPECT_EQ(WLS_SPLIT_ACT_NONE, wls_split_decide_slave(0xBB, body).kind);
}

TEST(SplitLogic, wakeup_reapplies_the_rail) {
    const uint8_t body[4] = {0, 0, 0, 0};
    EXPECT_EQ(WLS_SPLIT_ACT_REAPPLY_RAIL, wls_split_decide_slave(0xCC, body).kind);
}

TEST(SplitLogic, reset_settings_is_its_own_action) {
    const uint8_t body[4] = {0, 0, 0, 0};
    EXPECT_EQ(WLS_SPLIT_ACT_RESET_SETTINGS, wls_split_decide_slave(0xDD, body).kind);
}

TEST(SplitLogic, unknown_command_is_none) {
    const uint8_t body[4] = {1, 2, 3, 4};
    EXPECT_EQ(WLS_SPLIT_ACT_NONE, wls_split_decide_slave(0x00, body).kind);
    EXPECT_EQ(WLS_SPLIT_ACT_NONE, wls_split_decide_slave(0xEE, body).kind);
    EXPECT_EQ(WLS_SPLIT_ACT_NONE, wls_split_decide_slave(0xFF, body).kind);
}

TEST(SplitLogic, sync_multimode_clamps_a_corrupt_device_index) {
    /* The body is untrusted wire input: a corrupted byte must not become board
     * state. 0xFF would otherwise reach `wls_module_devs_change()` and
     * `hsm_host_of_devs()` -- which maps anything outside DEVS_USB..DEVS_2G4 to
     * a transport, so 0xFF read as BLUETOOTH. Out of range clamps to USB, the
     * safe floor (it can neither select a wrong wireless profile nor tear the
     * USB line down). */
    const uint8_t      corrupt[4] = {0xFF, 0xFF, 0, 0};
    wls_split_action_t a          = wls_split_decide_slave(0x55, corrupt);
    EXPECT_EQ(WLS_SPLIT_ACT_SYNC_DEVICE_INDEX, a.kind);
    EXPECT_EQ(WLS_SPLIT_DEVS_MIN, a.last_devs);
    EXPECT_EQ(WLS_SPLIT_DEVS_MIN, a.now_devs);
}

TEST(SplitLogic, sync_multimode_accepts_the_full_device_range) {
    /* The clamp must not narrow the legitimate range: every index the board
     * uses round-trips unchanged -- USB (0), BT1..BT5 (1..5), 2.4 GHz (6). */
    for (uint8_t devs = WLS_SPLIT_DEVS_MIN; devs <= WLS_SPLIT_DEVS_MAX; devs++) {
        const uint8_t      body[4] = {devs, devs, 0, 0};
        wls_split_action_t a       = wls_split_decide_slave(0x55, body);
        EXPECT_EQ(devs, a.last_devs);
        EXPECT_EQ(devs, a.now_devs);
    }
}

/* --- The cross-half sleep contract (regression 2026-10-15) -----------------
 *
 * The master orders the slave to stop with 0xAA, which makes the slave arm its
 * OWN stop; the only command that releases it is 0xCC, and 0xCC has exactly one
 * call site, behind an early return on USB. So ordering a sleep on a transport
 * whose wake is suppressed is a ONE-WAY order: the slave is left dark and
 * silent, recoverable only by power-cycling both halves. That was the right
 * half of the 30 s dark board.
 *
 * The rule is stated here as a pure decision, so the two halves' agreement is
 * pinned by a test rather than by matching the master's send site against the
 * slave's handler by hand. */

TEST(SplitLogic, may_not_order_sleep_when_the_wake_is_suppressed) {
    /* The fault case: this transport never sends 0xCC, so the order would never
     * be released. Refuse it. */
    EXPECT_FALSE(wls_split_can_order_sleep(/*sends_wake=*/false));
}

TEST(SplitLogic, may_order_sleep_when_the_wake_is_reachable) {
    EXPECT_TRUE(wls_split_can_order_sleep(/*sends_wake=*/true));
}

TEST(SplitLogic, the_contract_is_exactly_that_the_wake_must_be_reachable) {
    /* Composed the way the shell composes it: the master derives
     * `sends_wake` from the transport (0xCC is skipped on USB), and the
     * decision consumes it. On USB -- the transport the user returns to -- the
     * order must be refused; on any other transport it may proceed. This is the
     * exact state that stranded the right half. */
    bool on_usb         = true;
    bool sends_wake_usb = (on_usb != true); /* wireless_get_current_devs() == DEVS_USB */
    EXPECT_FALSE(wls_split_can_order_sleep(sends_wake_usb));

    bool on_bt         = false;
    bool sends_wake_bt = (on_bt != true);
    EXPECT_TRUE(wls_split_can_order_sleep(sends_wake_bt));
}

/* --- Rung 3: the order executor, where the right is a PRECONDITION ---------
 *
 * `wls_split_can_order_sleep()` alone is a convention: a caller may forget to
 * ask, or may send anyway. The executor makes the reachable-release fact a
 * precondition of CONSTRUCTING the order, so "an order exists without a
 * reachable release" is not representable. These vectors pin exactly that, at
 * the construction site. */

TEST(SplitLogic, the_executor_cannot_be_opened_permitted_when_the_wake_is_suppressed) {
    /* The one-way order is not merely refused -- it cannot be built. On a
     * transport that never sends 0xCC, every handle is unpermitted regardless of
     * the flag, so the shell has nothing to send. */
    wls_sleep_order_t refused_flag_one  = wls_sleep_order_open(/*sends_wake=*/false, 1);
    wls_sleep_order_t refused_flag_zero = wls_sleep_order_open(/*sends_wake=*/false, 0);
    EXPECT_FALSE(wls_sleep_order_permitted(&refused_flag_one));
    EXPECT_FALSE(wls_sleep_order_permitted(&refused_flag_zero));
}

TEST(SplitLogic, the_executor_opens_permitted_when_the_release_is_reachable) {
    wls_sleep_order_t order = wls_sleep_order_open(/*sends_wake=*/true, /*flag=*/1);
    EXPECT_TRUE(wls_sleep_order_permitted(&order));
}

TEST(SplitLogic, the_executor_carries_the_sleep_flag_unchanged) {
    /* The handle carries the body[0] the slave reads as its ordered-sleep flag.
     * It must survive construction untouched -- the shell must not be able to
     * reach a permitted order with a flag the transport did not imply. */
    EXPECT_EQ(1, wls_sleep_order_open(true, 1).sleep_flag);
    EXPECT_EQ(0, wls_sleep_order_open(true, 0).sleep_flag);
}

TEST(SplitLogic, the_executor_agrees_with_the_contract_it_replaces) {
    /* The handle's permission is exactly `wls_split_can_order_sleep()`, for both
     * transports: the stricter form cannot be more permissive than the rule it
     * formalises, or it would open a hole where the plain rule had none. */
    for (int sends_wake = 0; sends_wake <= 1; sends_wake++) {
        wls_sleep_order_t order = wls_sleep_order_open(sends_wake != 0, 1);
        EXPECT_EQ(wls_split_can_order_sleep(sends_wake != 0), wls_sleep_order_permitted(&order));
    }
}

TEST(SplitLogic, a_null_executor_is_never_permitted) {
    EXPECT_FALSE(wls_sleep_order_permitted(NULL));
}

} // namespace
