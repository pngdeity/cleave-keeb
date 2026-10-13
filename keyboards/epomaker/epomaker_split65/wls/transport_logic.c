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

/* The board's numeric mode (the `now_mode` the pin read produces in wls.c),
 * as a `hsm_switch_pos_t`. Kept here so the resolution rule takes the switch
 * position in one canonical form wherever it is derived. */
hsm_switch_pos_t hsm_switch_pos_of_mode(uint8_t now_mode);

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

/* Connection-host values (HSM_HOST_*) are defined in the header. */

uint8_t hsm_host_of_devs(uint8_t devs) {
    if (devs == HSM_DEVS_USB) {
        return HSM_HOST_USB;
    }
    if (devs == HSM_DEVS_2G4) {
        return HSM_HOST_2P4GHZ;
    }
    /* Every BT index, and anything unrecognised, is a wireless BT host: the
     * default that the vendor transport falls back to. */
    return HSM_HOST_BLUETOOTH;
}

hsm_switch_apply_t hsm_switch_apply(hsm_switch_pos_t pos, uint8_t last_bt) {
    hsm_switch_apply_t result;
    result.devs = hsm_boot_devs(pos, last_bt);
    result.host = hsm_host_of_devs(result.devs);
    return result;
}

hsm_switch_pos_t hsm_switch_pos_of_mode(uint8_t now_mode) {
    /* Mirrors the pin read in wls.c: 3 = USB, 1 = BT, 2 = 2.4 GHz, else unknown.
     * Any value outside the three known positions is unknown, which the rule
     * treats as "keep the persisted intent". */
    switch (now_mode) {
        case 3:
            return HSM_SWITCH_USB;
        case 1:
            return HSM_SWITCH_BT;
        case 2:
            return HSM_SWITCH_2G4;
        default:
            return HSM_SWITCH_UNKNOWN;
    }
}

hsm_switch_pos_t hsm_switch_pos_of_hs_mode(uint8_t hs_mode) {
    /* The detector's output vocabulary (`enum modeio_mode`): hs_usb=1, hs_bt=2,
     * hs_2g4=3. `hs_none`/`hs_wireless`/anything else -> unknown. Note this is a
     * *different* numbering from `hsm_switch_pos_of_mode`: the pin read uses
     * 1=BT/2=2.4G/3=USB, the enum uses its declaration order. Keeping the two
     * named separately is what stops the two vocabularies being conflated. */
    switch (hs_mode) {
        case 1: /* hs_usb */
            return HSM_SWITCH_USB;
        case 2: /* hs_bt */
            return HSM_SWITCH_BT;
        case 3: /* hs_2g4 */
            return HSM_SWITCH_2G4;
        default: /* hs_none, hs_wireless, anything else */
            return HSM_SWITCH_UNKNOWN;
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

/* True when an absolute deadline (`at`, 0 = unarmed) has been reached at `now`.
 * Signed difference so a 32-bit timer wrap is handled the way `timer_elapsed32`
 * does. */
static bool hsm_deadline_reached(uint32_t at, uint32_t now) {
    return at != 0 && (int32_t)(now - at) >= 0;
}

void hsm_link_restart(hsm_link_state_t state, uint32_t now, uint32_t reconnect_timeout, uint32_t sleep_timeout, hsm_link_timers_t *timers) {
    /* Activity starts the countdown for the state we are in, and clears the
     * other state's deadline (it does not apply). This is the pure form of the
     * old "hs_rgb_blink_set_timer(timer_read32())" activity reset, but scoped to
     * the active countdown so one event cannot arm a foreign timer. */
    timers->reconnect_at = (state == HSM_LINK_DISCONNECTED) ? now + reconnect_timeout : 0;
    timers->sleep_at     = (state == HSM_LINK_CONNECTED) ? now + sleep_timeout : 0;
}

hsm_link_action_t hsm_link_watch(hsm_link_state_t state, bool state_changed, uint32_t now, uint32_t reconnect_timeout, uint32_t sleep_timeout, hsm_link_timers_t *timers) {
    /* A deadline is meaningful only for its own state; a deadline left over from
     * a state we have left would be a latent second meaning, so clear the ones
     * that do not apply. */
    if (state != HSM_LINK_DISCONNECTED) {
        timers->reconnect_at = 0;
    }
    if (state != HSM_LINK_CONNECTED) {
        timers->sleep_at = 0;
    }

    switch (state) {
        case HSM_LINK_DISCONNECTED: {
            /* Arm on entry (or first tick after a change); then a reached
             * deadline pokes the module to reconnect and re-arms. */
            if (state_changed || timers->reconnect_at == 0) {
                timers->reconnect_at = now + reconnect_timeout;
                return HSM_LINK_ACT_NONE;
            }
            if (hsm_deadline_reached(timers->reconnect_at, now)) {
                timers->reconnect_at = now + reconnect_timeout;
                return HSM_LINK_ACT_RECONNECT;
            }
            return HSM_LINK_ACT_NONE;
        }
        case HSM_LINK_CONNECTED: {
            if (state_changed || timers->sleep_at == 0) {
                timers->sleep_at = now + sleep_timeout;
                return HSM_LINK_ACT_NONE;
            }
            if (hsm_deadline_reached(timers->sleep_at, now)) {
                timers->sleep_at = now + sleep_timeout;
                return HSM_LINK_ACT_SLEEP;
            }
            return HSM_LINK_ACT_NONE;
        }
        case HSM_LINK_NONE:
        case HSM_LINK_PAIRING:
        case HSM_LINK_REJECT:
        default:
            return HSM_LINK_ACT_NONE;
    }
}

bool hsm_should_order_sleep(uint8_t level, bool charging, uint8_t stop_threshold) {
    return !charging && level <= stop_threshold;
}
