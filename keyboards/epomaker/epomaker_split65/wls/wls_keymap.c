// Copyright 2026
// SPDX-License-Identifier: GPL-2.0-or-later

#include "wls_keymap.h"

#include "wls/wls.h"
#include "wls/wls_eeprom.h"
#include "wls/keymap_logic.h"
#include "wls/transport_logic.h"
#include "wls/wls_module.h"
#include "wls/rgb_key_logic.h"
#include "wls/wls_indicator.h"
#include "rgb_record/rgb_record.h"
#include "quantum.h"
#include "connection.h"

#ifdef WIRELESS_ENABLE
#    include "wireless.h"
#    include "lowpower.h"
#    include "module.h"
#endif

/* The battery-query key's latch: while held, the battery indicator is shown at
 * the charging colour. Board state that belongs to the indicator, not the
 * keymap; it lives here because this concern owns the keycode that sets it and
 * is read by the indicator's paint pass. */
bool im_bat_req_charging_flag = false;

/* The EEPROM-clear hold timer. Armed on `EE_CLR` press, cleared on release; the
 * indicator pass fires the reset when it has been held past the threshold. One
 * deadline, read as a comparison. */
uint32_t wls_ee_clr_timer = 0;

/* The rgblight mode table. The `RL_MOD` keycode steps through it; mode 10 turns
 * rgblight off. Also the boot default (see `wls_lifecycle_boot`), so it is
 * exposed rather than file-local. */
const uint8_t wls_rgblight_modes[] = {14, 8, 2, 1, 1, 1, 1, 1, 1, 1, 0};

/* MOR_1..4: hold Left-Ctrl plus `base` while the key is down. (The filp remap
 * rule lives in the pure core `wls/keymap_logic`; only the Ctrl-combo family
 * stays here, because it is a one-line effect with no layer/filp decision.) */
static const struct {
    uint16_t mor;
    uint16_t base;
} mor_map[] = {
    {MOR_1, KC_Z},
    {MOR_2, KC_X},
    {MOR_3, KC_C},
    {MOR_4, KC_V},
};

/* Perform one filp decision. The decision is the pure `filp_decide()`; this is
 * only the effect shell. */
static void filp_apply(const filp_action_t *act, bool pressed) {
    switch (act->kind) {
        case FILP_ACT_KEY:
            /* KC_NO means "swallowed, emit nothing". */
            if (act->key == KC_NO) {
                return;
            }
            if (pressed) {
                register_code16(act->key);
            } else {
                unregister_code16(act->key);
            }
            break;
        case FILP_ACT_CHORD:
            if (pressed) {
                register_code(act->mod);
                register_code(act->base);
            } else {
                unregister_code(act->mod);
                unregister_code(act->base);
            }
            break;
        case FILP_ACT_RGB_VAL:
            if (pressed) {
                if (act->delta < 0) {
                    rgb_matrix_decrease_val();
                } else {
                    rgb_matrix_increase_val();
                }
            }
            break;
        case FILP_ACT_NONE:
        default:
            break;
    }
}

/* The filp remap for `keycode`, or false if `keycode` is not a remapped key.
 * Called only when `confinfo.filp` is set: under filp every remapped key is
 * *replaced* by its layer-appropriate mapping (a KC_NO mapping emits nothing)
 * and swallowed. Returns true when the key was handled. The decision itself is
 * the pure `filp_decide()`; this function only applies it. */
static bool process_filp_key(uint16_t keycode, keyrecord_t *record) {
    filp_action_t act = filp_decide(keycode, keymap_is_mac_system());
    if (act.kind == FILP_ACT_NONE) {
        return false;
    }
    filp_apply(&act, record->event.pressed);
    return true;
}

#ifdef WIRELESS_ENABLE

