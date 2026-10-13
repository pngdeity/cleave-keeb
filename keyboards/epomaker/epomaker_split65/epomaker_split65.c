// Copyright 2024 yangzheng20003 (@yangzheng20003)
// SPDX-License-Identifier: GPL-2.0-or-later

#include QMK_KEYBOARD_H
#include "wls/wls.h"
#include "wls/transport_logic.h"
#include "wls/keymap_logic.h"
#include "wls/wls_eeprom.h"
#include "wls/wls_usb.h"
#include "wls/split_logic.h"
#include "wls/reset_logic.h"
#include "wls/wls_split.h"
#include "wls/wls_lifecycle.h"
#include "wls/indicator_logic.h"
#include "rgb_record/rgb_record.h"
#include "quantum.h"
#include "connection.h"
#include "serial_usart.h"
#include "usb_util.h"
#ifdef WIRELESS_ENABLE
#    include "wireless.h"
#    include "usb_main.h"
#    include "lowpower.h"
#    include "module.h"
#endif

typedef struct {
    bool        active;
    wls_blink_t blink; /* the shared blink timing (wls/indicator_logic) */
    uint8_t     index;
    RGB         rgb;
    void (*blink_cb)(uint8_t);
} hs_rgb_indicator_t;

enum layers {
    _BL = 0,
    _FL,
    _MBL,
    _MFL,
};

hs_rgb_indicator_t hs_rgb_indicators[HS_RGB_INDICATOR_COUNT];
hs_rgb_indicator_t hs_rgb_bat[HS_RGB_BAT_COUNT];

void user_sync_mms_slave_handler(uint8_t in_buflen, const void *in_data, uint8_t out_buflen, void *out_data);
void rgb_blink_dir(void);
void hs_reset_settings(wls_reset_cause_t cause);
void rgb_matrix_hs_indicator(void);
void rgb_matrix_hs_indicator_set(uint8_t index, RGB rgb, uint32_t interval, uint8_t times);
void rgb_matrix_hs_set_remain_time(uint8_t index, uint8_t remain_time);

#define keymap_is_mac_system() ((get_highest_layer(default_layer_state) == _MBL) || (get_highest_layer(default_layer_state) == _MFL))
#define keymap_is_base_layer() ((get_highest_layer(default_layer_state) == _BL) || (get_highest_layer(default_layer_state) == _FL))

/* The battery indicator's suppression deadline (0 = not suppressed). Set to
 * "now + WLS_BAT_SUPPRESS_MS" by an EEPROM reset; the indicator is hidden while
 * `timer_read32() < bat_suppress_until`. One deadline read as a comparison by
 * the shell, instead of a bool plus a parallel timer kept in lockstep and
 * re-derived every frame. */
static uint32_t bat_suppress_until = 0;
static uint32_t ee_clr_timer       = 0;
static bool     no_record_fg;
static bool     im_bat_req_charging_flag = false;

static const uint8_t buff[] = {14, 8, 2, 1, 1, 1, 1, 1, 1, 1, 0};

void master_sync_mms_slave(uint8_t last_mode, uint8_t now_mode, uint8_t reset) {
    const uint8_t body[4] = {last_mode, now_mode, reset, 0};
    if (wls_split_send(WLS_SPLIT_CMD_SYNC_MULTIMODE, body)) {
        dprintf("Slave Sleep OK1\n");
    } else {
        dprintf("Slave sync failed1!\n");
    }
}

void keyboard_post_init_kb(void) {
#ifdef CONSOLE_ENABLE
    debug_enable = true;
#endif
    /* Diagnostic boot counter: one increment per MCU boot, so a host reader can
     * tell a firmware reset loop from a mere USB-line drop (see `wls_diag`). */
    wls_note_boot();

    eeconfig_confinfo_init();
#ifdef RGBLIGHT_ENABLE
    rgblight_mode(buff[0]);
#endif

    wls_lifecycle_boot();

    keyboard_post_init_user();

    rgbrec_init(confinfo.record_channel);

    // usart_init();
    transaction_register_rpc(USER_SYNC_MMS, user_sync_mms_slave_handler);
}

#ifdef WIRELESS_ENABLE

void suspend_power_down_kb(void) {
    wls_lifecycle_suspend_power_down_kb();

    suspend_power_down_user();
}

void suspend_wakeup_init_kb(void) {
    wls_lifecycle_suspend_wakeup_init_kb();
    suspend_wakeup_init_user();
    hs_link_activity();
}

