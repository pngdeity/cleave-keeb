// Copyright 2026
// SPDX-License-Identifier: GPL-2.0-or-later

#include "split_logic.h"

wls_split_action_t wls_split_decide_slave(uint8_t cmd, const uint8_t body[4]) {
    wls_split_action_t a = {WLS_SPLIT_ACT_NONE, 0, 0, false};

    switch (cmd) {
        case WLS_SPLIT_CMD_SYNC_MULTIMODE:
            a.kind      = WLS_SPLIT_ACT_SYNC_DEVICE_INDEX;
            a.last_devs = body[0];
            a.now_devs  = body[1];
            break;
        case WLS_SPLIT_CMD_SLEEP:
            a.kind       = WLS_SPLIT_ACT_ORDER_SLEEP;
            a.sleep_flag = body[0] != 0;
            break;
        case WLS_SPLIT_CMD_POWER_DOWN:
            /* The master is suspending; the slave does nothing special -- it
             * must NOT force its own rail off (that is the defect-5 latch). */
            a.kind = WLS_SPLIT_ACT_NONE;
            break;
        case WLS_SPLIT_CMD_WAKEUP:
            a.kind = WLS_SPLIT_ACT_REAPPLY_RAIL;
            break;
        case WLS_SPLIT_CMD_RESET_SETTINGS:
            a.kind = WLS_SPLIT_ACT_RESET_SETTINGS;
            break;
        default:
            break;
    }

    return a;
}
