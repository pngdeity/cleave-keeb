/* Copyright 2026
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/* Table-driven proof of the transport-selection invariant, without hardware.
 *
 * The invariant the state machine must hold (see docs/FINDINGS.md "the
 * transport state is a tiny machine"): the physical switch is absolute
 * authority for USB / BT / 2.4 GHz, and persistence only supplies the BT1..5
 * sub-selection. The bug this pins down (TODO.md defect 4) was a *persisted*
 * wireless index restored at boot while the switch said USB — the board booted
 * onto a dead wireless link with USB still enumerated ("enumerated but no
 * keys"). The matrix below is the test vector set: every (switch position ×
 * persisted sub-selection) boot cell must land on the switch's own domain. */

#include "gtest/gtest.h"

extern "C" {
#include "keyboards/epomaker/epomaker_split65/wls/transport_logic.h"
}

/* Device-index values, mirroring keyboards/linker/wireless/module.h. */
enum {
    DEVS_USB = 0,
    DEVS_BT1 = 1,
    DEVS_BT2 = 2,
    DEVS_BT3 = 3,
    DEVS_BT4 = 4,
    DEVS_BT5 = 5,
    DEVS_2G4 = 6,
};

class TransportLogic : public ::testing::Test {};

/* I1 — "USB present but not selected" must never be reachable from the switch.
 * Whatever was persisted, the USB position selects USB. */
TEST_F(TransportLogic, usb_position_always_selects_usb) {
    const uint8_t persisted[] = {DEVS_USB, DEVS_BT1, DEVS_BT3, DEVS_BT5, DEVS_2G4};
    for (uint8_t p : persisted) {
        EXPECT_EQ(DEVS_USB, hsm_boot_devs(HSM_SWITCH_USB, p)) << "persisted devs=" << int(p) << " must not survive a USB-position boot";
    }
}

/* The 2.4 GHz position selects 2.4 GHz regardless of persistence. */
TEST_F(TransportLogic, two_g_position_always_selects_2g4) {
    const uint8_t persisted[] = {DEVS_USB, DEVS_BT1, DEVS_BT5, DEVS_2G4};
    for (uint8_t p : persisted) {
        EXPECT_EQ(DEVS_2G4, hsm_boot_devs(HSM_SWITCH_2G4, p));
    }
}

/* The BT position cannot express which of BT1..5; persistence supplies it. */
TEST_F(TransportLogic, bt_position_restores_persisted_subindex) {
    EXPECT_EQ(DEVS_BT1, hsm_boot_devs(HSM_SWITCH_BT, DEVS_BT1));
    EXPECT_EQ(DEVS_BT3, hsm_boot_devs(HSM_SWITCH_BT, DEVS_BT3));
    EXPECT_EQ(DEVS_BT5, hsm_boot_devs(HSM_SWITCH_BT, DEVS_BT5));
}

/* A corrupt persisted value (USB / 2.4G / out of range) must not leak through a
 * BT-position boot; it clamps into the BT range. */
TEST_F(TransportLogic, bt_position_clamps_corrupt_persistence) {
    EXPECT_GE(hsm_boot_devs(HSM_SWITCH_BT, DEVS_USB), DEVS_BT1);
    EXPECT_LE(hsm_boot_devs(HSM_SWITCH_BT, DEVS_USB), DEVS_BT5);
    EXPECT_GE(hsm_boot_devs(HSM_SWITCH_BT, DEVS_2G4), DEVS_BT1);
    EXPECT_LE(hsm_boot_devs(HSM_SWITCH_BT, DEVS_2G4), DEVS_BT5);
    EXPECT_GE(hsm_boot_devs(HSM_SWITCH_BT, 0xFF), DEVS_BT1);
    EXPECT_LE(hsm_boot_devs(HSM_SWITCH_BT, 0xFF), DEVS_BT5);
}

/* No readable position: keep persistence, but never invent a wireless index for
 * a corrupt value — fall back to USB (the safe, enumerated default). */
TEST_F(TransportLogic, unknown_position_is_forgiving_but_safe) {
    EXPECT_EQ(DEVS_BT2, hsm_boot_devs(HSM_SWITCH_UNKNOWN, DEVS_BT2));
    EXPECT_EQ(DEVS_USB, hsm_boot_devs(HSM_SWITCH_UNKNOWN, DEVS_2G4));
    EXPECT_EQ(DEVS_USB, hsm_boot_devs(HSM_SWITCH_UNKNOWN, 0xFF));
}

/* Keycode gating: a wireless key may not cross the switch. */
TEST_F(TransportLogic, keycodes_respect_switch_domain) {
    /* BT key in BT position: allowed. */
    EXPECT_TRUE(hsm_keycode_allowed(HSM_SWITCH_BT, DEVS_BT2));
    /* BT key while the switch says USB or 2.4G: refused. */
    EXPECT_FALSE(hsm_keycode_allowed(HSM_SWITCH_USB, DEVS_BT2));
    EXPECT_FALSE(hsm_keycode_allowed(HSM_SWITCH_2G4, DEVS_BT2));
    /* 2.4G key only in the 2.4G position. */
    EXPECT_TRUE(hsm_keycode_allowed(HSM_SWITCH_2G4, DEVS_2G4));
    EXPECT_FALSE(hsm_keycode_allowed(HSM_SWITCH_BT, DEVS_2G4));
    /* Unknown position stays forgiving (a broken switch must not hard-lock). */
    EXPECT_TRUE(hsm_keycode_allowed(HSM_SWITCH_UNKNOWN, DEVS_BT1));
    EXPECT_TRUE(hsm_keycode_allowed(HSM_SWITCH_UNKNOWN, DEVS_2G4));
}

