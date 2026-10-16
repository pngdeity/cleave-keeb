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

bool lpwr_excluded_code_is_safe(bool excluded, bool covered) {
    /* A code the board does not arm is safe to exclude ONLY if the pad it is
     * reported on is still reachable through an armed code. If it is not, an
     * edge on that pad is stamped with the excluded code and dropped -- a real
     * wake silently discarded. So: not excluded, or covered. */
    return !excluded || covered;
}

uint32_t lpwr_code_of_pad(const lpwr_attribution_t *attr, uint8_t pad, bool is_master) {
    /* The classifier, as a function of the pad and the board's attribution. The
     * order is the order the pins are named in `lpwr_attribution_t`; the mode
     * switch pads exist only on the master, so on the slave they fall through to
     * MATRIX -- which is correct, because the slave never registers them.
     *
     * MATRIX is the default and NOT a case: a pad the board registered no
     * specific code under reports MATRIX. */
    if (pad == attr->uart_pad) {
        return attr->uart_code;
    }
    if (pad == attr->usb_pad) {
        return attr->usb_code;
    }
    if (pad == attr->cable_pad) {
        return attr->cable_code;
    }
    if (is_master) {
        if (pad == attr->bt_pad) {
            return attr->bt_code;
        }
        if (pad == attr->g24_pad) {
            return attr->g24_code;
        }
    }
    return attr->default_code;
}

uint8_t lpwr_pad_of_code(const lpwr_attribution_t *attr, uint32_t code, bool is_master) {
    if (code == attr->uart_code) {
        return attr->uart_pad;
    }
    if (code == attr->usb_code) {
        return attr->usb_pad;
    }
    if (code == attr->cable_code) {
        return attr->cable_pad;
    }
    if (is_master) {
        if (code == attr->bt_code) {
            return attr->bt_pad;
        }
        if (code == attr->g24_code) {
            return attr->g24_pad;
        }
    }
    return LPWR_PAD_NONE;
}

bool lpwr_pads_collide(uint8_t pad_a, uint8_t pad_b) {
    /* `PAL_PAD(line) = line & 0x0F`, so the port is discarded and two pins
     * collide exactly when their pad numbers are equal. */
    return pad_a == pad_b;
}

bool lpwr_arm_is_coherent(bool orders_sleep, bool arms_timeout) {
    /* An order without an arm can never be taken, so it is incoherent. An arm
     * without an order is fine: the stop policy refuses the unordered stop, so
     * nothing unsafe happens -- that is exactly the S1 case. */
    return !orders_sleep || arms_timeout;
}

bool lpwr_timeout_allowed_decide(bool hook_allows, bool usb_active, bool manual_pending, bool idle_elapsed) {
    /* A board hook may forbid the idle timeout outright (including a pending
     * manual override: the board knows a stop would be unsafe). A live USB host
     * is never timed out. Otherwise a timeout stop is allowed when either a
     * manual override is pending or the idle timer has elapsed. */
    if (!hook_allows) {
        return false;
    }
    if (usb_active) {
        return false;
    }
    return manual_pending || idle_elapsed;
}

bool lpwr_presleep_is_allowed_decide(bool timeout_allowed, bool stop_allowed) {
    /* Both must hold. The second conjunct is the whole point: PRESLEEP's side
     * effects are irreversible from inside the sleep sequence, so entering it
     * for a stop that will later be refused strands the board with the matrix
     * disabled and no reachable re-enable. A refused stop must be refused here,
     * before the effects, not at the point of commitment after them. */
    return timeout_allowed && stop_allowed;
}
