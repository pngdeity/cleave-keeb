// Copyright 2026
// SPDX-License-Identifier: GPL-2.0-or-later

#include "reset_logic.h"

wls_reset_plan_t wls_reset_plan_of(wls_reset_cause_t cause) {
    wls_reset_plan_t plan = {0};
    uint8_t          n    = 0;

    /* Only a locally-requested reset tells the other half; a reset the other
     * half asked for would otherwise bounce back and forth. */
    if (cause == WLS_RESET_CAUSE_LOCAL) {
        plan.steps[n++] = WLS_RESET_STEP_NOTIFY_PEER;
    }

    plan.steps[n++] = WLS_RESET_STEP_WIPE_CONFIG;
    plan.steps[n++] = WLS_RESET_STEP_FLASH;
    plan.steps[n++] = WLS_RESET_STEP_RERUN_BOOT;

    plan.count = n;
    return plan;
}