/* The long-press re-pair target is decided by the keycode alone, as a pure
 * function. This is what lets the deferred timer capture a *value* at arm time
 * instead of a pointer into per-keypress state (audit fix 1). */
TEST_F(TransportLogic, long_press_target_is_a_keycode_table) {
    /* Wireless keycodes (KC_BT1..KC_2G4 = QK_KB_0..QK_KB_3) map to their device. */
    EXPECT_EQ(DEVS_BT1, hsm_long_press_devs(0x7E00));
    EXPECT_EQ(DEVS_BT2, hsm_long_press_devs(0x7E01));
    EXPECT_EQ(DEVS_BT3, hsm_long_press_devs(0x7E02));
    EXPECT_EQ(DEVS_2G4, hsm_long_press_devs(0x7E03));

    /* Anything else has no long-press action. */
    EXPECT_EQ(-1, hsm_long_press_devs(0x0000));
    EXPECT_EQ(-1, hsm_long_press_devs(0x0004)); /* KC_A */
    EXPECT_EQ(-1, hsm_long_press_devs(0x7E10)); /* a different keyboard keycode */
}

/* The BT profile is *derived* from the device index, never stored: BT1..5 map to
 * their own number (the readback), and any non-BT index maps to 0 (no profile).
 * This is what removes the second RAM variable that could desync from the device
 * index and then lie in the battery readback (audit fix 2). */
TEST_F(TransportLogic, bt_profile_is_a_pure_function_of_the_device_index) {
    /* On a BT profile the profile number is the device index (1..5). */
    EXPECT_EQ(DEVS_BT1, hsm_profile_of_devs(DEVS_BT1));
    EXPECT_EQ(DEVS_BT2, hsm_profile_of_devs(DEVS_BT2));
    EXPECT_EQ(DEVS_BT5, hsm_profile_of_devs(DEVS_BT5));

    /* Off a BT profile there is no profile. */
    EXPECT_EQ(0, hsm_profile_of_devs(DEVS_USB));
    EXPECT_EQ(0, hsm_profile_of_devs(DEVS_2G4));
    EXPECT_EQ(0, hsm_profile_of_devs(0xFF));

    /* Round trip: the profile read back from a BT index selects that same index,
     * so the reported profile and the transport authority cannot disagree. */
    for (uint8_t d = DEVS_BT1; d <= DEVS_BT5; d++) {
        EXPECT_EQ(d, hsm_profile_of_devs(d));
    }
}

/* The mode scan seeds from the *live* device index, never a RAM mirror of the
 * persisted one (audit fix 3). `hsm_mode_seed` is the identity by contract, and
 * `hsm_seed_btdev` prefers the live index when it is on a BT profile, falling
 * back to the clamped persisted BT index otherwise. */
TEST_F(TransportLogic, mode_seed_is_the_live_device_index) {
    for (uint8_t d = DEVS_USB; d <= DEVS_2G4; d++) {
        EXPECT_EQ(d, hsm_mode_seed(d));
    }
}

TEST_F(TransportLogic, bt_seed_prefers_live_index_and_clamps_fallback) {
    /* On a BT profile the live index IS the sub-selection. */
    EXPECT_EQ(DEVS_BT4, hsm_seed_btdev(DEVS_BT4, DEVS_BT2));

    /* Off a BT profile the persisted last-BT index is the fallback. */
    EXPECT_EQ(DEVS_BT3, hsm_seed_btdev(DEVS_USB, DEVS_BT3));
    EXPECT_EQ(DEVS_BT5, hsm_seed_btdev(DEVS_2G4, DEVS_BT5));

    /* A corrupt fallback clamps into BT1..5 rather than leaking USB/2.4G. */
    EXPECT_EQ(DEVS_BT1, hsm_seed_btdev(DEVS_USB, DEVS_USB));
    EXPECT_EQ(DEVS_BT1, hsm_seed_btdev(DEVS_USB, DEVS_2G4));
    EXPECT_EQ(DEVS_BT1, hsm_seed_btdev(DEVS_2G4, 0xFF));
}

/* The link watch has two independent countdowns: a DISCONNECTED link re-pokes
 * the module after `reconnect_timeout`, a CONNECTED link orders sleep after
 * `sleep_timeout`. They must never share a deadline: a value that means "poke"
 * in one state cannot mean "sleep" in another. Each state's deadline is cleared
 * when that state is not current, so a stale deadline cannot fire late. */