uint32_t wls_keymap_long_press(uint32_t trigger_time, void *cb_arg) {
    (void)trigger_time;
    /* The deferred call carries the *resolved* target device, written once when
     * the timer was armed. It must not point at shared state: `defer_exec` stores
     * the pointer, so a pointer into a per-keypress variable would be rewritten
     * by any later keypress in the 3 s window and could re-pair the wrong
     * profile (audit fix 1). */
    uint8_t devs = (uint8_t)(uintptr_t)cb_arg;

    if (devs >= DEVS_BT1 && devs <= DEVS_BT5) {
        if (hs_keycode_allowed(devs)) {
            /* Re-pair the profile the driver already owns (14b): select it with
             * reset=true so the module gets CLEAN + devinfo + PAIR. */
            bluetooth_select_profile(devs, true);
            wls_indicate_devs(devs, true);
        }
    } else if (devs == DEVS_2G4) {
        if (hs_keycode_allowed(devs)) {
            wls_module_devs_change(wls_module_devs(), DEVS_2G4, true);
            wls_indicate_devs(DEVS_2G4, true);
        }
    }

    return 0;
}

static bool process_record_wls(uint16_t keycode, keyrecord_t *record) {
    static deferred_token wls_process_long_press_token = INVALID_DEFERRED_TOKEN;

#    ifndef WLS_KEYCODE_PAIR_TIME
#        define WLS_KEYCODE_PAIR_TIME 3000
#    endif

#    define WLS_KEYCODE_EXEC(wls_dev)                                                                                                                         \
        do {                                                                                                                                                  \
            if (record->event.pressed) {                                                                                                                      \
                /* Route through upstream's connection subsystem so it owns the                                                                               \
                 * persisted host choice; connection_host_changed_kb() then                                                                                   \
                 * maps it onto the vendor device index and transport. This is the                                                                            \
                 * USER-intent path, so it persists (`connection_set_host`), unlike                                                                           \
                 * the switch read at boot which is `_noeeprom` (14c). */                                                                                     \
                connection_host_t wls_host = (connection_host_t)hsm_host_of_devs(wls_dev);                                                                    \
                connection_set_host(wls_host);                                                                                                                \
                /* BT1..BT5 is the driver's sub-index, not a transport: tell the                                                                              \
                 * driver which profile so it owns the choice (14b). A short tap                                                                              \
                 * selects (reset=false); the long-press re-pairs (reset=true). */                                                                            \
                if ((wls_dev) >= DEVS_BT1 && (wls_dev) <= DEVS_BT5) {                                                                                         \
                    bluetooth_select_profile(wls_dev, false);                                                                                                 \
                }                                                                                                                                             \
                wls_persist_devs();                                                                                                                           \
                wls_indicate_devs((wls_dev), false);                                                                                                          \
                if (wls_process_long_press_token == INVALID_DEFERRED_TOKEN) {                                                                                 \
                    /* Capture the resolved target by value, not a pointer into                                                                               \
                     * per-keypress state (audit fix 1). */                                                                                                   \
                    wls_process_long_press_token = defer_exec(WLS_KEYCODE_PAIR_TIME, wls_keymap_long_press, (void *)(uintptr_t)hsm_long_press_devs(keycode)); \
                }                                                                                                                                             \
            } else {                                                                                                                                          \
                cancel_deferred_exec(wls_process_long_press_token);                                                                                           \
                wls_process_long_press_token = INVALID_DEFERRED_TOKEN;                                                                                        \
            }                                                                                                                                                 \
        } while (false)

    switch (keycode) {
        case KC_BT1: {
            if (hs_keycode_allowed(DEVS_BT1)) {
                WLS_KEYCODE_EXEC(DEVS_BT1);
                hs_link_activity();
            }

        } break;
        case KC_BT2: {
            if (hs_keycode_allowed(DEVS_BT2)) {
                WLS_KEYCODE_EXEC(DEVS_BT2);
                hs_link_activity();
            }
        } break;
        case KC_BT3: {
            if (hs_keycode_allowed(DEVS_BT3)) {
                WLS_KEYCODE_EXEC(DEVS_BT3);
                hs_link_activity();
            }
        } break;
        case KC_2G4: {
            if (hs_keycode_allowed(DEVS_2G4)) {
                WLS_KEYCODE_EXEC(DEVS_2G4);
                hs_link_activity();
            }
        } break;

        default:
            return true;
    }

    return false;
}

#endif /* WIRELESS_ENABLE */

