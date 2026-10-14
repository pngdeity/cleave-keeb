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
    const uint8_t corrupt[4] = {0xFF, 0xFF, 0, 0};
    wls_split_action_t a     = wls_split_decide_slave(0x55, corrupt);
    EXPECT_EQ(WLS_SPLIT_ACT_SYNC_DEVICE_INDEX, a.kind);
    EXPECT_EQ(WLS_SPLIT_DEVS_MIN, a.last_devs);
    EXPECT_EQ(WLS_SPLIT_DEVS_MIN, a.now_devs);
}

TEST(SplitLogic, sync_multimode_accepts_the_full_device_range) {
    /* The clamp must not narrow the legitimate range: every index the board
     * uses round-trips unchanged -- USB (0), BT1..BT5 (1..5), 2.4 GHz (6). */
    for (uint8_t devs = WLS_SPLIT_DEVS_MIN; devs <= WLS_SPLIT_DEVS_MAX; devs++) {
        const uint8_t body[4] = {devs, devs, 0, 0};
        wls_split_action_t a  = wls_split_decide_slave(0x55, body);
        EXPECT_EQ(devs, a.last_devs);
        EXPECT_EQ(devs, a.now_devs);
    }
}

} // namespace