TEST_F(TransportLogic, link_watch_uses_independent_deadlines) {
    hsm_link_timers_t t;
    const uint32_t    RECONNECT = 30000, SLEEP = 60000;

    /* DISCONNECTED: arms on entry, then fires reconnect exactly at the timeout. */
    t = {0, 0};
    EXPECT_EQ(HSM_LINK_ACT_NONE, hsm_link_watch(HSM_LINK_DISCONNECTED, true, 1000, RECONNECT, SLEEP, &t));
    EXPECT_EQ(31000u, t.reconnect_at);
    EXPECT_EQ(0u, t.sleep_at); /* the sleep deadline is not this state's */
    EXPECT_EQ(HSM_LINK_ACT_NONE, hsm_link_watch(HSM_LINK_DISCONNECTED, false, 30000, RECONNECT, SLEEP, &t));
    EXPECT_EQ(HSM_LINK_ACT_RECONNECT, hsm_link_watch(HSM_LINK_DISCONNECTED, false, 31000, RECONNECT, SLEEP, &t));
    /* re-armed after firing */
    EXPECT_EQ(61000u, t.reconnect_at);

    /* CONNECTED: independent deadline, same shape. */
    t = {0, 0};
    EXPECT_EQ(HSM_LINK_ACT_NONE, hsm_link_watch(HSM_LINK_CONNECTED, true, 1000, RECONNECT, SLEEP, &t));
    EXPECT_EQ(61000u, t.sleep_at);
    EXPECT_EQ(0u, t.reconnect_at);
    EXPECT_EQ(HSM_LINK_ACT_SLEEP, hsm_link_watch(HSM_LINK_CONNECTED, false, 61000, RECONNECT, SLEEP, &t));
}

/* A state change restarts the active countdown, and leaving a state clears its
 * deadline so it cannot fire late under the next state's meaning. */
TEST_F(TransportLogic, link_watch_clears_foreign_deadlines) {
    hsm_link_timers_t t         = {0, 0};
    const uint32_t    RECONNECT = 30000, SLEEP = 60000;

    /* Arm a reconnect deadline... */
    hsm_link_watch(HSM_LINK_DISCONNECTED, true, 1000, RECONNECT, SLEEP, &t);
    EXPECT_NE(0u, t.reconnect_at);

    /* ...then leave DISCONNECTED: the reconnect deadline must be cleared. */
    EXPECT_EQ(HSM_LINK_ACT_NONE, hsm_link_watch(HSM_LINK_CONNECTED, true, 5000, RECONNECT, SLEEP, &t));
    EXPECT_EQ(0u, t.reconnect_at);

    /* States with no countdown clear both. */
    t = {123, 456};
    EXPECT_EQ(HSM_LINK_ACT_NONE, hsm_link_watch(HSM_LINK_NONE, false, 9, RECONNECT, SLEEP, &t));
    EXPECT_EQ(0u, t.reconnect_at);
    EXPECT_EQ(0u, t.sleep_at);
}

/* Activity restarts only the active countdown, and returns the deadlines to
 * store back (rather than writing through a caller pointer) so a caller cannot
 * forget to persist them. */
TEST_F(TransportLogic, link_restart_returns_active_deadline_only) {
    const uint32_t RECONNECT = 30000, SLEEP = 60000;

    /* DISCONNECTED: only the reconnect deadline is armed. */
    hsm_link_timers_t d = hsm_link_restart(HSM_LINK_DISCONNECTED, 1000, RECONNECT, SLEEP);
    EXPECT_EQ(31000u, d.reconnect_at);
    EXPECT_EQ(0u, d.sleep_at);

    /* CONNECTED: only the sleep deadline is armed. */
    hsm_link_timers_t c = hsm_link_restart(HSM_LINK_CONNECTED, 1000, RECONNECT, SLEEP);
    EXPECT_EQ(0u, c.reconnect_at);
    EXPECT_EQ(61000u, c.sleep_at);

    /* Neither countdown applies to a state that is not one of the two. */
    hsm_link_timers_t n = hsm_link_restart(HSM_LINK_NONE, 1000, RECONNECT, SLEEP);
    EXPECT_EQ(0u, n.reconnect_at);
    EXPECT_EQ(0u, n.sleep_at);

    hsm_link_timers_t r = hsm_link_restart(HSM_LINK_PAIRING, 1000, RECONNECT, SLEEP);
    EXPECT_EQ(0u, r.reconnect_at);
    EXPECT_EQ(0u, r.sleep_at);
}

/* The low-battery sleep order is a pure function of the battery facts: order a
 * stop only at/below the stop threshold while not charging. This pins the
 * decision the board's battery tick makes before it calls `wls_order_sleep`. */
TEST_F(TransportLogic, sleep_order_is_a_pure_function_of_battery_and_charge) {
    const uint8_t STOP = 5;

    /* At or below the threshold, not charging: order. */
    EXPECT_TRUE(hsm_should_order_sleep(0, false, STOP));
    EXPECT_TRUE(hsm_should_order_sleep(STOP, false, STOP));

    /* Above the threshold: do not order. */
    EXPECT_FALSE(hsm_should_order_sleep(STOP + 1, false, STOP));
    EXPECT_FALSE(hsm_should_order_sleep(100, false, STOP));

    /* Charging always overrides: never order a stop while charging. */
    EXPECT_FALSE(hsm_should_order_sleep(0, true, STOP));
    EXPECT_FALSE(hsm_should_order_sleep(STOP, true, STOP));
}

/* The switch selects a *pair* of selectors that must agree: the vendor device
 * index and upstream's connection host. The defect was a switch edge that set
 * only the index, so a runtime BT->USB flip left the host on BLUETOOTH and
 * nothing typed until a power cycle. This pins the pair for every position. */
