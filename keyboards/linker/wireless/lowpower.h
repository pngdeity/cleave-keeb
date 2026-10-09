// Copyright 2024 Su (@isuua)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

typedef enum {
    LPWR_NORMAL = 0,
    LPWR_PRESLEEP,
    LPWR_STOP,
    LPWR_WAKEUP,
} lpwr_state_t;

/* Wake codes are a *set*, not the last one seen: more than one source can fire
 * within the same stop window and every one of them must survive to be
 * interpreted. Each member is therefore its own bit, and the accessors below
 * accumulate. LPWR_WAKEUP_NONE = 0 keeps "nothing fired" representable. */
typedef enum {
    LPWR_WAKEUP_NONE    = 0,
    LPWR_WAKEUP_MATRIX  = 1 << 0,
    LPWR_WAKEUP_UART    = 1 << 1,
    LPWR_WAKEUP_CABLE   = 1 << 2,
    LPWR_WAKEUP_USB     = 1 << 3,
    LPWR_WAKEUP_ONEKEY  = 1 << 4,
    LPWR_WAKEUP_ENCODER = 1 << 5,
    LPWR_WAKEUP_SWITCH  = 1 << 6,
} lpwr_wakeupcd_t;

typedef enum {
    LPWR_MODE_TIMEOUT = 0,
} lpwr_mode_t;

/* Sleep policy contract. The low-power state machine will not commit to a stop
 * unless this returns true, so a board can refuse any sleep it knows is unsafe.
 * The default (weak) implementation returns true: a board that is always safe
 * to stop needs to do nothing and keeps the stack's behaviour unchanged. A board
 * whose stop is only safe on some paths (no interpreted wake source, or an
 * unordered sleep) implements this to report whether the present stop is one it
 * may take. The check runs at the single point of commitment (`lpwr_stop_cb()`)
 * before `lpwr_enter_stop()`, so the decision cannot be bypassed by any caller. */
bool lpwr_stop_is_allowed(void);

/* Wake interpretation contract. After a stop the machine looks at the set of
 * wake codes that fired and asks this board which of them it actually arms. A
 * wake is honoured only if at least one fired code is in the armed set; any
 * other result is treated as a phantom and the stop is re-entered.
 *
 * The reason this must be board-supplied: the WB32 EXTI is pad-numbered, one
 * channel per pad, and the port is discarded before the callback runs. On this
 * board the module UART RX shares its pad with a matrix column, so a column
 * edge is reported as LPWR_WAKEUP_UART even though the board never arms UART
 * wake. Without this contract that phantom is acted on as a real wake, is
 * routed back to LPWR_STOP with lpwr_wakeup_cb() skipped, and the half comes to
 * rest dark and unresponsive.
 *
 * The default (weak) implementation reports every code armed, so a board with
 * no pad aliasing behaves exactly as before. A board that arms only some
 * sources returns the mask of those it arms. */
uint32_t lpwr_wakeup_armed_mask(void);

lpwr_state_t lpwr_get_state(void);
lpwr_mode_t lpwr_get_mode(void);
uint32_t lpwr_timestamp_read(void);
uint32_t lpwr_timeout_value_read(void);
void lpwr_set_sleep_wakeupcd(lpwr_wakeupcd_t wakeupcd);
uint32_t lpwr_get_sleep_wakeupcd(void);
void lpwr_update_timestamp(void);
void lpwr_set_timeout_manual(bool enable);
bool lpwr_get_timeout_manual(void);
void lpwr_set_state(lpwr_state_t state);
void lpwr_set_mode(lpwr_mode_t mode);
void lpwr_task(void);
