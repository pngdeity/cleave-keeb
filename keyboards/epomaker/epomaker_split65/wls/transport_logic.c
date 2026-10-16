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

/* The input rule: the switch is the transport, the cable is only power.
 *
 * A cable cannot be a transport input. A wall charger, a power bank, a phone and
 * a PC all raise the cable-detect pin, and at decision time they are
 * indistinguishable -- so "a cable means USB" can only ever force USB, which is
 * precisely the requirement-2 failure: a power-only cable leaves the keyboard on
 * a transport there is nothing to type to, while the switch asked for wireless.
 *
 * The switch, by contrast, is the user's intent and the only signal that
 * expresses it. Where the switch is readable it is authoritative. Where it is
 * not, a *confirmed* host (upstream's own enumeration result -- a fact, not a
 * hint) gives USB; with no such host, the persisted intent is the best answer. */
hsm_switch_apply_t hsm_input_rule(hsm_switch_pos_t pos, uint8_t last_bt, bool host_confirmed) {
    if (pos != HSM_SWITCH_UNKNOWN) {
        return hsm_switch_apply(pos, last_bt);
    }
    hsm_switch_apply_t result;
    result.kind = HSM_SWITCH_SETTLED;
    if (host_confirmed) {
        result.devs = HSM_DEVS_USB;
    } else {
        result.devs = hsm_btdev_or(last_bt, HSM_DEVS_USB);
    }
    result.host = hsm_host_of_devs(result.devs);
    return result;
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

hsm_link_cue_t hsm_link_cue_of(hsm_link_state_t state) {
    /* One arm per carried state, with no `default:` -- so a state added to the
     * enum fails to compile (`-Wswitch`) rather than falling silently into an
     * arm that looks fine. That IS the class fix for X-30: the original defect
     * was a `default:` that swallowed REJECT into an actionless arm. */
    switch (state) {
        case HSM_LINK_CONNECTED:
            return HSM_LINK_CUE_STEADY;
        case HSM_LINK_PAIRING:
        case HSM_LINK_DISCONNECTED:
            return HSM_LINK_CUE_BLINK;
        case HSM_LINK_NONE:
        case HSM_LINK_REJECT:
            return HSM_LINK_CUE_NONE;
    }
    /* Unreachable with a complete enum (the switch above is exhaustive and the
     * trailing return silences `-Wreturn-type`). */
    return HSM_LINK_CUE_NONE;
}

hsm_link_state_t hsm_link_state_of_md(uint8_t md_state) {
    /* The module's MD_STATE_* (module.h): 0 NONE, 1 PAIRING, 2 CONNECTED,
     * 3 DISCONNECTED, 4 REJECT. Kept as numeric literals so this core does not
     * include the vendor header; the board's `wls_diag` test pins the values
     * against the enum, so a drift fails a test rather than lying silently. */
    switch (md_state) {
        case 1:
            return HSM_LINK_PAIRING;
        case 2:
            return HSM_LINK_CONNECTED;
        case 3:
            return HSM_LINK_DISCONNECTED;
        case 4:
            return HSM_LINK_REJECT;
        case 0:
        default:
            return HSM_LINK_NONE;
    }
}

hsm_chan_cue_t hsm_chan_cue_of(uint8_t devs) {
    /* One arm per DEVS_*, no `default:` -- a channel added to the module's enum
     * fails to compile (`-Wswitch`) rather than quietly inheriting another
     * channel's LED. The same class guard as `hsm_link_cue_of`, for the same
     * reason: the original defect there was a `default:` that flattened a
     * carried state, and this cue is the fix for a state flattened the same way
     * (the channel at rest). USB is its own arm rather than falling into a
     * catch-all "unknown" -- the cable *is* a mode (X-3's switch position), it
     * simply has no channel LED, and saying so explicitly is what tells the
     * shell to clear a stale wireless cue. */
    switch (devs) {
        case HSM_DEVS_BT1:
            return HSM_CHAN_CUE_BT1;
        case HSM_DEVS_BT2:
            return HSM_CHAN_CUE_BT2;
        case HSM_DEVS_BT3:
            return HSM_CHAN_CUE_BT3;
        case HSM_DEVS_BT5:
            return HSM_CHAN_CUE_BT5;
        case HSM_DEVS_2G4:
            return HSM_CHAN_CUE_2G4;
        case HSM_DEVS_USB:
            return HSM_CHAN_CUE_NONE;
    }
    /* Anything else is a value the board does not select: notably BT4, the one
     * index between BT3 and BT5 that the duplicated literals at the top of this
     * file do not name (the vendor enum has it; `hsm_devs_is_bt` covers it by
     * range). BT4/BT5's *BT5* arm is above because it is in the literal block;
     * BT4 stays un-surfaced (A9/B14) and so has no arm, landing here with a
     * corrupt index. Stated as ordinary control flow rather than a `default:`
     * so it reads as "unhandled", not as a deliberate catch-all mapping. */
    return HSM_CHAN_CUE_NONE;
}