TEST_F(TransportLogic, switch_position_yields_a_matched_devs_and_host_pair) {
    /* USB: device index USB, host USB. */
    hsm_switch_apply_t usb = hsm_switch_apply(HSM_SWITCH_USB, DEVS_BT3);
    EXPECT_EQ(HSM_SWITCH_SETTLED, usb.kind);
    EXPECT_EQ(DEVS_USB, usb.devs);
    EXPECT_EQ(HSM_HOST_USB, usb.host);

    /* 2.4 GHz: device index 2.4 GHz, host 2.4 GHz, whatever was persisted. */
    hsm_switch_apply_t g24 = hsm_switch_apply(HSM_SWITCH_2G4, DEVS_BT1);
    EXPECT_EQ(HSM_SWITCH_SETTLED, g24.kind);
    EXPECT_EQ(DEVS_2G4, g24.devs);
    EXPECT_EQ(HSM_HOST_2P4GHZ, g24.host);

    /* BT: the persisted sub-index picks which of BT1..5, host BLUETOOTH. */
    hsm_switch_apply_t bt = hsm_switch_apply(HSM_SWITCH_BT, DEVS_BT2);
    EXPECT_EQ(HSM_SWITCH_SETTLED, bt.kind);
    EXPECT_EQ(DEVS_BT2, bt.devs);
    EXPECT_EQ(HSM_HOST_BLUETOOTH, bt.host);
}

/* The switch is one INPUT among several, not the sole authority.
 *
 * A host cable is a physical fact and outranks a switch preference: for EVERY
 * switch position and every persisted sub-index, a settled cable read must land
 * on USB. This is the invariant that makes the observed failure -- a settled
 * wireless read with a cable present, sending HID to the module while the host
 * hears nothing ("enumerated, lit, silent") -- unrepresentable rather than
 * merely unlikely. */
TEST_F(TransportLogic, cable_outranks_the_switch_for_every_input) {
    const hsm_switch_pos_t positions[]      = {HSM_SWITCH_UNKNOWN, HSM_SWITCH_USB, HSM_SWITCH_BT, HSM_SWITCH_2G4};
    const uint8_t          last_bt_values[] = {DEVS_USB, DEVS_BT1, DEVS_BT3, DEVS_BT5, DEVS_2G4};
    for (hsm_switch_pos_t pos : positions) {
        for (uint8_t last_bt : last_bt_values) {
            hsm_switch_apply_t r = hsm_switch_apply_with_cable(pos, last_bt, true);
            EXPECT_EQ(HSM_SWITCH_SETTLED, r.kind) << "pos " << int(pos);
            EXPECT_EQ(DEVS_USB, r.devs) << "pos " << int(pos) << " last_bt " << int(last_bt);
            EXPECT_EQ(HSM_HOST_USB, r.host) << "pos " << int(pos) << " last_bt " << int(last_bt);
        }
    }
}

/* With no cable the switch is the best available intent, so it is honoured
 * exactly as before -- the demotion must not cost the wireless affordance. */
TEST_F(TransportLogic, without_a_cable_the_switch_still_decides) {
    hsm_switch_apply_t bt = hsm_switch_apply_with_cable(HSM_SWITCH_BT, DEVS_BT4, false);
    EXPECT_EQ(HSM_SWITCH_SETTLED, bt.kind);
    EXPECT_EQ(DEVS_BT4, bt.devs);
    EXPECT_EQ(HSM_HOST_BLUETOOTH, bt.host);

    hsm_switch_apply_t g24 = hsm_switch_apply_with_cable(HSM_SWITCH_2G4, DEVS_BT1, false);
    EXPECT_EQ(HSM_SWITCH_SETTLED, g24.kind);
    EXPECT_EQ(DEVS_2G4, g24.devs);
    EXPECT_EQ(HSM_HOST_2P4GHZ, g24.host);
}

/* An unreadable switch with no cable degrades to the persisted intent instead of
 * applying nothing. The old rule returned UNREADABLE and left both selectors
 * untouched, which is what let a boot on the USB position self-detach (UNKNOWN
 * -> persisted BT -> "Bluetooth" -> line down). Now the fallback is explicit and
 * always a real transport, so "attached to a host but routed elsewhere" cannot
 * arise from an unreadable pin either. */
TEST_F(TransportLogic, unreadable_switch_without_a_cable_falls_back_to_the_persisted_index) {
    for (uint8_t bt = DEVS_BT1; bt <= DEVS_BT5; bt++) {
        hsm_switch_apply_t r = hsm_switch_apply_with_cable(HSM_SWITCH_UNKNOWN, bt, false);
        EXPECT_EQ(HSM_SWITCH_SETTLED, r.kind) << "bt " << int(bt);
        EXPECT_EQ(bt, r.devs) << "bt " << int(bt);
        EXPECT_EQ(HSM_HOST_BLUETOOTH, r.host) << "bt " << int(bt);
    }

    /* A persisted index that is not a BT profile clamps to USB rather than
     * fabricating a profile. */
    hsm_switch_apply_t clamp = hsm_switch_apply_with_cable(HSM_SWITCH_UNKNOWN, DEVS_2G4, false);
    EXPECT_EQ(HSM_SWITCH_SETTLED, clamp.kind);
    EXPECT_EQ(DEVS_USB, clamp.devs);
    EXPECT_EQ(HSM_HOST_USB, clamp.host);
}

