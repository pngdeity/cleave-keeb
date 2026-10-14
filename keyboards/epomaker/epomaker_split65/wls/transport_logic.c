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

/* The one place that knows what "is a BT device index" means. Every site that
 * clamps a possibly-corrupt value into the BT range uses these; the fallback is
 * a *parameter* because it legitimately differs per site (BT1 where a BT device
 * is required, USB where the switch said nothing, 0 where "no BT profile" is
 * meant). Passing it keeps each site's choice visible instead of hiding a
 * literal behind a shared rule. */
bool hsm_devs_is_bt(uint8_t devs) {
    return devs >= HSM_DEVS_BT1 && devs <= HSM_DEVS_BT5;
}

uint8_t hsm_btdev_or(uint8_t devs, uint8_t fallback) {
    return hsm_devs_is_bt(devs) ? devs : fallback;
}

bool hsm_confinfo_needs_default(uint32_t raw, uint8_t version, uint8_t current_version) {
    /* A blank block, or one written by older firmware (a different layout), is
     * re-defaulted. Testing the version rather than guessing per field makes the
     * migration deterministic; the *decision* is here, the write stays in the
     * shell that owns the store. */
    return raw == 0 || version != current_version;
}

uint8_t hsm_boot_devs(hsm_switch_pos_t pos, uint8_t last_bt) {
    switch (pos) {
        case HSM_SWITCH_USB:
            return HSM_DEVS_USB;
        case HSM_SWITCH_BT:
            /* The switch cannot say which of BT1..5; persistence supplies it,
             * clamped to the BT range so a corrupt value cannot select USB/2.4G. */
            return hsm_btdev_or(last_bt, HSM_DEVS_BT1);
        case HSM_SWITCH_2G4:
            return HSM_DEVS_2G4;
        case HSM_SWITCH_UNKNOWN:
        default:
            /* No position reads true: keep the persisted intent (the switch may
             * be absent or mid-travel). */
            return hsm_btdev_or(last_bt, HSM_DEVS_USB);
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
    /* An unreadable position carries NO transport: mark the kind and poison the
     * payload, so even a caller that ignores `kind` cannot read out a valid
     * device index or host (0 is a valid `DEVS_USB`, so zeroing is not enough). */
    if (pos == HSM_SWITCH_UNKNOWN) {
        result.kind = HSM_SWITCH_UNREADABLE;
        result.devs = 0xFF;
        result.host = 0xFF;
        return result;
    }
    result.kind = HSM_SWITCH_SETTLED;
    result.devs = hsm_boot_devs(pos, last_bt);
    result.host = hsm_host_of_devs(result.devs);
    return result;
}

/* The switch is one INPUT among several, not the sole authority.
 *
 * The switch alone was treated as absolute, so a single wrong pin read routed
 * every keystroke to a transport that was not there: a settled read of 2.4 GHz
 * with a host cable present sends HID to the module UART and nothing reaches the
 * host -- "enumerated, lit, silent" (the magenta boot blink). The cable is a
 * physical fact and outranks a switch *preference*, so `cable_here` (a settled
 * `B9` read: this half has the host cable) wins outright; an unreadable switch
 * then degrades to the persisted intent instead of poisoning the decision.
 *
 * Precedence is "trust the most reliable signal that is present", highest first:
 *   1. cable present        -> USB, regardless of the switch
 *   2. switch settled       -> the switch's transport
 *   3. switch unreadable    -> the persisted device index
 * There is no case where a cable is present and the answer is not USB, which is
 * what makes the observed failure unrepresentable. */
hsm_switch_apply_t hsm_switch_apply_with_cable(hsm_switch_pos_t pos, uint8_t last_bt, bool cable_here) {
    if (cable_here) {
        hsm_switch_apply_t usb;
        usb.kind = HSM_SWITCH_SETTLED;
        usb.devs = HSM_DEVS_USB;
        usb.host = HSM_HOST_USB;
        return usb;
    }
    /* No cable: the switch is now the best available intent. An unreadable read
     * still carries no transport, but rather than leaving both selectors at
     * whatever they were, fall back to the persisted device index -- the last
     * known-good intent survives a reboot, so it is a better answer than the
     * stale live value the caller happened to hold. */
    if (pos == HSM_SWITCH_UNKNOWN) {
        hsm_switch_apply_t result;
        result.kind = HSM_SWITCH_SETTLED;
        result.devs = hsm_btdev_or(last_bt, HSM_DEVS_USB);
        result.host = hsm_host_of_devs(result.devs);
        return result;
    }
    return hsm_switch_apply(pos, last_bt);
}

/* The two-stage rule: hints first, then the USB peripheral's own confirmation.
 *
 * `hsm_switch_apply_with_cable` decides from *intent* signals -- the switch and
 * the cable-detect pin. Neither can tell "a host is connected" from "something
 * is supplying power": a dumb PSU raises `B9` and provides VBUS exactly as a
 * host does, so `cable_here` alone would force USB and leave the keyboard silent
 * with nothing to type to. The only input that answers the real question is the
 * device's own enumeration result, `usb_connected_state()`
 * (`USB_DRIVER.state == USB_ACTIVE`), which a PSU never reaches.
 *
 * That signal arrives *late* (only after enumeration, ~100 ms+), so it cannot be
 * the only input: before it resolves, a genuine host is indistinguishable from a
 * PSU. Hence two stages:
 *   1. USB active   -> USB. A confirmed host is a fact, and it outranks every
 *                      hint (it also fixes a switch that lies).
 *   2. otherwise    -> the hint (`hint`), which is the caller's cable/switch/
 *                      persisted decision. A PSU never reaches stage 1, so the
 *                      hint stands and the board stays wireless.
 *
 * `hint` is passed in already resolved (the caller runs
 * `hsm_switch_apply_with_cable` for it) so this core composes rather than
 * duplicates, and stays pure over plain values. */
hsm_switch_apply_t hsm_transport_decide(hsm_switch_apply_t hint, bool usb_active) {
    if (usb_active) {
        hsm_switch_apply_t usb;
        usb.kind = HSM_SWITCH_SETTLED;
        usb.devs = HSM_DEVS_USB;
        usb.host = HSM_HOST_USB;
        return usb;
    }
    return hint;
}

bool hsm_transport_changed(uint8_t decision_host, uint8_t decision_devs, uint8_t committed_host, uint8_t committed_devs) {
    return decision_host != committed_host || decision_devs != committed_devs;
}

bool hsm_host_present_on_notify(int configure_state) {
    /* Mirrors `usb_configure_state_t`: NO_INIT=0, INIT=1, CONFIGURED=2,
     * SUSPEND=3. Only CONFIGURED and SUSPEND mean the host is still there.
     * SUSPEND is a host *idle*, and QMK handles it with remote wakeup -- reading
     * it as a loss would re-decide on the switch hint and drop the data line the
     * moment the host stopped polling. */
    return configure_state == 2 /* CONFIGURED */ || configure_state == 3 /* SUSPEND */;
}

bool hsm_transport_may_act(bool ready) {
    return ready;
}

bool hsm_module_may_act(bool ready) {
    /* The module's own gate: `wireless_devs_change` reaches a ring buffer that
     * exists only after `wireless_init()`. See the header. */
    return ready;
}

bool hsm_transport_may_drive_line(bool decision_is_usb) {
    /* An unconfirmed decision may only ever turn the line ON. See the header for
     * why the down direction destroys the confirmation that outranks it. */
    return decision_is_usb;
}

bool hsm_switch_moved(hsm_switch_pos_t now_pos, hsm_switch_pos_t last_pos, bool forced) {
    if (forced) {
        return true;
    }
    return now_pos != last_pos;
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
    bool target_is_bt  = hsm_devs_is_bt(target_devs);
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
    return hsm_devs_is_bt(devs) ? devs : 0;
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
    if (hsm_devs_is_bt(current_devs)) {
        return current_devs;
    }
    return hsm_btdev_or(last_bt, HSM_DEVS_BT1);
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

hsm_link_timers_t hsm_link_restart(hsm_link_state_t state, uint32_t now, uint32_t reconnect_timeout, uint32_t sleep_timeout) {
    /* Activity starts the countdown for the state we are in, and clears the
     * other state's deadline (it does not apply). This is the pure form of the
     * old "hs_rgb_blink_set_timer(timer_read32())" activity reset, but scoped to
     * the active countdown so one event cannot arm a foreign timer. */
    hsm_link_timers_t timers;
    timers.reconnect_at = (state == HSM_LINK_DISCONNECTED) ? now + reconnect_timeout : 0;
    timers.sleep_at     = (state == HSM_LINK_CONNECTED) ? now + sleep_timeout : 0;
    return timers;
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
