// Copyright 2026
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Pure transport-selection decisions, extracted from the board's switch scan
 * (`wls/wls.c`) so the transport state machine can be tested without the MCU.
 *
 * The physical 3-position switch is absolute authority for its own domain
 * (USB / BT / 2.4 GHz); software persistence may only model what the switch
 * cannot express — which of BT1..5, once the switch merely says "BT". The defect
 * this encodes against is a *persisted* transport surviving a reboot with the
 * switch elsewhere ("enumerated but no keys"; TODO.md defect 4). */

/* Which switch position the two pins encode. */
typedef enum {
    HSM_SWITCH_UNKNOWN = 0,
    HSM_SWITCH_USB,
    HSM_SWITCH_BT,
    HSM_SWITCH_2G4,
} hsm_switch_pos_t;

/* The device index the switch position selects at boot, given the persisted
 * sub-selection (`last_bt`) to use if the switch says BT. Returns one of
 * DEVS_USB / DEVS_BT1..5 / DEVS_2G4 (values 0..6, matching `module.h`). */
uint8_t hsm_boot_devs(hsm_switch_pos_t pos, uint8_t last_bt);

/* Whether a keycode may act, given the current switch position and target.
 * A BT key may only act in the BT position (or an unknown/forgiving one); a
 * 2.4 GHz key only in the 2.4 GHz position. This keeps a keycode from crossing
 * modes the switch does not allow. */
bool hsm_keycode_allowed(hsm_switch_pos_t pos, uint8_t target_devs);

/* The device index a wireless keycode re-pairs on a long-press, or -1 when the
 * keycode has no long-press action. Pure: the keycode alone decides the target,
 * so the caller can capture the value at arm time instead of holding a pointer
 * into shared state that every later keypress overwrites (the defect this
 * encodes against). */
int16_t hsm_long_press_devs(uint16_t keycode);