/* A confirmed host outranks every hint. `usb_active` is the peripheral's own
 * enumeration result (`usb_connected_state()`), the one input that distinguishes
 * a real host from a dumb PSU -- so when it is true the transport is USB no
 * matter what the switch or the cable pin said. This is what keeps the magenta
 * failure fixed: a lying switch cannot override a host that has actually
 * enumerated. */
TEST_F(TransportLogic, a_confirmed_host_outranks_every_hint) {
    const hsm_switch_pos_t positions[]      = {HSM_SWITCH_UNKNOWN, HSM_SWITCH_USB, HSM_SWITCH_BT, HSM_SWITCH_2G4};
    const uint8_t          last_bt_values[] = {DEVS_USB, DEVS_BT1, DEVS_BT3, DEVS_BT5, DEVS_2G4};
    for (hsm_switch_pos_t pos : positions) {
        for (uint8_t last_bt : last_bt_values) {
            for (bool cable : {false, true}) {
                hsm_switch_apply_t hint = hsm_switch_apply_with_cable(pos, last_bt, cable);
                hsm_switch_apply_t r    = hsm_transport_decide(hint, true);
                EXPECT_EQ(HSM_SWITCH_SETTLED, r.kind) << "pos " << int(pos);
                EXPECT_EQ(DEVS_USB, r.devs) << "pos " << int(pos) << " last_bt " << int(last_bt);
                EXPECT_EQ(HSM_HOST_USB, r.host) << "pos " << int(pos) << " last_bt " << int(last_bt);
            }
        }
    }
}

/* Without a confirmed host the hint stands unchanged -- including when the hint
 * is USB. This is the PSU case: a dumb supply raises `B9` and provides VBUS just
 * as a host does, so the hint may say USB, but with no enumeration the transport
 * must fall back to the switch's wireless choice rather than go silent. */
TEST_F(TransportLogic, without_a_confirmed_host_the_hint_stands) {
    /* The PSU: cable present (hint votes USB) but nothing enumerated. The
     * decision must not manufacture a host that is not there -- it returns the
     * hint as-is, and the caller's hint for "switch on BT, no host" is BT. */
    hsm_switch_apply_t psu_hint = hsm_switch_apply_with_cable(HSM_SWITCH_BT, DEVS_BT2, false);
    hsm_switch_apply_t psu      = hsm_transport_decide(psu_hint, false);
    EXPECT_EQ(HSM_SWITCH_SETTLED, psu.kind);
    EXPECT_EQ(DEVS_BT2, psu.devs);
    EXPECT_EQ(HSM_HOST_BLUETOOTH, psu.host);

    /* A cable hint that has NOT enumerated is returned unchanged: the decision
     * adds no host, so the silent-USB outcome depends on the caller's hint, not
     * on this stage. */
    hsm_switch_apply_t cable_hint = hsm_switch_apply_with_cable(HSM_SWITCH_2G4, DEVS_BT1, true);
    hsm_switch_apply_t cable      = hsm_transport_decide(cable_hint, false);
    EXPECT_EQ(cable_hint.host, cable.host);
    EXPECT_EQ(cable_hint.devs, cable.devs);
}

/* The regression guard for the boot self-detach (docs/FINDINGS.md, "the boot
 * switch read is un-settled"). An unreadable switch position must yield
 * HSM_SWITCH_UNREADABLE and carry NO transport -- for EVERY persisted sub-index
 * -- because the earlier design mapped unknown -> the persisted BT index ->
 * "Bluetooth", which tore the USB data line down at boot on the USB position.
 * This is the case that makes that mapping impossible: a fallback with no
 * distinguishable "unknown" is exactly what must never come back. */
TEST_F(TransportLogic, unreadable_switch_position_carries_no_transport) {
    /* Every persisted sub-index, BT1..5 and the USB/2.4GHz ones included. */
    const uint8_t last_bt_values[] = {DEVS_USB, DEVS_BT1, DEVS_BT2, DEVS_BT3, DEVS_BT4, DEVS_BT5, DEVS_2G4};
    for (uint8_t last_bt : last_bt_values) {
        hsm_switch_apply_t r = hsm_switch_apply(HSM_SWITCH_UNKNOWN, last_bt);
        EXPECT_EQ(HSM_SWITCH_UNREADABLE, r.kind) << "last_bt " << int(last_bt);
        /* No decision may be smuggled out in the payload: neither selector may
         * hold a value a caller could apply. 0 is a valid `DEVS_USB`, so the
         * payload must be poisoned (0xFF), not merely zeroed. */
        EXPECT_EQ(0xFF, r.devs) << "last_bt " << int(last_bt);
        EXPECT_EQ(0xFF, r.host) << "last_bt " << int(last_bt);
    }
}

/* Every readable position is settled and yields a decision; only the unknown
 * position is unreadable. This is the other half of the guard: the fix must not
 * have made a real position unusable. */
TEST_F(TransportLogic, every_readable_position_is_settled) {
    EXPECT_EQ(HSM_SWITCH_SETTLED, hsm_switch_apply(HSM_SWITCH_USB, DEVS_BT1).kind);
    EXPECT_EQ(HSM_SWITCH_SETTLED, hsm_switch_apply(HSM_SWITCH_BT, DEVS_BT1).kind);
    EXPECT_EQ(HSM_SWITCH_SETTLED, hsm_switch_apply(HSM_SWITCH_2G4, DEVS_BT1).kind);
}