#ifdef WIRELESS_ENABLE
/* Persist the user's explicit transport choice. Called only from the keycode
 * path, so the switch stays level-authoritative and a reboot cannot resurrect a
 * stale wireless index (defect 4). The vendor `wireless_devs_change_kb()` hook
 * is deliberately *not* overridden: it fires for every device-index change
 * including periodic re-asserts, so anything written there would be committed by
 * an unrelated whole-struct persist (`KC_FILP`). The live authority is
 * `wireless_get_current_devs()`; `confinfo.devs`/`.last_btdevs` are persisted
 * intent only. */
void wls_persist_devs(void) {
    uint8_t devs  = wireless_get_current_devs();
    confinfo.devs = devs;
    if (devs >= DEVS_BT1 && devs <= DEVS_BT5) {
        confinfo.last_btdevs = devs;
    }
    eeconfig_confinfo_update(confinfo.raw);
}
#endif /* WIRELESS_ENABLE */

/* Step the rgblight mode. The one non-obvious part: when rgblight is in its HSV
 * mode (1), the current hue is decoded back into a mode index so the step
 * continues from what the user sees. */
static void im_rgblight_increase(void) {
    HSV            rgb;
    uint8_t        moude;
    static uint8_t mode = 0;

    moude = rgblight_get_mode();
    if (moude == 1) {
        rgb = rgblight_get_hsv();
        if (rgb.h == 0 && rgb.s != 0)
            mode = 3;
        else
            mode = 9;
        switch (rgb.h) {
            case 40: {
                mode = 4;
            } break;
            case 80: {
                mode = 5;
            } break;
            case 120: {
                mode = 6;
            } break;
            case 160: {
                mode = 7;
            } break;
            case 200: {
                mode = 8;
            } break;
            default:
                break;
        }
    }

    mode++;
    if (mode == 11) mode = 0;
    if (mode == 10) {
        rgb = rgblight_get_hsv();
        rgblight_sethsv(0, 255, rgb.v);
        rgblight_disable();
    } else {
        rgblight_enable();
        rgblight_mode(wls_rgblight_modes[mode]);
    }

    rgb = rgblight_get_hsv();
    switch (mode) {
        case 3: {
            rgblight_sethsv(0, 255, rgb.v);
        } break;
        case 4: {
            rgblight_sethsv(40, 255, rgb.v);
        } break;
        case 5: {
            rgblight_sethsv(80, 255, rgb.v);
        } break;
        case 6: {
            rgblight_sethsv(120, 255, rgb.v);
        } break;
        case 7: {
            rgblight_sethsv(160, 255, rgb.v);
        } break;
        case 8: {
            rgblight_sethsv(200, 255, rgb.v);
        } break;
        case 9: {
            rgblight_sethsv(0, 0, rgb.v);
        } break;
        case 0: {
            rgblight_set_speed(255);
        } break;
        default: {
            rgblight_set_speed(200);
        } break;
    }
}

