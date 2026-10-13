// Copyright 2026
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include QMK_KEYBOARD_H

#include <stdbool.h>
#include <stdint.h>

/* The layer ids. `_BL`/`_FL` are the base layers, `_MBL`/`_MFL` the macOS
 * variants; the filp and layer-keycode rules depend on which default layer is
 * active, so both the keymap concern and the indicator pass name them. */
enum layers {
    _BL = 0,
    _FL,
    _MBL,
    _MFL,
};

/* True when the active default layer is a macOS variant. The single statement of
 * the mac/Windows distinction (the pure `filp_decide()` and the layer-keycode
 * logic both consume it). */
#define keymap_is_mac_system() ((get_highest_layer(default_layer_state) == _MBL) || (get_highest_layer(default_layer_state) == _MFL))

/* The board's keycode concern: the wireless keycodes, the Ctrl-combo macros,
 * the filp remap's effect half, the RGB/rgblight keycodes, the layer keys.
 *
 * The *decisions* are already pure and tested — `wls/keymap_logic`
 * (`filp_decide`) and `wls/transport_logic` (`hsm_*`). What lives here is the
 * effect shell that was previously interleaved into a single 400-line
 * `process_record_kb` switch: the emit, the persist, the indicator arming, and
 * the deferred long-press.
 *
 * `wls_keymap_process_record()` runs the board's whole non-QMK keycode chain.
 * It is called from `process_record_kb` after the QMK-mandated
 * `process_record_user()` delegation, and returns the same contract (`true` to
 * let the key continue, `false` to swallow it). */

bool wls_keymap_process_record(uint16_t keycode, keyrecord_t *record);

/* The rgblight mode table the `RL_MOD` keycode steps through, and the boot
 * default (`rgblight_mode(wls_rgblight_modes[0])`). One table, two readers. */
extern const uint8_t wls_rgblight_modes[];

/* Persist the user's explicit transport choice (the vendor device index and, for
 * BT1..BT5, the sub-profile the switch cannot express). Called only from the
 * keycode path, so the switch stays level-authoritative (defect 4). */
void wls_persist_devs(void);

/* The deferred long-press worker (re-pair / re-select after
 * `WLS_KEYCODE_PAIR_TIME`). Exposed because `defer_exec()` stores the function
 * pointer. */
uint32_t wls_keymap_long_press(uint32_t trigger_time, void *cb_arg);
