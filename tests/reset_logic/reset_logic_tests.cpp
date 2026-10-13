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

/* Proof of the reset plan, without hardware.
 *
 * The one rule worth pinning is the asymmetry: a locally-requested reset
 * notifies the other half, and a reset the other half requested does not (else
 * the two halves would reset each other forever). The rest of the plan is a
 * fixed ordered tail, which these vectors also fix so a reordering shows up as
 * a test failure rather than a subtle behavioural change. */

#include "gtest/gtest.h"

extern "C" {
#include "keyboards/epomaker/epomaker_split65/wls/reset_logic.h"
}

namespace {

TEST(ResetLogic, local_reset_starts_by_notifying_the_peer) {
    wls_reset_plan_t plan = wls_reset_plan_of(WLS_RESET_CAUSE_LOCAL);
    ASSERT_EQ(4, plan.count);
    EXPECT_EQ(WLS_RESET_STEP_NOTIFY_PEER, plan.steps[0]);
    EXPECT_EQ(WLS_RESET_STEP_WIPE_CONFIG, plan.steps[1]);
    EXPECT_EQ(WLS_RESET_STEP_FLASH, plan.steps[2]);
    EXPECT_EQ(WLS_RESET_STEP_RERUN_BOOT, plan.steps[3]);
}

TEST(ResetLogic, remote_reset_never_echoes_to_the_peer) {
    wls_reset_plan_t plan = wls_reset_plan_of(WLS_RESET_CAUSE_REMOTE);
    ASSERT_EQ(3, plan.count);
    EXPECT_EQ(WLS_RESET_STEP_WIPE_CONFIG, plan.steps[0]);
    EXPECT_EQ(WLS_RESET_STEP_FLASH, plan.steps[1]);
    EXPECT_EQ(WLS_RESET_STEP_RERUN_BOOT, plan.steps[2]);
}

TEST(ResetLogic, no_plan_ever_contains_notify_peer_twice) {
    for (int cause = WLS_RESET_CAUSE_LOCAL; cause <= WLS_RESET_CAUSE_REMOTE; cause++) {
        wls_reset_plan_t plan = wls_reset_plan_of((wls_reset_cause_t)cause);
        int              seen = 0;
        for (uint8_t i = 0; i < plan.count; i++) {
            if (plan.steps[i] == WLS_RESET_STEP_NOTIFY_PEER) seen++;
        }
        EXPECT_EQ(cause == WLS_RESET_CAUSE_LOCAL ? 1 : 0, seen);
    }
}

TEST(ResetLogic, wipe_precedes_flash_which_precedes_rerun_boot) {
    wls_reset_plan_t plan = wls_reset_plan_of(WLS_RESET_CAUSE_REMOTE);
    int              wipe = -1, flash = -1, boot = -1;
    for (uint8_t i = 0; i < plan.count; i++) {
        if (plan.steps[i] == WLS_RESET_STEP_WIPE_CONFIG) wipe = i;
        if (plan.steps[i] == WLS_RESET_STEP_FLASH) flash = i;
        if (plan.steps[i] == WLS_RESET_STEP_RERUN_BOOT) boot = i;
    }
    EXPECT_LT(wipe, flash);
    EXPECT_LT(flash, boot);
}

} // namespace
