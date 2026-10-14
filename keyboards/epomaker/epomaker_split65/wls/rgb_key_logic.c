// Copyright 2026
// SPDX-License-Identifier: GPL-2.0-or-later

#include "rgb_key_logic.h"

#include "keycodes.h"

wls_rgb_key_action_t wls_rgb_key_decide(uint16_t keycode, const wls_rgb_state_t *state) {
    wls_rgb_key_action_t action = {WLS_RGB_KEY_NONE, 0, false};

    if (!state->pressed) {
        return action;
    }

    switch (keycode) {
        case QK_RGB_MATRIX_SPEED_UP:
            if (state->speed >= WLS_RGB_SPEED_HIGH_ACK) {
                action.kind = WLS_RGB_KEY_ACK_DIR;
            }
            break;
        case QK_RGB_MATRIX_SPEED_DOWN:
            if (state->speed <= WLS_RGB_SPEED_LOW_ACK) {
                action.kind = WLS_RGB_KEY_ACK_DIR;
            }
            break;
        case QK_RGB_MATRIX_MODE_NEXT:
            if (state->mode == WLS_RGB_MODE_STEP_FROM) {
                action.kind  = WLS_RGB_KEY_SET_MODE;
                action.param = WLS_RGB_MODE_STEP_TO;
            }
            break;
        case QK_RGB_MATRIX_VALUE_UP:
            /* Always (re)enables the matrix; additionally acknowledges at speed. */
            action.enable = true;
            action.kind   = (state->speed >= WLS_RGB_VALUE_UP_ACK_SPEED) ? WLS_RGB_KEY_ACK_DIR : WLS_RGB_KEY_NONE;
            break;
        case QK_RGB_MATRIX_VALUE_DOWN:
            if (state->val <= state->val_step) {
                action.kind = WLS_RGB_KEY_BLANK_ALL;
            } else if (state->speed <= WLS_RGB_VALUE_DOWN_ACK_SPEED) {
                action.kind = WLS_RGB_KEY_ACK_DIR;
            }
            break;
        default:
            break;
    }

    return action;
}
