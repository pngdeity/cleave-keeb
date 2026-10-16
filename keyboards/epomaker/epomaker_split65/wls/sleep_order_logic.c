// Copyright 2026
// SPDX-License-Identifier: GPL-2.0-or-later

#include "sleep_order_logic.h"

wls_sleep_arm_out_t wls_sleep_arm_tick(wls_sleep_arm_t *s, bool critical, uint32_t now_ms) {
    if (!critical) {
        s->armed_ms = 0;
        return WLS_SLEEP_ARM_HOLD;
    }

    if (!s->armed_ms) {
        s->armed_ms = now_ms ? now_ms : 1; /* avoid the 0 sentinel */
        return WLS_SLEEP_ARM_HOLD;
    }

    if ((uint32_t)(now_ms - s->armed_ms) >= WLS_SLEEP_ORDER_ARM_MS) {
        s->armed_ms = 0;
        return WLS_SLEEP_ARM_NOW;
    }

    return WLS_SLEEP_ARM_HOLD;
}
