// Copyright 2026
// SPDX-License-Identifier: GPL-2.0-or-later

#include "transport_logic.h"

/* Device indices, duplicated from keyboards/linker/wireless/module.h so this
 * file stays free of the firmware include graph (and therefore testable). */
#define HSM_DEVS_USB 0
#define HSM_DEVS_BT1 1
#define HSM_DEVS_BT2 2
#define HSM_DEVS_BT3 3
#define HSM_DEVS_BT5 5
#define HSM_DEVS_2G4 6

/* Wireless keycodes, duplicated from the board's keycode block so this file
 * stays include-free (and testable). `keyboard.json` declares KC_BT1..KC_BT3 and
 * KC_2G4 as the first four keyboard keycodes, i.e. QK_KB_0..QK_KB_3 — a fixed,
 * contiguous range (0x7E00..0x7E03) the generated header preserves. */
#define HSM_KC_BT1 0x7E00
#define HSM_KC_BT2 0x7E01
#define HSM_KC_BT3 0x7E02
#define HSM_KC_2G4 0x7E03

uint8_t hsm_boot_devs(hsm_switch_pos_t pos, uint8_t last_bt) {
    switch (pos) {
        case HSM_SWITCH_USB:
            return HSM_DEVS_USB;
        case HSM_SWITCH_BT:
            /* The switch cannot say which of BT1..5; persistence supplies it,
             * clamped to the BT range so a corrupt value cannot select USB/2.4G. */
            return (last_bt >= HSM_DEVS_BT1 && last_bt <= HSM_DEVS_BT5) ? last_bt : HSM_DEVS_BT1;
        case HSM_SWITCH_2G4:
            return HSM_DEVS_2G4;
        case HSM_SWITCH_UNKNOWN:
        default:
            /* No position reads true: keep the persisted intent (the switch may
             * be absent or mid-travel). */
            return (last_bt >= HSM_DEVS_BT1 && last_bt <= HSM_DEVS_BT5) ? last_bt : HSM_DEVS_USB;
    }
}

bool hsm_keycode_allowed(hsm_switch_pos_t pos, uint8_t target_devs) {
    bool target_is_bt  = (target_devs >= HSM_DEVS_BT1 && target_devs <= HSM_DEVS_BT5);
    bool target_is_2g4 = (target_devs == HSM_DEVS_2G4);

    switch (pos) {
        case HSM_SWITCH_USB:
            /* A wireless keycode while the switch says USB would contradict the
             * authority the switch holds; refuse it. */
            return false;
        case HSM_SWITCH_BT:
            return target_is_bt;
        case HSM_SWITCH_2G4:
            return target_is_2g4;
        case HSM_SWITCH_UNKNOWN:
        default:
            /* Forgiving: the vendor firmware allowed acting without a readable
             * switch; preserve that so a broken switch is not a hard lock. */
            return true;
    }
}

uint8_t hsm_profile_of_devs(uint8_t devs) {
    /* The BT profile number IS the device index on a BT1..5 index (DEVS_BT1..5 =
     * 1..5), so the profile is derived, never stored. 0 means "no BT profile"
     * (USB / 2.4 GHz), which is what the readback should report then. */
    return (devs >= HSM_DEVS_BT1 && devs <= HSM_DEVS_BT5) ? devs : 0;
}

int16_t hsm_long_press_devs(uint16_t keycode) {
    switch (keycode) {
        case HSM_KC_BT1:
            return HSM_DEVS_BT1;
        case HSM_KC_BT2:
            return HSM_DEVS_BT2;
        case HSM_KC_BT3:
            return HSM_DEVS_BT3;
        case HSM_KC_2G4:
            return HSM_DEVS_2G4;
        default:
            /* No long-press action (includes EE_CLR, which the old handler
             * matched but deliberately did nothing for). */
            return -1;
    }
}

uint8_t hsm_seed_btdev(uint8_t current_devs, uint8_t last_bt) {
    /* On a BT profile the live device index is the sub-selection; otherwise the
     * switch is not on BT, so fall back to the persisted last BT index (clamped
     * to the BT range). This is what lets the scan seed from the *live* device
     * index instead of a RAM mirror of the stored one. */
    if (current_devs >= HSM_DEVS_BT1 && current_devs <= HSM_DEVS_BT5) {
        return current_devs;
    }
    return (last_bt >= HSM_DEVS_BT1 && last_bt <= HSM_DEVS_BT5) ? last_bt : HSM_DEVS_BT1;
}

uint8_t hsm_mode_seed(uint8_t current_devs) {
    /* The device index *is* the seed the mode scan compares against, so this is
     * the identity — named so the intent ("seed the edge-detector with the live
     * authority, never a persisted copy") is explicit and testable. */
    return current_devs;
}
