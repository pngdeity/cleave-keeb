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

/* ---------------------------------------------------------------------------
 * Idle-timeout admissibility, as a pure decision.
 *
 * The machine may enter PRESLEEP on an idle timeout, or immediately when a
 * one-shot *manual* override is pending. The override is consumable: once a
 * timeout-induced stop is actually allowed, the override has done its job. The
 * decision depends only on four facts, so it is pure; whether to *consume* the
 * override is the caller's act, not a side effect of asking.
 *
 * `hook_allows`    — the board hook permits a timeout stop at all.
 * `usb_active`     — the active host is USB and the USB driver is up.
 * `manual_pending` — a manual override is waiting to be honored.
 * `idle_elapsed`   — the idle timer has reached the configured timeout.
 *
 * Returns true when the machine may proceed to PRESLEEP.
 */
bool lpwr_timeout_allowed_decide(bool hook_allows, bool usb_active, bool manual_pending, bool idle_elapsed);

/* ---------------------------------------------------------------------------
 * Whether the machine may enter PRESLEEP, as a pure decision.
 *
 * This is the sequencing invariant that the 2026-10-15 dark-board fault was
 * missing. PRESLEEP has side effects it cannot undo by itself -- it disables the
 * RGB matrix -- and the only thing that reverses them runs in LPWR_WAKEUP, a
 * state a REFUSED stop never reaches (the refusal returns to LPWR_NORMAL). So a
 * stop that would be refused at the point of commitment must be refused BEFORE
 * the first irreversible effect, not after it.
 *
 * `timeout_allowed` — the idle/manual condition permits the sleep sequence.
 * `stop_allowed`    — the board's stop policy would permit the stop itself.
 *
 * Returns true only when both hold. Entering PRESLEEP on `timeout_allowed`
 * alone is the fault: it takes the side effects and then declines the stop,
 * leaving the matrix off with no path back.
 */
bool lpwr_presleep_is_allowed_decide(bool timeout_allowed, bool stop_allowed);