/* The host mapper is total and matches the firmware's `connection_host_t`: USB
 * -> USB, 2.4 GHz -> 2P4GHZ, every BT index (and anything unrecognised) -> the
 * wireless default BLUETOOTH. Non-BT positions never depend on the sub-index. */
TEST_F(TransportLogic, host_maps_from_the_device_index) {
    EXPECT_EQ(HSM_HOST_USB, hsm_host_of_devs(DEVS_USB));
    EXPECT_EQ(HSM_HOST_2P4GHZ, hsm_host_of_devs(DEVS_2G4));
    for (uint8_t bt = DEVS_BT1; bt <= DEVS_BT5; bt++) {
        EXPECT_EQ(HSM_HOST_BLUETOOTH, hsm_host_of_devs(bt)) << "BT index " << int(bt);
    }
    EXPECT_EQ(HSM_HOST_BLUETOOTH, hsm_host_of_devs(0xFF));
}

/* The board's numeric pin-mode maps onto the one canonical position type; any
 * value outside the three known positions is unknown. */
TEST_F(TransportLogic, numeric_mode_maps_to_the_canonical_position) {
    EXPECT_EQ(HSM_SWITCH_USB, hsm_switch_pos_of_mode(3));
    EXPECT_EQ(HSM_SWITCH_BT, hsm_switch_pos_of_mode(1));
    EXPECT_EQ(HSM_SWITCH_2G4, hsm_switch_pos_of_mode(2));
    EXPECT_EQ(HSM_SWITCH_UNKNOWN, hsm_switch_pos_of_mode(0));
    EXPECT_EQ(HSM_SWITCH_UNKNOWN, hsm_switch_pos_of_mode(7));
}

/* The switch edge is a position change, not a disagreement with the resolved
 * transport. Reading it as the latter is the livelock: under the transport rule
 * the pins are *supposed* to disagree with the transport (a confirmed host
 * outranks a lying switch), so that predicate never cleared and re-drove the
 * actuator every 50 ms scan -- cutting the data line and re-enumerating. */
TEST_F(TransportLogic, switch_edge_is_a_position_change_not_a_disagreement) {
    EXPECT_FALSE(hsm_switch_moved(HSM_SWITCH_2G4, HSM_SWITCH_2G4, false));
    EXPECT_TRUE(hsm_switch_moved(HSM_SWITCH_USB, HSM_SWITCH_2G4, false));
    EXPECT_TRUE(hsm_switch_moved(HSM_SWITCH_2G4, HSM_SWITCH_USB, false));
    EXPECT_TRUE(hsm_switch_moved(HSM_SWITCH_UNKNOWN, HSM_SWITCH_BT, false));
    EXPECT_FALSE(hsm_switch_moved(HSM_SWITCH_UNKNOWN, HSM_SWITCH_UNKNOWN, false));
}

/* The detector's own "re-read now" request counts as an edge regardless of
 * history, so a forced scan still runs the switch's side effects. */
TEST_F(TransportLogic, a_forced_read_is_always_an_edge) {
    EXPECT_TRUE(hsm_switch_moved(HSM_SWITCH_USB, HSM_SWITCH_USB, true));
    EXPECT_TRUE(hsm_switch_moved(HSM_SWITCH_UNKNOWN, HSM_SWITCH_UNKNOWN, true));
}

/* The detector's *output* mode (wls.h `enum modeio_mode`: hs_none=0, hs_usb=1,
 * hs_bt=2, hs_2g4=3, hs_wireless=4) maps onto the canonical position. This is a
 * different numbering from the raw pin read above, so it has its own mapper;
 * `hs_wireless` is an input mode the detector never emits and maps to unknown. */
TEST_F(TransportLogic, detector_output_mode_maps_to_the_canonical_position) {
    enum { HS_NONE = 0, HS_USB = 1, HS_BT = 2, HS_2G4 = 3, HS_WIRELESS = 4 };
    EXPECT_EQ(HSM_SWITCH_USB, hsm_switch_pos_of_hs_mode(HS_USB));
    EXPECT_EQ(HSM_SWITCH_BT, hsm_switch_pos_of_hs_mode(HS_BT));
    EXPECT_EQ(HSM_SWITCH_2G4, hsm_switch_pos_of_hs_mode(HS_2G4));
    EXPECT_EQ(HSM_SWITCH_UNKNOWN, hsm_switch_pos_of_hs_mode(HS_NONE));
    EXPECT_EQ(HSM_SWITCH_UNKNOWN, hsm_switch_pos_of_hs_mode(HS_WIRELESS));
    EXPECT_EQ(HSM_SWITCH_UNKNOWN, hsm_switch_pos_of_hs_mode(0xFF));
}

/* The switch gate, as the board composes it: position (from the detector) plus
 * target. A BT key acts only in the BT position, a 2.4 GHz key only in the
 * 2.4 GHz position, and an unreadable switch is forgiving — the rule the six
 * inline copies in the board glue all expressed. */
