// Copyright 2026
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Pure decisions for the RGB / rgblight keycodes.
 *
 * The four `QK_RGB_MATRIX_*` keycodes each carried their own threshold
 * comparison and side effect inline in `process_record_kb`'s switch -- magic
 * numbers (215, 120, 95, 30, and the mode literal 29 -> 31) with no test
 * coverage, which is the shape that produced defects 7 and 8 (policy stated in
 * imperative code, restated per site). The thresholds are one rule; here they
 * are named and the decision is a value.
 *
 * The shell (`wls_keymap.c`) supplies the live speed/value/mode and applies the
 * returned instruction. The numbers are the *policy*; the shell holds none. */

/* The threshold at or above which a speed-up key acknowledges at the rail; the
 * one at or below which a speed-down acknowledges. */
#define WLS_RGB_SPEED_HIGH_ACK 215
#define WLS_RGB_SPEED_LOW_ACK 95
/* The value-up key acknowledges at this speed and above. */
#define WLS_RGB_VALUE_UP_ACK_SPEED 120
/* The value-down key acknowledges at this speed and below. */
#define WLS_RGB_VALUE_DOWN_ACK_SPEED 30
/* The one mode that steps to another rather than advancing normally. */
#define WLS_RGB_MODE_STEP_FROM 29
#define WLS_RGB_MODE_STEP_TO 31

/* What the shell should do for an RGB keycode. Exactly one instruction. */
typedef enum {
    WLS_RGB_KEY_NONE = 0,  /* nothing to do (or not an RGB keycode) */
    WLS_RGB_KEY_ACK_DIR,   /* flash the four direction LEDs */
    WLS_RGB_KEY_BLANK_ALL, /* rail apply + blank every LED */
    WLS_RGB_KEY_SET_MODE,  /* jump to `param` */
} wls_rgb_key_kind_t;

typedef struct {
    wls_rgb_key_kind_t kind;
    uint8_t            param;  /* SET_MODE: the mode to jump to */
    bool               enable; /* also rgb_matrix_enable() + rail apply first */
} wls_rgb_key_action_t;

/* The live RGB state the decision reads. Values, not firmware calls. `val_step`
 * is the matrix's minimum value step (QMK's `RGB_MATRIX_VAL_STEP`), passed in so
 * this core stays free of firmware headers. */
typedef struct {
    uint8_t speed;
    uint8_t val;
    uint8_t val_step;
    uint8_t mode;
    bool    pressed;
} wls_rgb_state_t;

/* Decide the instruction for `keycode` (one of the four `QK_RGB_MATRIX_*`
 * keys), or NONE for anything else. Pure: no firmware state, no side effects. */
wls_rgb_key_action_t wls_rgb_key_decide(uint16_t keycode, const wls_rgb_state_t *state);
