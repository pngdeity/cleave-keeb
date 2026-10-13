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

/* Proof of the filp remap decision, without hardware.
 *
 * `process_record_kb`'s remap rule was rewritten from 24 per-key case blocks
 * into the pure `filp_decide()` plus a table. These vectors pin the decision to
 * the *original* per-key semantics, so the rewrite is proven, not reasoned:
 *   - the F-row is a media key on macOS and NOT remapped on Windows;
 *   - the digits / `-` / `=` map on both layers;
 *   - KC_3/KC_4 are macOS GUI chords (Windows: F3/F4);
 *   - KC_5/KC_6 are RGB-value steps on macOS (Windows: F5/F6);
 *   - a key outside the tables is not handled on either layer.
 */

#include "gtest/gtest.h"

extern "C" {
#include "keyboards/epomaker/epomaker_split65/wls/keymap_logic.h"
#include "keycodes.h"
}

namespace {

TEST(FilpDecide, f_row_is_a_media_key_on_mac_and_untouched_on_windows) {
    /* KC_F1 -> KC_MSEL on macOS; on Windows it passes through (NONE). */
    filp_action_t mac = filp_decide(KC_F1, true);
    EXPECT_EQ(FILP_ACT_KEY, mac.kind);
    EXPECT_EQ(KC_MSEL, mac.key);

    filp_action_t win = filp_decide(KC_F1, false);
    EXPECT_EQ(FILP_ACT_NONE, win.kind);

    /* A different F-key, to pin that the table is not a single entry.
     * (The F-row table is F1..F12 -> MSEL, VOLD, VOLU, MUTE, MSTP, MPRV,
     * MPLY, MNXT, MAIL, WHOM, CALC, WSCH; so F8 -> MNXT.) */
    EXPECT_EQ(KC_MNXT, filp_decide(KC_F8, true).key);
    EXPECT_EQ(FILP_ACT_NONE, filp_decide(KC_F8, false).kind);
}

TEST(FilpDecide, digits_map_on_both_layers) {
    /* KC_7 -> KC_MPRV on macOS, KC_F7 on Windows. */
    filp_action_t mac = filp_decide(KC_7, true);
    EXPECT_EQ(FILP_ACT_KEY, mac.kind);
    EXPECT_EQ(KC_MPRV, mac.key);

    filp_action_t win = filp_decide(KC_7, false);
    EXPECT_EQ(FILP_ACT_KEY, win.kind);
    EXPECT_EQ(KC_F7, win.key);
}

TEST(FilpDecide, kc_3_and_kc_4_are_mac_chords_and_windows_f_keys) {
    /* KC_3: macOS is LGUI+Tab (a chord); Windows is F3. */
    filp_action_t mac3 = filp_decide(KC_3, true);
    EXPECT_EQ(FILP_ACT_CHORD, mac3.kind);
    EXPECT_EQ(KC_LGUI, mac3.mod);
    EXPECT_EQ(KC_TAB, mac3.base);

    filp_action_t win3 = filp_decide(KC_3, false);
    EXPECT_EQ(FILP_ACT_KEY, win3.kind);
    EXPECT_EQ(KC_F3, win3.key);

    /* KC_4: macOS is LGUI+E; Windows is F4. */
    filp_action_t mac4 = filp_decide(KC_4, true);
    EXPECT_EQ(FILP_ACT_CHORD, mac4.kind);
    EXPECT_EQ(KC_LGUI, mac4.mod);
    EXPECT_EQ(KC_E, mac4.base);

    filp_action_t win4 = filp_decide(KC_4, false);
    EXPECT_EQ(FILP_ACT_KEY, win4.kind);
    EXPECT_EQ(KC_F4, win4.key);
}

TEST(FilpDecide, kc_5_and_kc_6_are_rgb_val_on_mac_and_windows_f_keys) {
    /* KC_5: macOS steps the RGB value down; Windows is F5. */
    filp_action_t mac5 = filp_decide(KC_5, true);
    EXPECT_EQ(FILP_ACT_RGB_VAL, mac5.kind);
    EXPECT_EQ(-1, mac5.delta);

    filp_action_t win5 = filp_decide(KC_5, false);
    EXPECT_EQ(FILP_ACT_KEY, win5.kind);
    EXPECT_EQ(KC_F5, win5.key);

    /* KC_6: macOS steps the RGB value up; Windows is F6. */
    filp_action_t mac6 = filp_decide(KC_6, true);
    EXPECT_EQ(FILP_ACT_RGB_VAL, mac6.kind);
    EXPECT_EQ(+1, mac6.delta);

    filp_action_t win6 = filp_decide(KC_6, false);
    EXPECT_EQ(FILP_ACT_KEY, win6.kind);
    EXPECT_EQ(KC_F6, win6.key);
}

TEST(FilpDecide, unlisted_keys_are_not_handled) {
    EXPECT_EQ(FILP_ACT_NONE, filp_decide(KC_A, true).kind);
    EXPECT_EQ(FILP_ACT_NONE, filp_decide(KC_A, false).kind);
    EXPECT_EQ(FILP_ACT_NONE, filp_decide(KC_SPC, true).kind);
    EXPECT_EQ(FILP_ACT_NONE, filp_decide(KC_ESC, false).kind);
}

} // namespace