TEST_F(TransportLogic, keycode_gate_permits_only_the_matching_position) {
    EXPECT_FALSE(hsm_keycode_allowed(HSM_SWITCH_USB, DEVS_BT1));
    EXPECT_FALSE(hsm_keycode_allowed(HSM_SWITCH_USB, DEVS_2G4));
    EXPECT_TRUE(hsm_keycode_allowed(HSM_SWITCH_BT, DEVS_BT1));
    EXPECT_TRUE(hsm_keycode_allowed(HSM_SWITCH_BT, DEVS_BT5));
    EXPECT_FALSE(hsm_keycode_allowed(HSM_SWITCH_BT, DEVS_2G4));
    EXPECT_TRUE(hsm_keycode_allowed(HSM_SWITCH_2G4, DEVS_2G4));
    EXPECT_FALSE(hsm_keycode_allowed(HSM_SWITCH_2G4, DEVS_BT1));
    EXPECT_TRUE(hsm_keycode_allowed(HSM_SWITCH_UNKNOWN, DEVS_BT1));
    EXPECT_TRUE(hsm_keycode_allowed(HSM_SWITCH_UNKNOWN, DEVS_2G4));
}

/* The transport-change rule: the owner applies the transport, and drives the USB
 * line, only when this says the decision differs from what is committed. It is
 * the reason a self-re-arming trigger is impossible: re-running the decision on
 * an unchanged input produces no change, so nothing acts, so nothing perturbs
 * the input. Both livelocks this session were failures of exactly that property
 * (see docs/ADR-transport-authority.md). */
TEST_F(TransportLogic, a_repeated_decision_is_not_a_change) {
    EXPECT_FALSE(hsm_transport_changed(HSM_HOST_USB, DEVS_USB, HSM_HOST_USB, DEVS_USB));
    EXPECT_FALSE(hsm_transport_changed(HSM_HOST_2P4GHZ, DEVS_2G4, HSM_HOST_2P4GHZ, DEVS_2G4));
}

/* Either selector differing is a change -- the pair is the transport, and a host
 * written without its matching device index routes HID to the wrong driver. */
TEST_F(TransportLogic, either_selector_differing_is_a_change) {
    EXPECT_TRUE(hsm_transport_changed(HSM_HOST_2P4GHZ, DEVS_USB, HSM_HOST_USB, DEVS_USB));
    EXPECT_TRUE(hsm_transport_changed(HSM_HOST_USB, DEVS_2G4, HSM_HOST_USB, DEVS_USB));
    EXPECT_TRUE(hsm_transport_changed(HSM_HOST_USB, DEVS_BT1, HSM_HOST_USB, DEVS_2G4));
}

/* The undecided start: the owner begins at WLS_TRANSPORT_UNKNOWN, so the first
 * commit is always a change and therefore always writes both selectors. Without
 * this the owner could silently agree with a `connection_init()` host it never
 * chose (EEPROM may hold CONNECTION_HOST_AUTO, which is not a transport). */
TEST_F(TransportLogic, the_first_decision_over_an_undecided_owner_is_a_change) {
    EXPECT_TRUE(hsm_transport_changed(HSM_HOST_USB, DEVS_USB, WLS_TRANSPORT_UNKNOWN, DEVS_USB));
    EXPECT_TRUE(hsm_transport_changed(HSM_HOST_BLUETOOTH, DEVS_BT1, WLS_TRANSPORT_UNKNOWN, DEVS_USB));
    EXPECT_TRUE(hsm_transport_changed(HSM_HOST_2P4GHZ, DEVS_2G4, WLS_TRANSPORT_UNKNOWN, DEVS_USB));
}

/* A host that is merely idle is still a host.
 *
 * Upstream's notification carries `usb_configure_state_t` (NO_INIT=0, INIT=1,
 * CONFIGURED=2, SUSPEND=3). Reading SUSPEND as a host loss re-decides the
 * transport on the switch hint and drops the data line the moment the host
 * stops polling -- a self-detach at idle, and on a board whose switch reads
 * wireless while cabled it is the very failure the transport owner removes. Only
 * a reset / true unconfigure means the host is gone. */
TEST_F(TransportLogic, a_suspended_host_is_still_present) {
    enum { NO_INIT = 0, INIT = 1, CONFIGURED = 2, SUSPEND = 3 };
    EXPECT_TRUE(hsm_host_present_on_notify(CONFIGURED));
    EXPECT_TRUE(hsm_host_present_on_notify(SUSPEND));
    EXPECT_FALSE(hsm_host_present_on_notify(NO_INIT));
    EXPECT_FALSE(hsm_host_present_on_notify(INIT));
    /* Any value upstream may add later must not be mistaken for a live host. */
    EXPECT_FALSE(hsm_host_present_on_notify(99));
}

/* An owner that has not been told the module is up performs no effect.
 *
 * Committing the transport reaches the vendor ring buffer
 * (`wireless_devs_change` -> `md_send_devctrl` -> `smsg_push`), which is only
 * initialized by `wireless_init()`. The owner can be driven before that:
 * `protocol_setup()` fires the USB-lifecycle hook, which feeds the owner, and
 * the ring is not up yet -- `smsg_push` would `memcpy` to a NULL buffer and
 * hard-fault the MCU. The 2026-10-13 build did exactly that and reset before the
 * main loop, so the board pulled up D+ and went mute. */
TEST_F(TransportLogic, an_unready_owner_may_not_act) {
    EXPECT_FALSE(hsm_transport_may_act(false));
    EXPECT_TRUE(hsm_transport_may_act(true));
}

/* The module's own gate, applied wherever `wireless_devs_change` is called. The
 * transport owner's readiness and the module's readiness are the same event but
 * different components: gating one actuator's call sites (the owner) is not
 * gating the class, which is why every board call site goes through the module's
 * own owner (`wls/wls_module.c`) and this predicate pins the rule. */
