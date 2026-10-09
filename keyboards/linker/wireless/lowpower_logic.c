// Copyright 2024 Su (@isuua)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "lowpower_logic.h"

bool lpwr_stop_is_allowed_decide(bool is_master, bool lower_sleep) {
    /* Only the master may take the stop, and only when it is ordered (the
     * low-battery path sets lower_sleep, which arms the wake sources first).
     * Any other stop is un-ordered and leaves the half unrecoverable. */
    return is_master && lower_sleep;
}

bool lpwr_wakeup_is_real(uint32_t wake_set, uint32_t armed_mask) {
    /* A set, not the last code seen: a phantom code that aliases onto a pad the
     * board arms must not by itself count as a wake, but must not mask a real
     * code that fired in the same cycle either. */
    return (wake_set & armed_mask) != 0;
}