void suspend_wakeup_init_user(void) {
    wls_lifecycle_suspend_wakeup_init_user();
}

void suspend_power_down_user(void) {
    wls_lifecycle_suspend_power_down_user();
}

bool lpwr_is_allow_timeout_hook(void) {
    return wls_lifecycle_lpwr_allow_timeout();
}

bool lpwr_is_allow_presleep_hook(void) {
    return wls_lifecycle_lpwr_allow_presleep();
}

void wireless_post_task(void) {
    wls_lifecycle_wireless_post_task();
    kb_battery_push_task();
}

uint32_t wls_process_long_press(uint32_t trigger_time, void *cb_arg) {
    (void)trigger_time;
    /* The deferred call carries the *resolved* target device, written once when
     * the timer was armed. It must not point at shared state: `defer_exec` stores
     * the pointer, so a pointer into a per-keypress variable would be rewritten
     * by any later keypress in the 3 s window and could re-pair the wrong
     * profile (audit fix 1). */
    uint8_t devs = (uint8_t)(uintptr_t)cb_arg;

    if (devs >= DEVS_BT1 && devs <= DEVS_BT5) {
        if (hs_keycode_allowed(devs, confinfo.last_btdevs)) {
            /* Re-pair the profile the driver already owns (14b): select it with
             * reset=true so the module gets CLEAN + devinfo + PAIR. */
            bluetooth_select_profile(devs, true);
            wls_indicate_devs(devs, true);
        }
    } else if (devs == DEVS_2G4) {
        if (hs_keycode_allowed(devs, confinfo.last_btdevs)) {
            wireless_devs_change(wireless_get_current_devs(), DEVS_2G4, true);
            wls_indicate_devs(DEVS_2G4, true);
        }
    }

    return 0;
}

bool process_record_wls(uint16_t keycode, keyrecord_t *record) {
    static deferred_token wls_process_long_press_token = INVALID_DEFERRED_TOKEN;

#    ifndef WLS_KEYCODE_PAIR_TIME
#        define WLS_KEYCODE_PAIR_TIME 3000
#    endif

#    define WLS_KEYCODE_EXEC(wls_dev)                                                                                                                          \
        do {                                                                                                                                                   \
            if (record->event.pressed) {                                                                                                                       \
                /* Route through upstream's connection subsystem so it owns the                                                                                \
                 * persisted host choice; connection_host_changed_kb() then                                                                                    \
                 * maps it onto the vendor device index and transport. This is the                                                                             \
                 * USER-intent path, so it persists (`connection_set_host`), unlike                                                                            \
                 * the switch read at boot which is `_noeeprom` (14c). */                                                                                      \
                connection_host_t wls_host = (connection_host_t)hsm_host_of_devs(wls_dev);                                                                     \
                connection_set_host(wls_host);                                                                                                                 \
                /* BT1..BT5 is the driver's sub-index, not a transport: tell the                                                                               \
                 * driver which profile so it owns the choice (14b). A short tap                                                                               \
                 * selects (reset=false); the long-press re-pairs (reset=true). */                                                                             \
                if ((wls_dev) >= DEVS_BT1 && (wls_dev) <= DEVS_BT5) {                                                                                          \
                    bluetooth_select_profile(wls_dev, false);                                                                                                  \
                }                                                                                                                                              \
                wls_persist_devs();                                                                                                                            \
                wls_indicate_devs((wls_dev), false);                                                                                                           \
                if (wls_process_long_press_token == INVALID_DEFERRED_TOKEN) {                                                                                  \
                    /* Capture the resolved target by value, not a pointer into                                                                                \
                     * per-keypress state (audit fix 1). */                                                                                                    \
                    wls_process_long_press_token = defer_exec(WLS_KEYCODE_PAIR_TIME, wls_process_long_press, (void *)(uintptr_t)hsm_long_press_devs(keycode)); \
                }                                                                                                                                              \
            } else {                                                                                                                                           \
                cancel_deferred_exec(wls_process_long_press_token);                                                                                            \
                wls_process_long_press_token = INVALID_DEFERRED_TOKEN;                                                                                         \
            }                                                                                                                                                  \
        } while (false)

    switch (keycode) {
        case KC_BT1: {
            if (hs_keycode_allowed(DEVS_BT1, confinfo.last_btdevs)) {
                WLS_KEYCODE_EXEC(DEVS_BT1);
                hs_link_activity();
            }

        } break;
        case KC_BT2: {
            if (hs_keycode_allowed(DEVS_BT2, confinfo.last_btdevs)) {
                WLS_KEYCODE_EXEC(DEVS_BT2);
                hs_link_activity();
            }
        } break;
        case KC_BT3: {
            if (hs_keycode_allowed(DEVS_BT3, confinfo.last_btdevs)) {
                WLS_KEYCODE_EXEC(DEVS_BT3);
                hs_link_activity();
            }
        } break;
        case KC_2G4: {
            if (hs_keycode_allowed(DEVS_2G4, confinfo.last_btdevs)) {
                WLS_KEYCODE_EXEC(DEVS_2G4);
                hs_link_activity();
            }
        } break;

        default:
            return true;
    }

    return false;
}
#endif

