// Copyright 2024 Su (@isuua)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Pure decisions extracted from the low-power state machine (`lowpower.c`).
 *
 * These are the parts of a stop that depend only on their arguments: whether a
 * stop is permitted, and whether the codes left by the wake hardware count as a
 * real wake. They carry no ChibiOS, no registers and no globals, so the unit
 * tests compile them directly — the imperative shell that touches the MCU stays
 * in `lowpower.c`.
 *
 * The wake codes are a *set* of bit flags (`lpwr_wakeupcd_t`). Comparing a set
 * against a mask, rather than switching on the last code seen, is what makes a
 * pad aliased to another function (the WB32 EXTI reports by pad number only) a
 * phantom rather than a phantom-triggered wake. */

/* Whether a stop may be taken, given the two facts the policy depends on. */
bool lpwr_stop_is_allowed_decide(bool is_master, bool lower_sleep);

/* Whether any code in the fired set belongs to the set this board arms. */
bool lpwr_wakeup_is_real(uint32_t wake_set, uint32_t armed_mask);
