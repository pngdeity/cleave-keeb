// Copyright 2026
// SPDX-License-Identifier: GPL-2.0-or-later

#include "transport_logic.h"

/* Device indices, duplicated from keyboards/linker/wireless/module.h so this
 * file stays free of the firmware include graph (and therefore testable). */
#define HSM_DEVS_USB 0
#define HSM_DEVS_BT1 1
#define HSM_DEVS_BT5 5
#define HSM_DEVS_2G4 6

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