bool wls_keymap_process_record(uint16_t keycode, keyrecord_t *record) {
#ifdef WIRELESS_ENABLE
    if (process_record_wls(keycode, record) != true) {
        return false;
    }
#endif

    /* Ctrl-combo macros (MOR_1..4). */
    for (size_t i = 0; i < ARRAY_SIZE(mor_map); i++) {
        if (mor_map[i].mor == keycode) {
            if (record->event.pressed) {
                register_code(KC_LCTL);
                register_code(mor_map[i].base);
            } else {
                unregister_code(KC_LCTL);
                unregister_code(mor_map[i].base);
            }
            break;
        }
    }

    /* The filp remap (F1..F12, digits, -/=): swallow the key when it applies. */
    if (confinfo.filp && process_filp_key(keycode, record)) {
        return false;
    }

    switch (keycode) {
        case KC_FILP: {
            if (record->event.pressed) {
                confinfo.filp = !confinfo.filp;
                eeconfig_confinfo_update(confinfo.raw);
            }
            return false;
        } break;
        case KC_BATQ: {
            if (record->event.pressed) {
                im_bat_req_charging_flag = true;
            } else {
                im_bat_req_charging_flag = false;
            }
        } break;
        case QK_BOOT: {
            if (record->event.pressed) {
                dprintf("into boot!!!\r\n");
                eeconfig_disable();
                bootloader_jump();
            }
        } break;

        case NK_TOGG: {
            if (rgbrec_is_started()) {
                return false;
            }
            if (record->event.pressed) {
                rgb_matrix_hs_indicator_set(0xFF, (RGB){0x00, 0x6E, 0x00}, 250, 1);
            }
        } break;
        case RL_MOD: {
            if (rgbrec_is_started()) {
                return false;
            }
            if (record->event.pressed) {
                im_rgblight_increase();
            }

            return false;
        } break;
        case EE_CLR: {
            if (record->event.pressed) {
                wls_ee_clr_timer = timer_read32();
            } else {
                wls_ee_clr_timer = 0;
            }

            return false;
        } break;
        case QK_RGB_MATRIX_SPEED_UP:
        case QK_RGB_MATRIX_SPEED_DOWN:
        case QK_RGB_MATRIX_MODE_NEXT:
        case QK_RGB_MATRIX_VALUE_UP:
        case QK_RGB_MATRIX_VALUE_DOWN: {
            /* The thresholds and the mode step are the pure `wls_rgb_key_decide()`
             * (wls/rgb_key_logic); this is only the effect shell. */
            wls_rgb_state_t      st = {rgb_matrix_get_speed(), rgb_matrix_get_val(), rgb_matrix_get_mode(), record->event.pressed};
            wls_rgb_key_action_t ra = wls_rgb_key_decide(keycode, &st);
            if (ra.kind == WLS_RGB_KEY_FLOOR) {
                /* The value is at the floor and must not go lower (X-7). Consume
                 * the record so upstream's `process_rgb_matrix` never runs
                 * `rgb_matrix_decrease_val()`: the decider can name the refusal,
                 * but only this `false` makes it real. */
                return false;
            }
            if (ra.enable) {
                rgb_matrix_enable();
                wls_led_rail_apply();
            }
            switch (ra.kind) {
                case WLS_RGB_KEY_ACK_DIR:
                    rgb_blink_dir();
                    break;
                case WLS_RGB_KEY_SET_MODE:
                    rgb_matrix_mode(ra.param);
                    break;
                case WLS_RGB_KEY_NONE:
                default:
                    break;
            }
        } break;

        case TO(_BL): {
            if (record->event.pressed) {
                rgb_matrix_hs_cancel_index(HS_RGB_BLINK_INDEX_MAC);
                rgb_matrix_hs_indicator_set(HS_RGB_BLINK_INDEX_WIN, (RGB){RGB_WHITE}, 250, 3);
                if (keymap_is_mac_system()) {
                    set_single_persistent_default_layer(_BL);
                    layer_move(0);
                }
            }

            return false;
        } break;
        case TO(_MBL): {
            if (record->event.pressed) {
                rgb_matrix_hs_cancel_index(HS_RGB_BLINK_INDEX_WIN);
                rgb_matrix_hs_indicator_set(HS_RGB_BLINK_INDEX_MAC, (RGB){RGB_WHITE}, 250, 3);
                if (!keymap_is_mac_system()) {
                    set_single_persistent_default_layer(_MBL);
                    layer_move(0);
                }
            }

            return false;
        } break;

        case KC_LCMD: {
            if (keymap_is_mac_system()) {
                if (keymap_config.no_gui && !rgbrec_is_started()) {
                    if (record->event.pressed) {
                        register_code16(KC_LCMD);
                    } else {
                        unregister_code16(KC_LCMD);
                    }
                }
            }

            return true;
        } break;
        case KC_RCMD: {
            if (keymap_is_mac_system()) {
                if (keymap_config.no_gui && !rgbrec_is_started()) {
                    if (record->event.pressed) {
                        register_code16(KC_RCMD);
                    } else {
                        unregister_code16(KC_RCMD);
                    }
                }
            }

            return true;
        } break;
        case HS_BATQ: {
            extern bool rk_bat_req_flag;
            rk_bat_req_flag = (wireless_get_current_devs() != DEVS_USB) && record->event.pressed;
            return false;
        } break;

        default:
            break;
    }

    return true;
}