bool process_record_user(uint16_t keycode, keyrecord_t *record) {
    if (*md_getp_state() == MD_STATE_CONNECTED) {
        hs_link_activity();
    }

    switch (keycode) {
        case MO(_FL):
        case MO(_MFL): {
            if (!record->event.pressed && rgbrec_is_started()) {
                if (no_record_fg == true) {
                    no_record_fg = false;
                    rgbrec_register_record(keycode, record);
                }
                no_record_fg = true;
            }
            break;
        }

        case QK_RGB_MATRIX_MODE_NEXT:
            break;
        default: {
            if (rgbrec_is_started()) {
                if (!IS_QK_MOMENTARY(keycode) && record->event.pressed) {
                    rgbrec_register_record(keycode, record);

                    return false;
                }
            }
        } break;
    }

    return true;
}

void im_rgblight_increase(void) {
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
        rgblight_mode(buff[mode]);
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

/* --- process_record_kb dispatch tables -------------------------------------
 *
 * Two families in `process_record_kb` were the same rule written out once per
 * keycode (wiring review Phase C.6): the Ctrl-combo macros (MOR_1..4) and the
 * Fn/number remap (24 keys that become a macOS media key or a Windows F-key
 * depending on the persisted `confinfo.filp` and the current default layer).
 * Writing them as tables keeps the mode/layer rule and the HID emission apart
 * instead of interleaving them in a 400-line switch (CMU 18-642: don't mix mode
 * logic with output sequences), and makes the mapping reviewable as data.
 */

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

bool process_record_kb(uint16_t keycode, keyrecord_t *record) {
    if (process_record_user(keycode, record) != true) {
        return false;
    }

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
                ee_clr_timer = timer_read32();
            } else {
                ee_clr_timer = 0;
            }

            return false;
        } break;
        case QK_RGB_MATRIX_SPEED_UP: {
            if (record->event.pressed) {
                if (rgb_matrix_get_speed() >= 215) {
                    rgb_blink_dir();
                }
            }
        } break;
        case QK_RGB_MATRIX_SPEED_DOWN: {
            if (record->event.pressed) {
                if (rgb_matrix_get_speed() <= 95) {
                    rgb_blink_dir();
                }
            }
        } break;
        case QK_RGB_MATRIX_VALUE_UP: {
            if (record->event.pressed) {
                rgb_matrix_enable();
                wls_led_rail_apply();
                if (rgb_matrix_get_speed() >= 120) {
                    rgb_blink_dir();
                }
            }
        } break;
        case QK_RGB_MATRIX_VALUE_DOWN: {
            if (record->event.pressed) {
                if (rgb_matrix_get_val() <= RGB_MATRIX_VAL_STEP) {
                    wls_led_rail_apply();
                    for (uint8_t i = 0; i < RGB_MATRIX_LED_COUNT; i++) {
                        rgb_matrix_set_color(i, 0, 0, 0);
                    }
                }
                if (rgb_matrix_get_speed() <= 30) {
                    rgb_blink_dir();
                }
            }
        } break;

        case TO(_BL): {
            if (record->event.pressed) {
                rgb_matrix_hs_set_remain_time(HS_RGB_BLINK_INDEX_MAC, 0);
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
                rgb_matrix_hs_set_remain_time(HS_RGB_BLINK_INDEX_WIN, 0);
                rgb_matrix_hs_indicator_set(HS_RGB_BLINK_INDEX_MAC, (RGB){RGB_WHITE}, 250, 3);
                if (!keymap_is_mac_system()) {
                    set_single_persistent_default_layer(_MBL);
                    layer_move(0);
                }
            }

            return false;
        } break;

        case QK_RGB_MATRIX_MODE_NEXT: {
            if (record->event.pressed) {
                uint8_t mode = rgb_matrix_get_mode();
                if (mode == 29) {
                    rgb_matrix_mode(31);
                    return false;
                }
            }
            return true;

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

/* Cable-driven transport arbitration — REMOVED (functional requirement 2).
 *
 * Historically `hs_transport_arbitrate_cable()` owned the "switch to USB on
 * cable insert, restore the remembered wireless transport on remove" policy.
 * That defeats requirement 2: the operator charges from an external PSU while
 * using the keyboard wirelessly, so "a cable is present" must NOT by itself
 * mean "switch to USB". Mode selection now belongs solely to the physical mode
 * switch (hs_modeio_detection(), wls/wls.c). It was first gutted to a no-op and
 * its callers dropped in the design cleanup; the history is in docs/FINDINGS.md.
 */

void housekeeping_task_user(void) { // loop
    wls_lifecycle_housekeeping();

#ifdef WIRELESS_ENABLE
    if (is_keyboard_master()) {
        static uint32_t last_sync = 0;
        if (timer_elapsed32(last_sync) > 2000) {
            last_sync = timer_read32();
            master_sync_mms_slave(wireless_get_current_devs(), wireless_get_current_devs(), *md_getp_bat());
        }
    }
#endif
}

#ifdef RGB_MATRIX_ENABLE

#    ifdef WIRELESS_ENABLE
/* The channel-indicator flash, as one piece of state. The blink timing is the
 * shared `wls_blink_t` (wls/indicator_logic); the index and colour are this
 * concern's own. Bundling them keeps the state's scope honest instead of five
 * loose file-scope globals that any function could reach (Power of Ten Rule 6,
 * wiring review Phase C.5). */
static struct {
    wls_blink_t blink; /* the shared blink timing; interval_ms 0 = idle */
    uint8_t     index; /* LED to flash */
    RGB         rgb;   /* colour to flash */
} wls_indicator;

void rgb_matrix_wls_indicator_set(uint8_t index, RGB rgb, uint32_t interval, uint8_t times) {
    wls_blink_arm(&wls_indicator.blink, interval, times, timer_read32());
    wls_indicator.index = index;
    wls_indicator.rgb   = rgb;
}

/* The user's explicit transport intent is the only thing that may touch the
 * persisted index, and it is recorded solely by `wls_persist_devs()` on the
 * keycode path. The vendor `wireless_devs_change_kb()` hook is deliberately
 * *not* overridden: it fires for every device-index change including periodic
 * re-asserts, so anything written there would be committed by an unrelated
 * whole-struct persist (`KC_FILP`) and could resurrect a stale index at boot
 * (the defect-4 family). The live authority is `wireless_get_current_devs()`;
 * `confinfo.devs`/`.last_btdevs` are persisted intent only. */

/* Persist the user's explicit transport choice. Called only from the keycode
 * path, so the switch stays level-authoritative and a reboot cannot resurrect a
 * stale wireless index (defect 4). Records the vendor device index and the BT
 * sub-profile the switch cannot express; the values are decided by the pure
 * core so the rule is testable. */
void wls_persist_devs(void) {
    uint8_t devs  = wireless_get_current_devs();
    confinfo.devs = devs;
    if (devs >= DEVS_BT1 && devs <= DEVS_BT5) {
        confinfo.last_btdevs = devs;
    }
    eeconfig_confinfo_update(confinfo.raw);
}

/* The user-visible transport confirmation: flash the selected channel's LED
 * once. Called ONLY from genuine transition sites (the mode-switch edge, the
 * Fn+Q/W/E keycode path, a re-pair long-press) — never from a re-assert, so the
 * indicator can only mean "the channel just changed". `reset` selects the
 * re-pair (fast) vs select (slow) cadence. This is the same separation applied
 * to the LED rail: the effect has one owner and is impossible to trigger by a
 * no-op. */
void wls_indicate_devs(uint8_t new_devs, bool reset) {
    switch (new_devs) {
        case DEVS_BT1: {
            if (reset) {
                rgb_matrix_wls_indicator_set(HS_RGB_BLINK_INDEX_BT1, (RGB){HS_LBACK_COLOR_BT1}, 200, 1);
            } else {
                rgb_matrix_wls_indicator_set(HS_RGB_BLINK_INDEX_BT1, (RGB){HS_PAIR_COLOR_BT1}, 500, 1);
            }
        } break;
        case DEVS_BT2: {
            if (reset) {
                rgb_matrix_wls_indicator_set(HS_RGB_BLINK_INDEX_BT2, (RGB){HS_LBACK_COLOR_BT2}, 200, 1);
            } else {
                rgb_matrix_wls_indicator_set(HS_RGB_BLINK_INDEX_BT2, (RGB){HS_PAIR_COLOR_BT2}, 500, 1);
            }
        } break;
        case DEVS_BT3: {
            if (reset) {
                rgb_matrix_wls_indicator_set(HS_RGB_BLINK_INDEX_BT3, (RGB){HS_LBACK_COLOR_BT3}, 200, 1);
            } else {
                rgb_matrix_wls_indicator_set(HS_RGB_BLINK_INDEX_BT3, (RGB){HS_PAIR_COLOR_BT3}, 500, 1);
            }
        } break;
        case DEVS_BT4: {
            if (reset) {
                rgb_matrix_wls_indicator_set(41, (RGB){RGB_BLUE}, 200, 1);
            } else {
                rgb_matrix_wls_indicator_set(41, (RGB){RGB_BLUE}, 500, 1);
            }
        } break;
        case DEVS_BT5: {
            if (reset) {
                rgb_matrix_wls_indicator_set(42, (RGB){RGB_BLUE}, 200, 1);
            } else {
                rgb_matrix_wls_indicator_set(42, (RGB){RGB_BLUE}, 500, 1);
            }
        } break;
        case DEVS_2G4: {
            if (reset) {
                rgb_matrix_wls_indicator_set(HS_RGB_BLINK_INDEX_2G4, (RGB){HS_LBACK_COLOR_2G4}, 200, 1);
            } else {
                rgb_matrix_wls_indicator_set(HS_RGB_BLINK_INDEX_2G4, (RGB){HS_LBACK_COLOR_2G4}, 500, 1);
            }
        } break;
        default:
            break;
    }
}

/* Upstream's connection subsystem resolves the active host; the board's job
 * here is the physical side of a host change — the USB data-line connect and
 * disconnect — not a driver swap (item 14d). `host_get_active_driver()` picks
 * the driver from the host, so there is no `host_set_driver()` glue left.
 *
 * This hook does not touch `wireless_devs_change()`: that would re-enter
 * `handle_host_changed()`, and the vendor device index is already kept in
 * lockstep by the callers (the boot switch read and the keycode path).
 *
 * The predicate is the RESOLVED host (`connection_get_host()`), not the raw
 * argument: `handle_host_changed()` forwards `config.desired_host`, which can
 * be CONNECTION_HOST_AUTO. Testing the raw value would tear the USB data line
 * down whenever the host is AUTO, even when AUTO currently resolves to USB. */
void connection_host_changed_kb(connection_host_t host) {
    (void)host;
    wls_usb_connect(connection_get_host() == CONNECTION_HOST_USB);
}

/* Terminal predicate for the channel flash: it decides whether a completed
 * flash stays lit and NEVER re-arms. The flash is owed to a transition and is
 * armed exactly once, at the transition site (`wls_indicate_devs`); when it
 * completes it is done. Returning true here means "re-arm" — and the vendor
 * code returned true while the module was not connected, which re-armed the
 * flash forever: a half sitting on a non-connected channel blinked on a ~500 ms
 * cadence for as long as it stayed disconnected, on both halves (the indicator
 * is not master-gated). A time-driven repeat is not a channel change, so it is
 * not this function's to make; any "still trying to connect" motion must be an
 * explicit, bounded policy owned elsewhere, not an unbounded self-retrigger. */
bool rgb_matrix_wls_indicator_cb(void) {
    if (*md_getp_state() == MD_STATE_CONNECTED) {
        led_wakeup();
    }

    return false;
}

void rgb_matrix_wls_indicator(void) {
    /* Idle: the flash owns nothing, so it paints nothing (the LED is left as
     * some other writer set it). */
    if (wls_indicator.blink.interval_ms == 0) {
        return;
    }

    wls_blink_out_t out = wls_blink_tick(&wls_indicator.blink, timer_read32());

    if (out.kind == WLS_BLINK_DONE) {
        /* The flash is owed to a transition and is armed exactly once, at the
         * transition site; on completion it is done (the terminal predicate
         * decides whether the LED stays lit). */
        if (rgb_matrix_wls_indicator_cb() != true) {
            return;
        }
    }

    if (out.on) {
        rgb_matrix_set_color(wls_indicator.index, wls_indicator.rgb.r, wls_indicator.rgb.g, wls_indicator.rgb.b);
    } else {
        rgb_matrix_set_color(wls_indicator.index, 0x00, 0x00, 0x00);
    }
}

void rgb_matrix_hs_bat_set(uint8_t index, RGB rgb, uint32_t interval, uint8_t times) {
    for (int i = 0; i < HS_RGB_BAT_COUNT; i++) {
        if (!hs_rgb_bat[i].active) {
            hs_rgb_bat[i].active = true;
            wls_blink_arm(&hs_rgb_bat[i].blink, interval, times, timer_read32());
            hs_rgb_bat[i].index = index;
            hs_rgb_bat[i].rgb   = rgb;
            break;
        }
    }
}

void rgb_matrix_hs_bat(void) {
    for (int i = 0; i < HS_RGB_BAT_COUNT; i++) {
        if (!hs_rgb_bat[i].active) {
            continue;
        }

        wls_blink_out_t out = wls_blink_tick(&hs_rgb_bat[i].blink, timer_read32());

        if (out.kind == WLS_BLINK_DONE) {
            hs_rgb_bat[i].active = false;
        }

        if (out.on) {
            rgb_matrix_set_color(hs_rgb_bat[i].index, hs_rgb_bat[i].rgb.r, hs_rgb_bat[i].rgb.g, hs_rgb_bat[i].rgb.b);
        } else {
            rgb_matrix_set_color(hs_rgb_bat[i].index, 0x00, 0x00, 0x00);
        }
    }
}
void bat_indicators(void) {
    static uint32_t battery_process_time = 0;
    uint8_t         bat_level            = *md_getp_bat();

    if (!is_keyboard_master()) {
        return;
    }

    /* Always-on soft battery indicator across HS_MATRIX_BAT_SOFT_INDEX and
     * HS_MATRIX_BAT_SOFT_INDEX2 (two adjacent right-half bottom-row LEDs), at
     * full channel intensity so the charge state is legible at a glance rather
     * than a single dim LED. */
    if (rgb_matrix_get_val() != 0) {
        uint8_t r = 0x00, g = 0x00, b = 0x00;
        switch (wls_bat_colour_of(charging_state, bat_full_flag, bat_level)) {
            case WLS_BAT_GREEN:
                g = 0xFF;
                break;
            case WLS_BAT_BLUE:
                g = 0x40;
                b = 0xFF;
                break;
            case WLS_BAT_ORANGE:
                r = 0xFF;
                g = 0x80;
                break;
            case WLS_BAT_RED:
                r = 0xFF;
                break;
            case WLS_BAT_NONE:
            default:
                break;
        }
        rgb_matrix_set_color(HS_MATRIX_BAT_SOFT_INDEX, r, g, b);
        rgb_matrix_set_color(HS_MATRIX_BAT_SOFT_INDEX2, r, g, b);
    }

    if (charging_state && (bat_full_flag)) {
        battery_process_time = 0;
        if (im_bat_req_charging_flag) rgb_matrix_set_color(HS_MATRIX_BLINK_INDEX_BAT, 0xFF, 0x00, 0x00);
    } else if (charging_state) {
        battery_process_time = 0;
        if (im_bat_req_charging_flag) rgb_matrix_set_color(HS_MATRIX_BLINK_INDEX_BAT, 0x00, 0xFF, 0x00);
    } else if (bat_level <= BATTERY_CAPACITY_LOW) {
        rgb_matrix_hs_bat_set(HS_MATRIX_BLINK_INDEX_BAT, (RGB){0xFF, 0x00, 0x00}, 250, 1);

        if (bat_level <= BATTERY_CAPACITY_STOP) {
            if (!battery_process_time) {
                battery_process_time = timer_read32();
            }

            if (battery_process_time && timer_elapsed32(battery_process_time) > 60000) {
                battery_process_time = 0;
                if (hsm_should_order_sleep(bat_level, charging_state, BATTERY_CAPACITY_STOP)) {
                    wls_order_sleep(true);
                    lpwr_set_timeout_manual(true);
                }
            }
        }
    } else {
        battery_process_time = 0;
    }
}

#    endif

#endif

void rgb_blink_dir(void) {
    rgb_matrix_hs_indicator_set(54, (RGB){0, 0, 0}, 250, 1);
    rgb_matrix_hs_indicator_set(66, (RGB){0, 0, 0}, 250, 1);
    rgb_matrix_hs_indicator_set(67, (RGB){0, 0, 0}, 250, 1);
    rgb_matrix_hs_indicator_set(65, (RGB){0, 0, 0}, 250, 1);
}

bool hs_reset_settings_user(void) {
    rgb_matrix_hs_indicator_set(0xFF, (RGB){0x10, 0x10, 0x10}, 250, 3);

    return true;
}

void nkr_indicators_hook(uint8_t index) {
    if ((hs_rgb_indicators[index].rgb.r == 0x6E) && (hs_rgb_indicators[index].rgb.g == 0x00) && (hs_rgb_indicators[index].rgb.b == 0x00)) {
        rgb_matrix_hs_indicator_set(0xFF, (RGB){0x6E, 0x00, 0x00}, 250, 1);

    } else if ((hs_rgb_indicators[index].rgb.r == 0x00) && (hs_rgb_indicators[index].rgb.g == 0x6E) && (hs_rgb_indicators[index].rgb.b == 0x00)) {
        rgb_matrix_hs_indicator_set(0xFF, (RGB){0x00, 0x00, 0x6F}, 250, 1);
    }
}

void rgb_matrix_hs_indicator_set(uint8_t index, RGB rgb, uint32_t interval, uint8_t times) {
    for (int i = 0; i < HS_RGB_INDICATOR_COUNT; i++) {
        if (!hs_rgb_indicators[i].active) {
            hs_rgb_indicators[i].active = true;
            wls_blink_arm(&hs_rgb_indicators[i].blink, interval, times, timer_read32());
            hs_rgb_indicators[i].index    = index;
            hs_rgb_indicators[i].rgb      = rgb;
            hs_rgb_indicators[i].blink_cb = (index != 0xFF) ? NULL : nkr_indicators_hook;
            break;
        }
    }
}

void rgb_matrix_hs_set_remain_time(uint8_t index, uint8_t remain_time) {
    (void)remain_time;
    for (int i = 0; i < HS_RGB_INDICATOR_COUNT; i++) {
        if (hs_rgb_indicators[i].index == index) {
            hs_rgb_indicators[i].blink.remaining   = 0;
            hs_rgb_indicators[i].blink.interval_ms = 0;
            hs_rgb_indicators[i].active            = false;
            break;
        }
    }
}

void rgb_matrix_hs_indicator(void) {
    for (int i = 0; i < HS_RGB_INDICATOR_COUNT; i++) {
        if (!hs_rgb_indicators[i].active) {
            continue;
        }

        wls_blink_out_t out = wls_blink_tick(&hs_rgb_indicators[i].blink, timer_read32());

        if (out.kind == WLS_BLINK_DONE) {
            hs_rgb_indicators[i].active = false;
            if (hs_rgb_indicators[i].blink_cb != NULL) {
                hs_rgb_indicators[i].blink_cb(i);
            }
            continue;
        }

        uint8_t r = out.on ? hs_rgb_indicators[i].rgb.r : 0x00;
        uint8_t g = out.on ? hs_rgb_indicators[i].rgb.g : 0x00;
        uint8_t b = out.on ? hs_rgb_indicators[i].rgb.b : 0x00;
        if (hs_rgb_indicators[i].index == 0xFF) {
            rgb_matrix_set_color_all(r, g, b);
        } else {
            rgb_matrix_set_color(hs_rgb_indicators[i].index, r, g, b);
        }
    }
}

bool rgb_matrix_indicators_advanced_kb(uint8_t led_min, uint8_t led_max) {
#ifdef RGBLIGHT_ENABLE
    if (rgb_matrix_indicators_advanced_user(led_min, led_max) != true) {
        return false;
    }
#else
    (void)led_min;
    (void)led_max;
#endif

    if (ee_clr_timer && timer_elapsed32(ee_clr_timer) > 3000) {
        hs_reset_settings(WLS_RESET_CAUSE_LOCAL);
        ee_clr_timer = 0;
    }

    if (host_keyboard_led_state().caps_lock) rgb_matrix_set_color(HS_RGB_INDEX_CAPS, 0x20, 0x20, 0x20);

    if (!keymap_is_mac_system() && keymap_config.no_gui) rgb_matrix_set_color(HS_RGB_INDEX_WIN_LOCK, 0x20, 0x20, 0x20);

#ifdef RGBLIGHT_ENABLE
    if (rgb_matrix_indicators_advanced_rgblight(led_min, led_max) != true) {
        return false;
    }
#endif

#ifdef WIRELESS_ENABLE
    rgb_matrix_wls_indicator();

    /* While suppressed (post-reset) the battery indicator is hidden; the
     * suppression is a deadline, so it simply expires — no re-arm, no flag to
     * flip back. Otherwise the indicator is shown and its own bounded blink
     * (rgb_matrix_hs_bat) drives the cadence. */
    if (timer_read32() >= bat_suppress_until && !rgbrec_is_started()) {
        rgb_matrix_hs_bat();
        bat_indicators();
    }

#endif

    rgb_matrix_hs_indicator();
    if (confinfo.filp) rgb_matrix_set_color(32, RGB_MATRIX_MAXIMUM_BRIGHTNESS, RGB_MATRIX_MAXIMUM_BRIGHTNESS, RGB_MATRIX_MAXIMUM_BRIGHTNESS);
    query();
    return true;
}

/* Execute the reset plan. The *policy* (which consequences, in what order,
 * and the one thing the cause changes) is the pure `wls_reset_plan_of()`
 * (wls/reset_logic); this function only performs the steps it names. Two
 * unrelated triggers reach it -- the `EE_CLR` hold timer (LOCAL) and the split
 * slave handler (REMOTE); the cause is the whole reason that handler no longer
 * needs to call back into the lifecycle module. */
void hs_reset_settings(wls_reset_cause_t cause) {
    wls_reset_plan_t plan = wls_reset_plan_of(cause);

    for (uint8_t i = 0; i < plan.count; i++) {
        switch (plan.steps[i]) {
            case WLS_RESET_STEP_NOTIFY_PEER:
                if (is_keyboard_master()) {
                    if (wls_split_send(WLS_SPLIT_CMD_RESET_SETTINGS, NULL)) {
                        dprintf("Slave Sleep OK\n");
                    } else {
                        dprintf("Slave sync failed!\n");
                    }
                }
                break;
            case WLS_RESET_STEP_WIPE_CONFIG:
                bat_suppress_until = timer_read32() + WLS_BAT_SUPPRESS_MS;
                eeconfig_init();
                eeconfig_update_rgb_matrix_default();

#ifdef RGBLIGHT_ENABLE
                extern void rgblight_init(void);
                is_rgblight_initialized = false;
                rgblight_init();
                eeconfig_update_rgblight_default();
                rgblight_enable();
#endif

                eeconfig_read_keymap(&keymap_config);

#if defined(NKRO_ENABLE) && defined(FORCE_NKRO)
                keymap_config.nkro = 0;
                eeconfig_update_keymap(&keymap_config);
#endif
                break;
            case WLS_RESET_STEP_FLASH:
                /* The acknowledgement flash; a refusal here ends the reset. */
                if (hs_reset_settings_user() != true) {
                    return;
                }
                break;
            case WLS_RESET_STEP_RERUN_BOOT:
                hs_link_activity();
                keyboard_post_init_kb();
                break;
        }
    }
}

void lpwr_wakeup_hook(void) {
    wls_lifecycle_lpwr_wakeup();
}

void user_sync_mms_slave_handler(uint8_t in_buflen, const void *in_data, uint8_t out_buflen, void *out_data) {
    (void)in_buflen;
    (void)out_buflen;
    const master_to_slave_t *m2s = (const master_to_slave_t *)in_data;
    slave_to_master_t       *s2m = (slave_to_master_t *)out_data;

    /* The wire is upstream's RPC; *what each command means* is the pure
     * `wls_split_decide_slave()` (wls/split_logic). This handler only applies
     * the decided action. */
    wls_split_action_t action = wls_split_decide_slave(m2s->cmd, m2s->body);

    switch (action.kind) {
        case WLS_SPLIT_ACT_SYNC_DEVICE_INDEX:
            wireless_devs_change(action.last_devs, action.now_devs, false);
            s2m->resp = 0x00;
            break;
        case WLS_SPLIT_ACT_ORDER_SLEEP:
            if (wireless_get_current_devs() != DEVS_USB) {
                palSetLineMode(SERIAL_USART_RX_PIN, PAL_OUTPUT_TYPE_OPENDRAIN);
                palSetLineMode(SERIAL_USART_TX_PIN, PAL_OUTPUT_TYPE_OPENDRAIN);
            }
            s2m->resp = 0x00;
            wls_order_sleep(action.sleep_flag);
            lpwr_set_timeout_manual(true);
            break;
        case WLS_SPLIT_ACT_REAPPLY_RAIL:
            wls_led_rail_apply();
            s2m->resp = 0x00;
            break;
        case WLS_SPLIT_ACT_RESET_SETTINGS:
            hs_reset_settings(WLS_RESET_CAUSE_REMOTE);
            s2m->resp = 0x00;
            break;
        case WLS_SPLIT_ACT_NONE:
        default:
            /* The master-suspend command (0xBB) lands here deliberately: the
             * slave must not force its own rail off (defect 5). An unknown
             * command also lands here -- stay silent, act on nothing. */
            break;
    }
}