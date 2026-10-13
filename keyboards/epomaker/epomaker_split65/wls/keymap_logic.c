// Copyright 2026
// SPDX-License-Identifier: GPL-2.0-or-later

#include "keymap_logic.h"

#include "keycodes.h"
#include "util.h"

/* The mapping, as data. Each entry is one row of the table the shell used to
 * spell out as a case block; keeping it here (not in the shell) is what makes
 * the rule testable and keeps mode logic out of the HID-emission path. */
static const struct {
    uint16_t key;
    uint16_t mac;
    uint16_t win;
} filp_dual[] = {
    {KC_1, KC_BRID, KC_F1}, {KC_2, KC_BRIU, KC_F2}, {KC_3, KC_NO, KC_F3}, {KC_4, KC_NO, KC_F4}, {KC_5, KC_NO, KC_F5}, {KC_6, KC_NO, KC_F6}, {KC_7, KC_MPRV, KC_F7}, {KC_8, KC_MPLY, KC_F8}, {KC_9, KC_MNXT, KC_F9}, {KC_0, KC_MUTE, KC_F10}, {KC_MINS, KC_VOLD, KC_F11}, {KC_EQL, KC_VOLU, KC_F12},
};

/* The macOS-side actions that are not a single keycode: two-key chords and the
 * RGB-value steps. Their Windows side is a plain F-key, so those keys also
 * appear in `filp_dual` with a KC_NO macOS target. */
static const struct {
    uint16_t key;
    uint16_t mod;   /* chord modifier, or KC_NO for a bare action */
    uint16_t base;  /* chord second key, or KC_NO */
    int8_t   delta; /* RGB-value direction, or 0 for a chord */
} filp_mac_special[] = {
    {KC_3, KC_LGUI, KC_TAB, 0},
    {KC_4, KC_LGUI, KC_E, 0},
    {KC_5, KC_NO, KC_NO, -1},
    {KC_6, KC_NO, KC_NO, +1},
};

/* The F-row: a media key on macOS; on Windows these keys are not remapped at
 * all (they pass through as ordinary F-keys), so this table is consulted only
 * when `is_mac`. */
static const struct {
    uint16_t key;
    uint16_t mac;
} filp_mac_only[] = {
    {KC_F1, KC_MSEL}, {KC_F2, KC_VOLD}, {KC_F3, KC_VOLU}, {KC_F4, KC_MUTE}, {KC_F5, KC_MSTP}, {KC_F6, KC_MPRV}, {KC_F7, KC_MPLY}, {KC_F8, KC_MNXT}, {KC_F9, KC_MAIL}, {KC_F10, KC_WHOM}, {KC_F11, KC_CALC}, {KC_F12, KC_WSCH},
};

filp_action_t filp_decide(uint16_t keycode, bool is_mac) {
    filp_action_t none = {FILP_ACT_NONE, 0, 0, 0, 0};

    /* The F-row remaps only on macOS; on Windows it is not a remapped key. */
    if (is_mac) {
        for (size_t i = 0; i < ARRAY_SIZE(filp_mac_only); i++) {
            if (filp_mac_only[i].key == keycode) {
                filp_action_t a = {FILP_ACT_KEY, filp_mac_only[i].mac, 0, 0, 0};
                return a;
            }
        }
    }

    /* Keys remapped on both layers. */
    for (size_t i = 0; i < ARRAY_SIZE(filp_dual); i++) {
        if (filp_dual[i].key != keycode) {
            continue;
        }
        uint16_t target = is_mac ? filp_dual[i].mac : filp_dual[i].win;
        /* A macOS target of KC_NO means the macOS action is not a single
         * keycode; fall through to `filp_mac_special`. */
        if (is_mac && target == KC_NO) {
            break;
        }
        filp_action_t a = {FILP_ACT_KEY, target, 0, 0, 0};
        return a;
    }

    if (!is_mac) {
        return none;
    }

    /* The macOS-side chords and RGB-value actions. */
    for (size_t i = 0; i < ARRAY_SIZE(filp_mac_special); i++) {
        if (filp_mac_special[i].key != keycode) {
            continue;
        }
        if (filp_mac_special[i].delta != 0) {
            filp_action_t a = {FILP_ACT_RGB_VAL, 0, 0, 0, filp_mac_special[i].delta};
            return a;
        }
        filp_action_t a = {FILP_ACT_CHORD, 0, filp_mac_special[i].mod, filp_mac_special[i].base, 0};
        return a;
    }

    return none;
}
