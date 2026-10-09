// Copyright 2024 Su (@isuua)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

typedef enum {
    LPWR_NORMAL = 0,
    LPWR_PRESLEEP,
    LPWR_STOP,
    LPWR_WAKEUP,
} lpwr_state_t;

typedef enum {
    LPWR_WAKEUP_NONE = 0,
    LPWR_WAKEUP_MATRIX,
    LPWR_WAKEUP_UART,
    LPWR_WAKEUP_CABLE,
    LPWR_WAKEUP_USB,
    LPWR_WAKEUP_ONEKEY,
    LPWR_WAKEUP_ENCODER,
    LPWR_WAKEUP_SWITCH,
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

lpwr_state_t lpwr_get_state(void);
lpwr_mode_t lpwr_get_mode(void);
uint32_t lpwr_timestamp_read(void);
uint32_t lpwr_timeout_value_read(void);
void lpwr_set_sleep_wakeupcd(lpwr_wakeupcd_t wakeupcd);
lpwr_wakeupcd_t lpwr_get_sleep_wakeupcd(void);
void lpwr_update_timestamp(void);
void lpwr_set_timeout_manual(bool enable);
bool lpwr_get_timeout_manual(void);
void lpwr_set_state(lpwr_state_t state);
void lpwr_set_mode(lpwr_mode_t mode);
void lpwr_task(void);
