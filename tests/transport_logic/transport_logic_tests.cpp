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
