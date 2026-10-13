// Copyright 2026
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Pure layer/filp key-remap decision (the board's `process_record_kb` remap).
 *
 * Under `confinfo.filp`, a set of keys is replaced by a layer-appropriate
 * action and swallowed: the F-row becomes media keys on the macOS layer, and
 * the digits / `-` / `=` become media or F-keys. Historically that rule was
 * written out once per keycode as 24 near-identical 10-line switch blocks that
 * interleaved the *decision* (which layer, remap or not) with the *effect*
 * (register/unregister a keycode). This is the decision alone, as a pure
 * function of (keycode, is_mac): the shell performs the returned action. It is
 * exhaustively testable, and the mapping is data a reviewer can read.
 *
 * The caller has *already* checked `confinfo.filp`; this core answers only "for
 * this key on this layer, what should happen?".
 */

/* What the shell should do with a remapped key. */
typedef enum {
    FILP_ACT_NONE = 0, /* not a remapped key: the caller's normal path applies */
    FILP_ACT_KEY,      /* emit `key` (may be KC_NO: swallow, emit nothing) */
    FILP_ACT_CHORD,    /* hold `mod` + `base` together (macOS GUI chords) */
    FILP_ACT_RGB_VAL,  /* change the RGB value: `delta` is +1 or -1 */
} filp_action_kind_t;

/* The decision. For FILP_ACT_KEY, `key` is the keycode to emit; for
 * FILP_ACT_CHORD, `mod`/`base` are the two keys; for FILP_ACT_RGB_VAL, `delta`
 * is the direction. Fields not used by `kind` are 0. */
typedef struct {
    filp_action_kind_t kind;
    uint16_t           key;
    uint16_t           mod;
    uint16_t           base;
    int8_t             delta;
} filp_action_t;

/* Decide the remap for `keycode` on the macOS (`is_mac` true) or Windows layer.
 * Returns FILP_ACT_NONE when the key is not remapped on this layer, in which
 * case the caller lets the key through unchanged. Pure: the result depends only
 * on (keycode, is_mac). */
filp_action_t filp_decide(uint16_t keycode, bool is_mac);