TEST_F(TransportLogic, an_unready_module_may_not_act) {
    EXPECT_FALSE(hsm_module_may_act(false));
    EXPECT_TRUE(hsm_module_may_act(true));
}

/* An unconfirmed (boot-time) decision may only ever turn the USB line ON. If it
 * could turn it OFF, it would destroy the enumeration that would confirm it: the
 * line falls, USB never enumerates, and the host-lifecycle event that outranks a
 * lying switch never fires. The 2026-10-13 build did exactly that -- the switch
 * read 2.4 GHz (the C14/C15 bridge), boot committed to it, and the board cut the
 * line it was about to enumerate on. The demotion belongs to confirmed events
 * (the switch edge, the host event hook), not to boot. */
TEST_F(TransportLogic, an_unconfirmed_decision_may_only_promote_the_line) {
    EXPECT_TRUE(hsm_transport_may_drive_line(true));   /* USB decision -> turn on  */
    EXPECT_FALSE(hsm_transport_may_drive_line(false)); /* wireless -> do NOT tear down */
}

/* --- the BT-index predicate, written once (F6) ------------------------------
 *
 * The range test "is this a BT device index" used to be spelled out at four call
 * sites, each with its own fallback literal. The predicate now lives in one
 * place; the fallback stays a per-site parameter, because the sites genuinely
 * differ (BT1 where a BT device is required, USB where the switch said nothing,
 * 0 where "no BT profile" is meant). These tests pin both halves. */

TEST_F(TransportLogic, bt_index_predicate_has_closed_bounds) {
    EXPECT_FALSE(hsm_devs_is_bt(DEVS_USB)); /* 0 -- below the range */
    EXPECT_TRUE(hsm_devs_is_bt(DEVS_BT1));  /* 1 -- the low bound */
    EXPECT_TRUE(hsm_devs_is_bt(DEVS_BT5));  /* 5 -- the high bound */
    EXPECT_FALSE(hsm_devs_is_bt(DEVS_2G4)); /* 6 -- above the range */
    EXPECT_FALSE(hsm_devs_is_bt(0xFF));     /* corrupt -- outside it */
}

TEST_F(TransportLogic, bt_or_fallback_passes_through_inside_the_range) {
    for (uint8_t d = DEVS_BT1; d <= DEVS_BT5; d++) {
        EXPECT_EQ(d, hsm_btdev_or(d, DEVS_USB));
    }
}

TEST_F(TransportLogic, bt_or_fallback_uses_the_callers_fallback) {
    EXPECT_EQ(DEVS_BT1, hsm_btdev_or(DEVS_USB, DEVS_BT1)); /* "a BT device is required" */
    EXPECT_EQ(DEVS_USB, hsm_btdev_or(DEVS_2G4, DEVS_USB)); /* "the switch said nothing"  */
    EXPECT_EQ(0, hsm_btdev_or(DEVS_2G4, 0));               /* "no BT profile"            */
    EXPECT_EQ(DEVS_USB, hsm_btdev_or(0xFF, DEVS_USB));     /* corrupt -> same rule       */
}

/* The four former sites, re-asserted through the core rule so a fallback cannot
 * drift unnoticed. */
TEST_F(TransportLogic, the_four_call_sites_keep_their_fallbacks) {
    EXPECT_EQ(DEVS_BT1, hsm_boot_devs(HSM_SWITCH_BT, DEVS_USB));      /* site 1: BT1  */
    EXPECT_EQ(DEVS_USB, hsm_boot_devs(HSM_SWITCH_UNKNOWN, DEVS_2G4)); /* site 1: USB  */
    EXPECT_EQ(0, hsm_profile_of_devs(DEVS_2G4));                      /* site 2: none */
    EXPECT_EQ(DEVS_BT1, hsm_seed_btdev(DEVS_USB, DEVS_2G4));          /* site 3: BT1  */
}

/* The EEPROM recovery rule is the same predicate, now host-testable: a persisted
 * `devs` inside the BT range wins, anything else yields the persisted fallback
 * (which the shell reads from `confinfo.last_btdevs`). */
TEST_F(TransportLogic, eeprom_bt_subindex_recovers_through_the_core_rule) {
    EXPECT_EQ(DEVS_BT3, hsm_btdev_or(DEVS_BT3, DEVS_BT1)); /* a good value survives */
    EXPECT_EQ(DEVS_BT4, hsm_btdev_or(DEVS_USB, DEVS_BT4)); /* corrupt -> fallback  */
    EXPECT_EQ(DEVS_BT1, hsm_btdev_or(DEVS_2G4, DEVS_BT1));
}

/* --- the confinfo migration gate (F6) --- */

TEST_F(TransportLogic, confinfo_redefaults_on_a_blank_block) {
    EXPECT_TRUE(hsm_confinfo_needs_default(0, 0, 1));
}

TEST_F(TransportLogic, confinfo_redefaults_on_a_version_change) {
    EXPECT_TRUE(hsm_confinfo_needs_default(0x1234, 0, 1)); /* older layout */
    EXPECT_TRUE(hsm_confinfo_needs_default(0x1234, 2, 1)); /* newer layout */
}

TEST_F(TransportLogic, confinfo_survives_a_matching_version) {
    EXPECT_FALSE(hsm_confinfo_needs_default(0x1234, 1, 1));
}