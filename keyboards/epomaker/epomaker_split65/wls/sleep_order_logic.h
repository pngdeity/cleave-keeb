// Copyright 2026
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Pure decision for the low-battery sleep order.
 *
 * `wls_indicator_battery()` held a time-driven rule inline: when the level is at
 * or below the stop threshold, start a 60 s timer on first sight; once that timer
 * elapses, order sleep. That is the same shape as the blink rule
 * (`wls/indicator_logic`) -- a small piece of state plus `now`, deciding one
 * action -- so it is expressed the same way, and the shell holds no policy.
 *
 * The *whether to sleep at all* predicate remains the pure
 * `hsm_should_order_sleep()` (wls/transport_logic); this core owns only the
 * *timing* (has the arm window elapsed?). */

/* How long the level must stay critical before the order is given. */
#define WLS_SLEEP_ORDER_ARM_MS 60000

/* The timing state: 0 = not armed; otherwise the moment the arm began. */
typedef struct {
    uint32_t armed_ms;
} wls_sleep_order_t;

#define WLS_SLEEP_ORDER_IDLE ((wls_sleep_order_t){0})

typedef enum {
    WLS_SLEEP_ORDER_HOLD = 0, /* nothing to do; keep the state as returned */
    WLS_SLEEP_ORDER_NOW,      /* order sleep (and the state is reset) */
} wls_sleep_order_out_t;

/* Advance the timing state for one tick. `critical` means the level is at or
 * below the stop threshold (and not charging) -- i.e. the arm condition holds.
 * Mutates `s` in place: arms on the first critical tick, clears when not
 * critical, and clears when it fires. Returns whether to order sleep now. Pure:
 * depends only on (`*s`, `critical`, `now_ms`). */
wls_sleep_order_out_t wls_sleep_order_tick(wls_sleep_order_t *s, bool critical, uint32_t now_ms);
