// Copyright 2026
// SPDX-License-Identifier: GPL-2.0-or-later

#include "split_logic.h"

#include <stddef.h>

/* Clamp an untrusted device index into the wire's valid range. Clamping (rather
 * than rejecting the command) keeps the sync's meaning -- "the master's index is
 * now X" -- while refusing to carry a value the module cannot select. Out of
 * range reads as USB, the safe floor: it can neither select a wrong wireless
 * profile nor tear the USB line down. */
static uint8_t clamp_devs(uint8_t devs) {
    return devs > WLS_SPLIT_DEVS_MAX ? WLS_SPLIT_DEVS_MIN : devs;
}

wls_split_action_t wls_split_decide_slave(uint8_t cmd, const uint8_t body[4]) {
    wls_split_action_t a = {WLS_SPLIT_ACT_NONE, 0, 0, false};

    switch (cmd) {
        case WLS_SPLIT_CMD_SYNC_MULTIMODE:
            a.kind      = WLS_SPLIT_ACT_SYNC_DEVICE_INDEX;
            a.last_devs = clamp_devs(body[0]);
            a.now_devs  = clamp_devs(body[1]);
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

bool wls_split_can_order_sleep(bool sends_wake_on_this_transport) {
    /* The cross-half contract: the master may only order the slave to stop when
     * it can also release it. The release (0xCC) is not sent on USB, and the
     * slave cannot release itself -- only the master's wake or the slave's own
     * `lpwr_stop_hook_post()` (which needs the slave's own LPWR_WAKEUP) cancels
     * the order. So if the wake is suppressed on this transport, no sleep may be
     * ordered: it would be a one-way order. */
    return sends_wake_on_this_transport;
}

wls_sleep_order_t wls_sleep_order_open(bool sends_wake_on_this_transport, uint8_t sleep_flag) {
    /* The handle IS the decision: permitted exactly when a release is reachable.
     * There is no path that produces a permitted handle for an unreachable wake,
     * so the shell cannot construct an order it is not allowed to send. */
    wls_sleep_order_t order = {wls_split_can_order_sleep(sends_wake_on_this_transport), sleep_flag};
    return order;
}

bool wls_sleep_order_permitted(const wls_sleep_order_t *order) {
    return order != NULL && order->permitted;
}

bool wls_split_can_send_relay(bool sends_release_on_this_transport) {
    /* The release side of the same contract. The 0xCC/0xBB pair relays LED-rail
     * power, so both are gated by the same fact: while cabled the rail must stay
     * up on both halves, and broadcasting a release would extinguish the slave's
     * backlight. This is the rule the sole 0xCC call site's early return encodes;
     * stating it here lets that site take it as a precondition instead of
     * re-deriving it, so the order side and the release side cannot disagree. */
    return sends_release_on_this_transport;
}

wls_sleep_order_t wls_sleep_release_open(bool sends_release_on_this_transport) {
    /* The handle IS the decision, on the release side: permitted exactly when the
     * release is actually sent on this transport. The flag field is unused here
     * (a release carries no body); it is left zero rather than invented. */
    wls_sleep_order_t release = {wls_split_can_send_relay(sends_release_on_this_transport), 0};
    return release;
}

bool wls_sleep_release_permitted(const wls_sleep_order_t *release) {
    return release != NULL && release->permitted;
}

bool wls_sleep_order_outlived_release(uint32_t sent_at_ms, uint32_t now_ms, bool release_transport_still_carries, bool released) {
    /* The liveness counterpart of the executor's safety precondition. A released
     * order is not outstanding; an order within the window may still be
     * released; and an order whose transport can still carry the release is
     * still releasable -- none of those is a fault. The fault is only the
     * residue: not released, window elapsed, and no release can come. */
    if (released) {
        return false;
    }
    if (release_transport_still_carries) {
        return false;
    }
    return (uint32_t)(now_ms - sent_at_ms) >= WLS_SLEEP_ORDER_RELEASE_MS;
}
