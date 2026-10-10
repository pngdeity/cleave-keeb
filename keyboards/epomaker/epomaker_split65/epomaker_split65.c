// Copyright 2024 yangzheng20003 (@yangzheng20003)
// SPDX-License-Identifier: GPL-2.0-or-later

#include QMK_KEYBOARD_H
#include "wls/wls.h"
#include "wls/transport_logic.h"
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

typedef union {
    uint32_t raw;
    struct {
        uint8_t flag : 1;
        uint8_t devs : 3;
        uint8_t record_channel : 4;
        uint8_t record_last_mode;
        uint8_t last_btdevs : 3;
        uint8_t dir_flag : 1;
        uint8_t filp : 1;
        uint8_t last_wireless_devs : 3;
        uint8_t version : 8;
    };
} confinfo_t;
confinfo_t confinfo;

/* Bump when a field's meaning or position changes. An EEPROM block written by
 * an older firmware then fails the check below and is re-defaulted once, which
 * is deterministic where a per-field heuristic is not. */
#define CONFINFO_VERSION 1

typedef struct {
    bool     active;
    uint32_t timer;
    uint32_t interval;
    uint32_t times;
    uint8_t  index;
    RGB      rgb;
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
void hs_reset_settings(void);
void rgb_matrix_hs_indicator(void);
void rgb_matrix_hs_indicator_set(uint8_t index, RGB rgb, uint32_t interval, uint8_t times);
void rgb_matrix_hs_set_remain_time(uint8_t index, uint8_t remain_time);

#define keymap_is_mac_system() ((get_highest_layer(default_layer_state) == _MBL) || (get_highest_layer(default_layer_state) == _MFL))
#define keymap_is_base_layer() ((get_highest_layer(default_layer_state) == _BL) || (get_highest_layer(default_layer_state) == _FL))

uint32_t        post_init_timer       = 0x00;
bool            mac_status            = false;
bool            charging_state        = false;
bool            bat_full_flag         = false;
bool            enable_bat_indicators = true;
uint32_t        bat_indicator_cnt     = 0;
static uint32_t ee_clr_timer          = 0;
bool            no_record_fg;
uint8_t         pov;
static bool     im_bat_req_charging_flag = false;
uint8_t         buff[]                   = {14, 8, 2, 1, 1, 1, 1, 1, 1, 1, 0};

void usart_init(void) {
    // palSetLineMode(SERIAL_USART_TX_PIN, PAL_MODE_ALTERNATE(SERIAL_USART_TX_PAL_MODE) | PAL_OUTPUT_TYPE_OPENDRAIN);
    palSetLineMode(SERIAL_USART_TX_PIN, PAL_MODE_ALTERNATE(SERIAL_USART_TX_PAL_MODE) | PAL_OUTPUT_TYPE_PUSHPULL | PAL_OUTPUT_SPEED_HIGHEST);
    palSetLineMode(SERIAL_USART_RX_PIN, PAL_MODE_ALTERNATE(SERIAL_USART_RX_PAL_MODE) | PAL_OUTPUT_TYPE_PUSHPULL | PAL_OUTPUT_SPEED_HIGHEST);
}

void eeconfig_confinfo_update(uint32_t raw) {
    eeconfig_update_kb(raw);
}

typedef struct _master_to_slave_t {
    uint8_t cmd;
    uint8_t body[4];
} master_to_slave_t;

typedef struct _slave_to_master_t {
    uint8_t resp;
    uint8_t body[4];
} slave_to_master_t;

uint32_t eeconfig_confinfo_read(void) {
    return eeconfig_read_kb();
}

void eeconfig_confinfo_default(void) {
    confinfo.flag               = true;
    confinfo.record_channel     = 0;
    confinfo.record_last_mode   = 0xff;
    confinfo.last_btdevs        = 1;
    confinfo.dir_flag           = 0;
    confinfo.last_wireless_devs = DEVS_BT1;
    confinfo.version            = CONFINFO_VERSION;

    // #ifdef WIRELESS_ENABLE
    //     confinfo.devs = DEVS_USB;
    // #endif

    eeconfig_init_user_datablock();
    eeconfig_confinfo_update(confinfo.raw);

#ifdef RGBLIGHT_ENABLE
    rgblight_mode(buff[0]);
#endif
}

void master_sync_mms_slave(uint8_t last_mode, uint8_t now_mode, uint8_t reset) {
    master_to_slave_t m2s = {0};
    slave_to_master_t s2m = {0};
    m2s.cmd               = 0x55;
    m2s.body[0]           = last_mode;
    m2s.body[1]           = now_mode;
    m2s.body[2]           = reset;
    if (transaction_rpc_exec(USER_SYNC_MMS, sizeof(m2s), &m2s, sizeof(s2m), &s2m)) {
        if (s2m.resp == 0x00)
            ;
        dprintf("Slave Sleep OK1\n");
    } else {
        dprintf("Slave sync failed1!\n");
    }
}

void eeconfig_confinfo_init(void) {
    confinfo.raw = eeconfig_confinfo_read();
    /* A blank block, or one written by older firmware (different layout), is
     * re-defaulted. The version gate makes the migration deterministic instead
     * of guessing per field. */
    if (!confinfo.raw || confinfo.version != CONFINFO_VERSION) {
        eeconfig_confinfo_default();
    }
}

void keyboard_post_init_kb(void) {
#ifdef CONSOLE_ENABLE
    debug_enable = true;
#endif
    eeconfig_confinfo_init();

#ifdef LED_POWER_EN_PIN
    gpio_set_pin_output(LED_POWER_EN_PIN);
#endif

#ifdef LED_POWER_EN2_PIN
    gpio_set_pin_output(LED_POWER_EN2_PIN);
#endif

    /* Rail starts from its single owner (defect 5). */
    wls_led_rail_apply();

#ifdef HS_BT_DEF_PIN
    gpio_set_pin_input_high(HS_BT_DEF_PIN);
#endif

#ifdef HS_2G4_DEF_PIN
    gpio_set_pin_input_high(HS_2G4_DEF_PIN);
#endif

#ifdef USB_POWER_EN_PIN
    gpio_write_pin_low(USB_POWER_EN_PIN);
    gpio_set_pin_output(USB_POWER_EN_PIN);
#endif

#ifdef HS_BAT_CABLE_PIN
    gpio_set_pin_input(HS_BAT_CABLE_PIN);
#endif

#ifdef BAT_FULL_PIN
    gpio_set_pin_input_high(BAT_FULL_PIN);
#endif

#ifdef WIRELESS_ENABLE
    wireless_init();
#    if (!(defined(HS_BT_DEF_PIN) && defined(HS_2G4_DEF_PIN)))
    wireless_devs_change(!confinfo.devs, confinfo.devs, false);
#    endif

    /* Apply the physical switch LEVEL to the connection host here, in post-init
     * — after upstream's `connection_init()` has read EEPROM but before the
     * first `keyboard_task()`/`host_task()` in the main loop. Without this the
     * first `host_task()` resolves `config.desired_host` from stale EEPROM
     * before the switch is read, and (with the vendor `set_transport()` swap
     * gone, item 14d) the board would route a report through a driver whose
     * module is not initialized. This is the same switch-authority seed as the
     * 100 ms pass in `wireless_post_task()`, moved earlier so no report can
     * precede it; that pass remains for boards/branches we do not reach here. */
#    if defined(HS_BT_DEF_PIN) && defined(HS_2G4_DEF_PIN)
    {
        uint8_t           boot_devs = hs_mode_switch_devs((confinfo.devs >= DEVS_BT1 && confinfo.devs <= DEVS_BT5) ? confinfo.devs : confinfo.last_btdevs);
        connection_host_t boot_host = (boot_devs == DEVS_USB) ? CONNECTION_HOST_USB : (boot_devs == DEVS_2G4) ? CONNECTION_HOST_2P4GHZ : CONNECTION_HOST_BLUETOOTH;
        connection_set_host_noeeprom(boot_host);
        /* Drive the USB data line from the switch level directly. When the
         * EEPROM host already equals `boot_host`, `connection_set_host_noeeprom`
         * early-returns without firing the hook, so the line would keep its
         * reset default (line above connects it unconditionally) on a non-USB
         * host. Setting it here removes that dependence on the hook firing. */
        wls_usb_connect(boot_host == CONNECTION_HOST_USB);
    }
#    endif
    post_init_timer = timer_read32();
#endif

    keyboard_post_init_user();

    rgbrec_init(confinfo.record_channel);

    pov = *md_getp_bat();
    // usart_init();
    transaction_register_rpc(USER_SYNC_MMS, user_sync_mms_slave_handler);
}

#ifdef WIRELESS_ENABLE

void usb_power_connect(void) {
#    ifdef USB_POWER_EN_PIN
    gpio_write_pin_low(USB_POWER_EN_PIN);
#    endif
}

void usb_power_disconnect(void) {
#    ifdef USB_POWER_EN_PIN
    gpio_write_pin_high(USB_POWER_EN_PIN);
#    endif
}

/* The USB data-line connect/disconnect primitive. Formerly the vendor
 * `set_transport()` drove this by swapping `host_driver_t`; with that swap gone
 * (item 14d) the board owns it directly. The driver choice is upstream's
 * now — `host_get_active_driver()` — so nothing here touches a driver pointer.
 *
 * `last_suspend_state` is re-asserted on connect because the flag is not
 * reliably set after a bus restart. */
void wls_usb_connect(bool enable) {
    extern bool last_suspend_state;

    if (enable) {
        if (!usb_connected_state()) {
            last_suspend_state = true;
#    if !defined(KEEP_USB_CONNECTION_IN_WIRELESS_MODE)
            usb_power_connect();
            restart_usb_driver(&USBD1);
#    endif
        }
    } else {
#    if !defined(KEEP_USB_CONNECTION_IN_WIRELESS_MODE)
        if (USB_DRIVER.state != USB_STOP) {
            usbDisconnectBus(&USBD1);
            usbStop(&USBD1);
            usb_power_disconnect();
        }
#    endif
    }
}

void suspend_power_down_kb(void) {
#    ifdef LED_POWER_EN_PIN
    gpio_write_pin_low(LED_POWER_EN_PIN);
#    endif

#    ifdef LED_POWER_EN2_PIN
    gpio_write_pin_low(LED_POWER_EN2_PIN);
#    endif

    suspend_power_down_user();
}

void suspend_wakeup_init_kb(void) {
    /* Rail from its single owner (defect 5). */
    wls_led_rail_apply();

    wireless_devs_change(wireless_get_current_devs(), wireless_get_current_devs(), false);
    suspend_wakeup_init_user();
    hs_link_activity();
}

void suspend_wakeup_init_user(void) {
    usart_init();

    /* The 0xCC/0xBB pair relay LED-rail power to the slave, so they only make
     * sense on battery. While cabled the rail must stay up on both halves;
     * broadcasting it would extinguish the slave's backlight. */
    if (wireless_get_current_devs() == DEVS_USB) {
        return;
    }

    master_to_slave_t m2s = {0};
    slave_to_master_t s2m = {0};
    m2s.cmd               = 0xCC;
    if (transaction_rpc_exec(USER_SYNC_MMS, sizeof(m2s), &m2s, sizeof(s2m), &s2m)) {
        if (s2m.resp == 0x00) {
        }
        dprintf("Slave Sleep OK\n");
    } else {
        dprint("Slave sync failed!\n");
    }
}

void suspend_power_down_user(void) {
    if (wireless_get_current_devs() == DEVS_USB) {
        return;
    }

    master_to_slave_t m2s = {0};
    slave_to_master_t s2m = {0};
    m2s.cmd               = 0xBB;
    if (transaction_rpc_exec(USER_SYNC_MMS, sizeof(m2s), &m2s, sizeof(s2m), &s2m)) {
        if (s2m.resp == 0x00) {
        }
        dprintf("Slave Sleep OK\n");
    } else {
        dprint("Slave sync failed!\n");
    }
}

bool lpwr_is_allow_timeout_hook(void) {
    /* A half may only fall into the timeout path when it is the master and is
     * not on USB. Whether the resulting stop is one this board may take is
     * decided once, at the point of commitment: the sleep-policy contract
     * (`lpwr_stop_is_allowed()`, enforced in the shared lowpower.c) refuses an
     * unordered stop there, so the rule lives in one place rather than at each
     * entry. */
    if (!is_keyboard_master()) {
        return false;
    }

    if (wireless_get_current_devs() == DEVS_USB) {
        return false;
    }

    return true;
}

bool lpwr_is_allow_presleep_hook(void) {
    extern bool charging_state;

    if (is_keyboard_master()) {
        master_to_slave_t m2s = {0};
        slave_to_master_t s2m = {0};
        m2s.cmd               = 0xAA;
        m2s.body[0]           = wls_sleep_ordered();
        if (transaction_rpc_exec(USER_SYNC_MMS, sizeof(m2s), &m2s, sizeof(s2m), &s2m)) {
            if (s2m.resp == 0x00) {
            }
            dprintf("Slave Sleep OK\n");
        } else {
            dprint("Slave sync failed!\n");
        }

        if (wireless_get_current_devs() != DEVS_USB) {
            palSetLineMode(SERIAL_USART_RX_PIN, PAL_OUTPUT_TYPE_OPENDRAIN);
            palSetLineMode(SERIAL_USART_TX_PIN, PAL_OUTPUT_TYPE_OPENDRAIN);
        }
    }

    if ((wireless_get_current_devs() == DEVS_USB) && (!charging_state)) {
        if (USB_DRIVER.state != USB_STOP) {
            usb_power_disconnect();
            usbDisconnectBus(&USBD1);
            usbStop(&USBD1);
        }
    }
    return true;
}

void wireless_post_task(void) {
    // auto switching devs
    if (post_init_timer && timer_elapsed32(post_init_timer) >= 100) {
        md_send_devctrl(MD_SND_CMD_DEVCTRL_FW_VERSION);   // get the module fw version.
        md_send_devctrl(MD_SND_CMD_DEVCTRL_SLEEP_BT_EN);  // timeout 30min to sleep in bt mode, enable
        md_send_devctrl(MD_SND_CMD_DEVCTRL_SLEEP_2G4_EN); // timeout 30min to sleep in 2.4g mode, enable
        /* The physical switch is authoritative at boot. Restoring the persisted
         * index blindly let a stale wireless selection survive a reboot with the
         * switch on USB — the board came up on a dead wireless link while USB
         * stayed enumerated ("enumerated but no keys"; defect 4). Derive the
         * index from the switch LEVEL here; persistence still supplies the
         * BT1..5 sub-selection the switch cannot express. */
        uint8_t boot_devs = hs_mode_switch_devs((confinfo.devs >= DEVS_BT1 && confinfo.devs <= DEVS_BT5) ? confinfo.devs : confinfo.last_btdevs);
        /* Two independent selectors pick the sender: the vendor device index
         * (above) and upstream's `config.desired_host`, which resolves
         * `host_get_active_driver()` to `bt_driver` when it is BLUETOOTH. Both
         * must be set from the same switch read, or the board routes the vendor
         * link correctly but sends HID through the wrong driver (split brain).
         *
         * This is a switch-driven change, so it must NOT persist: the physical
         * switch owns the position and is re-read every boot, and only a user
         * keycode may write the stored choice. `connection_set_host()` always
         * commits to EEPROM; `_noeeprom` is the seam that keeps the switch from
         * overwriting the user's stored intent (14c). */
        connection_host_t boot_host = (boot_devs == DEVS_USB) ? CONNECTION_HOST_USB : (boot_devs == DEVS_2G4) ? CONNECTION_HOST_2P4GHZ : CONNECTION_HOST_BLUETOOTH;
        connection_set_host_noeeprom(boot_host);
        wireless_devs_change(!confinfo.devs, boot_devs, false);
        post_init_timer = 0x00;
    }
#    if defined(HS_BT_DEF_PIN) && defined(HS_2G4_DEF_PIN)
    hs_mode_scan(false, hsm_mode_seed(wireless_get_current_devs()), hsm_seed_btdev(wireless_get_current_devs(), confinfo.last_btdevs));
#    endif

    kb_battery_push_task();
}

uint32_t wls_process_long_press(uint32_t trigger_time, void *cb_arg) {
    /* The deferred call carries the *resolved* target device, written once when
     * the timer was armed. It must not point at shared state: `defer_exec` stores
     * the pointer, so a pointer into a per-keypress variable would be rewritten
     * by any later keypress in the 3 s window and could re-pair the wrong
     * profile (audit fix 1). */
    uint8_t devs = (uint8_t)(uintptr_t)cb_arg;

    if (devs >= DEVS_BT1 && devs <= DEVS_BT5) {
        uint8_t mode = hsm_mode_seed(wireless_get_current_devs());
        hs_modeio_detection(true, &mode, hsm_seed_btdev(wireless_get_current_devs(), confinfo.last_btdevs));
        if ((mode == hs_bt) || (mode == hs_wireless) || (mode == hs_none)) {
            /* Re-pair the profile the driver already owns (14b): select it with
             * reset=true so the module gets CLEAN + devinfo + PAIR. */
            bluetooth_select_profile(devs, true);
            wls_indicate_devs(devs, true);
        }
    } else if (devs == DEVS_2G4) {
        uint8_t mode = hsm_mode_seed(wireless_get_current_devs());
        hs_modeio_detection(true, &mode, hsm_seed_btdev(wireless_get_current_devs(), confinfo.last_btdevs));
        if ((mode == hs_2g4) || (mode == hs_wireless) || (mode == hs_none)) {
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

#    define WLS_KEYCODE_EXEC(wls_dev)                                                                                                                                  \
        do {                                                                                                                                                           \
            if (record->event.pressed) {                                                                                                                               \
                /* Route through upstream's connection subsystem so it owns the                                                                                        \
                 * persisted host choice; connection_host_changed_kb() then                                                                                            \
                 * maps it onto the vendor device index and transport. This is the                                                                                     \
                 * USER-intent path, so it persists (`connection_set_host`), unlike                                                                                    \
                 * the switch read at boot which is `_noeeprom` (14c). */                                                                                              \
                connection_host_t wls_host = (wls_dev) == DEVS_USB ? CONNECTION_HOST_USB : (wls_dev) == DEVS_2G4 ? CONNECTION_HOST_2P4GHZ : CONNECTION_HOST_BLUETOOTH; \
                connection_set_host(wls_host);                                                                                                                         \
                /* BT1..BT5 is the driver's sub-index, not a transport: tell the                                                                                       \
                 * driver which profile so it owns the choice (14b). A short tap                                                                                       \
                 * selects (reset=false); the long-press re-pairs (reset=true). */                                                                                     \
                if ((wls_dev) >= DEVS_BT1 && (wls_dev) <= DEVS_BT5) {                                                                                                  \
                    bluetooth_select_profile(wls_dev, false);                                                                                                          \
                }                                                                                                                                                      \
                wls_persist_devs();                                                                                                                                    \
                wls_indicate_devs((wls_dev), false);                                                                                                                   \
                if (wls_process_long_press_token == INVALID_DEFERRED_TOKEN) {                                                                                          \
                    /* Capture the resolved target by value, not a pointer into                                                                                        \
                     * per-keypress state (audit fix 1). */                                                                                                            \
                    wls_process_long_press_token = defer_exec(WLS_KEYCODE_PAIR_TIME, wls_process_long_press, (void *)(uintptr_t)hsm_long_press_devs(keycode));         \
                }                                                                                                                                                      \
            } else {                                                                                                                                                   \
                cancel_deferred_exec(wls_process_long_press_token);                                                                                                    \
                wls_process_long_press_token = INVALID_DEFERRED_TOKEN;                                                                                                 \
            }                                                                                                                                                          \
        } while (false)

    switch (keycode) {
        case KC_BT1: {
            uint8_t mode = hsm_mode_seed(wireless_get_current_devs());
            hs_modeio_detection(true, &mode, hsm_seed_btdev(wireless_get_current_devs(), confinfo.last_btdevs));
            if ((mode == hs_bt) || (mode == hs_wireless) || (mode == hs_none)) {
                WLS_KEYCODE_EXEC(DEVS_BT1);
                hs_link_activity();
            }

        } break;
        case KC_BT2: {
            uint8_t mode = hsm_mode_seed(wireless_get_current_devs());
            hs_modeio_detection(true, &mode, hsm_seed_btdev(wireless_get_current_devs(), confinfo.last_btdevs));
            if ((mode == hs_bt) || (mode == hs_wireless) || (mode == hs_none)) {
                WLS_KEYCODE_EXEC(DEVS_BT2);
                hs_link_activity();
            }
        } break;
        case KC_BT3: {
            uint8_t mode = hsm_mode_seed(wireless_get_current_devs());
            hs_modeio_detection(true, &mode, hsm_seed_btdev(wireless_get_current_devs(), confinfo.last_btdevs));
            if ((mode == hs_bt) || (mode == hs_wireless) || (mode == hs_none)) {
                WLS_KEYCODE_EXEC(DEVS_BT3);
                hs_link_activity();
            }
        } break;
        case KC_2G4: {
            uint8_t mode = hsm_mode_seed(wireless_get_current_devs());
            hs_modeio_detection(true, &mode, hsm_seed_btdev(wireless_get_current_devs(), confinfo.last_btdevs));
            if ((mode == hs_2g4) || (mode == hs_wireless) || (mode == hs_none)) {
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

uint32_t hs_ct_time;
RGB      rgb_test_open;
bool     process_record_kb(uint16_t keycode, keyrecord_t *record) {
    if (process_record_user(keycode, record) != true) {
        return false;
    }

#ifdef WIRELESS_ENABLE
    if (process_record_wls(keycode, record) != true) {
        return false;
    }
#endif
    switch (keycode) {
        case MOR_1: {
            if (record->event.pressed) {
                register_code(KC_LCTL);
                register_code(KC_Z);
            } else {
                unregister_code(KC_LCTL);
                unregister_code(KC_Z);
            }
        } break;
        case MOR_2: {
            if (record->event.pressed) {
                register_code(KC_LCTL);
                register_code(KC_X);
            } else {
                unregister_code(KC_LCTL);
                unregister_code(KC_X);
            }
        } break;
        case MOR_3: {
            if (record->event.pressed) {
                register_code(KC_LCTL);
                register_code(KC_C);
            } else {
                unregister_code(KC_LCTL);
                unregister_code(KC_C);
            }
        } break;
        case MOR_4: {
            if (record->event.pressed) {
                register_code(KC_LCTL);
                register_code(KC_V);
            } else {
                unregister_code(KC_LCTL);
                unregister_code(KC_V);
            }
        } break;
        case KC_F1: {
            if (confinfo.filp) {
                if (keymap_is_mac_system()) {
                    if (record->event.pressed) {
                        register_code16(KC_MSEL);
                    } else {
                        unregister_code16(KC_MSEL);
                    }
                    return false;
                }
            }
            return true;
        } break;
        case KC_F2: {
            if (confinfo.filp) {
                if (keymap_is_mac_system()) {
                    if (record->event.pressed) {
                        register_code16(KC_VOLD);
                    } else {
                        unregister_code16(KC_VOLD);
                    }
                    return false;
                }
            }
            return true;
        } break;
        case KC_F3: {
            if (confinfo.filp) {
                if (keymap_is_mac_system()) {
                    if (record->event.pressed) {
                        register_code16(KC_VOLU);
                    } else {
                        unregister_code16(KC_VOLU);
                    }
                    return false;
                }
            }
            return true;
        } break;
        case KC_F4: {
            if (confinfo.filp) {
                if (keymap_is_mac_system()) {
                    if (record->event.pressed) {
                        register_code16(KC_MUTE);
                    } else {
                        unregister_code16(KC_MUTE);
                    }
                    return false;
                }
            }
            return true;
        } break;
        case KC_F5: {
            if (confinfo.filp) {
                if (keymap_is_mac_system()) {
                    if (record->event.pressed) {
                        register_code16(KC_MSTP);
                    } else {
                        unregister_code16(KC_MSTP);
                    }
                    return false;
                }
            }
            return true;
        } break;
        case KC_F6: {
            if (confinfo.filp) {
                if (keymap_is_mac_system()) {
                    if (record->event.pressed) {
                        register_code16(KC_MPRV);
                    } else {
                        unregister_code16(KC_MPRV);
                    }
                    return false;
                }
            }
            return true;
        } break;
        case KC_F7: {
            if (confinfo.filp) {
                if (keymap_is_mac_system()) {
                    if (record->event.pressed) {
                        register_code16(KC_MPLY);
                    } else {
                        unregister_code16(KC_MPLY);
                    }
                    return false;
                }
            }
            return true;
        } break;
        case KC_F8: {
            if (confinfo.filp) {
                if (keymap_is_mac_system()) {
                    if (record->event.pressed) {
                        register_code16(KC_MNXT);
                    } else {
                        unregister_code16(KC_MNXT);
                    }
                    return false;
                }
            }
            return true;
        } break;
        case KC_F9: {
            if (confinfo.filp) {
                if (keymap_is_mac_system()) {
                    if (record->event.pressed) {
                        register_code16(KC_MAIL);
                    } else {
                        unregister_code16(KC_MAIL);
                    }
                    return false;
                }
            }
            return true;
        } break;
        case KC_F10: {
            if (confinfo.filp) {
                if (keymap_is_mac_system()) {
                    if (record->event.pressed) {
                        register_code16(KC_WHOM);
                    } else {
                        unregister_code16(KC_WHOM);
                    }
                    return false;
                }
            }
            return true;
        } break;
        case KC_F11: {
            if (confinfo.filp) {
                if (keymap_is_mac_system()) {
                    if (record->event.pressed) {
                        register_code16(KC_CALC);
                    } else {
                        unregister_code16(KC_CALC);
                    }
                    return false;
                }
            }
            return true;
        } break;
        case KC_F12: {
            if (confinfo.filp) {
                if (keymap_is_mac_system()) {
                    if (record->event.pressed) {
                        register_code16(KC_WSCH);
                    } else {
                        unregister_code16(KC_WSCH);
                    }
                    return false;
                }
            }
            return true;
        } break;
        case KC_1: {
            if (confinfo.filp) {
                if (!keymap_is_mac_system()) {
                    if (record->event.pressed) {
                        register_code(KC_F1);
                    } else {
                        unregister_code(KC_F1);
                    }
                } else {
                    if (record->event.pressed) {
                        register_code(KC_BRID);
                    } else {
                        unregister_code(KC_BRID);
                    }
                }
                return false;
            }
            return true;
        } break;
        case KC_2: {
            if (confinfo.filp) {
                if (!keymap_is_mac_system()) {
                    if (record->event.pressed) {
                        register_code(KC_F2);
                    } else {
                        unregister_code(KC_F2);
                    }
                } else {
                    if (record->event.pressed) {
                        register_code(KC_BRIU);
                    } else {
                        unregister_code(KC_BRIU);
                    }
                }
                return false;
            }
            return true;
        } break;
        case KC_3: {
            if (confinfo.filp) {
                if (!keymap_is_mac_system()) {
                    if (record->event.pressed) {
                        register_code(KC_F3);
                    } else {
                        unregister_code(KC_F3);
                    }
                } else {
                    if (record->event.pressed) {
                        register_code(KC_LGUI);
                        register_code(KC_TAB);
                    } else {
                        unregister_code(KC_LGUI);
                        unregister_code(KC_TAB);
                    }
                }
                return false;
            }
            return true;
        } break;
        case KC_4: {
            if (confinfo.filp) {
                if (!keymap_is_mac_system()) {
                    if (record->event.pressed) {
                        register_code(KC_F4);
                    } else {
                        unregister_code(KC_F4);
                    }
                } else {
                    if (record->event.pressed) {
                        register_code(KC_LGUI);
                        register_code(KC_E);
                    } else {
                        unregister_code(KC_LGUI);
                        unregister_code(KC_E);
                    }
                }
                return false;
            }
            return true;
        } break;
        case KC_5: {
            if (confinfo.filp) {
                if (!keymap_is_mac_system()) {
                    if (record->event.pressed) {
                        register_code(KC_F5);
                    } else {
                        unregister_code(KC_F5);
                    }
                } else {
                    if (record->event.pressed) {
                        rgb_matrix_decrease_val();
                    }
                }
                return false;
            }
            return true;
        } break;
        case KC_6: {
            if (confinfo.filp) {
                if (!keymap_is_mac_system()) {
                    if (record->event.pressed) {
                        register_code(KC_F6);
                    } else {
                        unregister_code(KC_F6);
                    }
                } else {
                    if (record->event.pressed) {
                        rgb_matrix_increase_val();
                    }
                }
                return false;
            }
            return true;
        } break;
        case KC_7: {
            if (confinfo.filp) {
                if (!keymap_is_mac_system()) {
                    if (record->event.pressed) {
                        register_code(KC_F7);
                    } else {
                        unregister_code(KC_F7);
                    }
                } else {
                    if (record->event.pressed) {
                        register_code(KC_MPRV);
                    } else {
                        unregister_code(KC_MPRV);
                    }
                }
                return false;
            }
            return true;
        } break;
        case KC_8: {
            if (confinfo.filp) {
                if (!keymap_is_mac_system()) {
                    if (record->event.pressed) {
                        register_code(KC_F8);
                    } else {
                        unregister_code(KC_F8);
                    }
                } else {
                    if (record->event.pressed) {
                        register_code(KC_MPLY);
                    } else {
                        unregister_code(KC_MPLY);
                    }
                }
                return false;
            }
            return true;
        } break;
        case KC_9: {
            if (confinfo.filp) {
                if (!keymap_is_mac_system()) {
                    if (record->event.pressed) {
                        register_code(KC_F9);
                    } else {
                        unregister_code(KC_F9);
                    }
                } else {
                    if (record->event.pressed) {
                        register_code(KC_MNXT);
                    } else {
                        unregister_code(KC_MNXT);
                    }
                }
                return false;
            }
            return true;
        } break;
        case KC_0: {
            if (confinfo.filp) {
                if (!keymap_is_mac_system()) {
                    if (record->event.pressed) {
                        register_code(KC_F10);
                    } else {
                        unregister_code(KC_F10);
                    }
                } else {
                    if (record->event.pressed) {
                        register_code(KC_MUTE);
                    } else {
                        unregister_code(KC_MUTE);
                    }
                }
                return false;
            }
            return true;
        } break;
        case KC_MINS: {
            if (confinfo.filp) {
                if (!keymap_is_mac_system()) {
                    if (record->event.pressed) {
                        register_code(KC_F11);
                    } else {
                        unregister_code(KC_F11);
                    }
                } else {
                    if (record->event.pressed) {
                        register_code(KC_VOLD);
                    } else {
                        unregister_code(KC_VOLD);
                    }
                }
                return false;
            }
            return true;
        } break;
        case KC_EQL: {
            if (confinfo.filp) {
                if (!keymap_is_mac_system()) {
                    if (record->event.pressed) {
                        register_code(KC_F12);
                    } else {
                        unregister_code(KC_F12);
                    }
                } else {
                    if (record->event.pressed) {
                        register_code(KC_VOLU);
                    } else {
                        unregister_code(KC_VOLU);
                    }
                }
                return false;
            }
            return true;
        } break;
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
    uint8_t         hs_now_mode;
    static uint32_t hs_current_time;

    charging_state = gpio_read_pin(HS_BAT_CABLE_PIN);

    bat_full_flag = gpio_read_pin(BAT_FULL_PIN);

    /* Single owner for the LED rail: re-derive A5/A8 from the current RGB value
     * on every half. See `wls_led_rail_apply()`. */
    wls_led_rail_apply();

    if (charging_state && (bat_full_flag)) {
        hs_now_mode = MD_SND_CMD_DEVCTRL_CHARGING_DONE;
    } else if (charging_state) {
        hs_now_mode = MD_SND_CMD_DEVCTRL_CHARGING;
    } else {
        hs_now_mode = MD_SND_CMD_DEVCTRL_CHARGING_STOP;
    }

    if (!hs_current_time || timer_elapsed32(hs_current_time) > 1000) {
        hs_current_time = timer_read32();
        md_send_devctrl(hs_now_mode);
        md_send_devctrl(MD_SND_CMD_DEVCTRL_INQVOL);
    }

    if (is_keyboard_master()) {
        static uint32_t last_sync = 0;
        if (timer_elapsed32(last_sync) > 2000) {
            last_sync = timer_read32();
            pov       = *md_getp_bat();
            master_sync_mms_slave(wireless_get_current_devs(), wireless_get_current_devs(), pov);
        }
    }
}

#ifdef RGB_MATRIX_ENABLE

#    ifdef WIRELESS_ENABLE
bool     wls_rgb_indicator_reset    = false;
uint32_t wls_rgb_indicator_timer    = 0x00;
uint32_t wls_rgb_indicator_interval = 0;
uint32_t wls_rgb_indicator_times    = 0;
uint32_t wls_rgb_indicator_index    = 0;
RGB      wls_rgb_indicator_rgb      = {0};

void rgb_matrix_wls_indicator_set(uint8_t index, RGB rgb, uint32_t interval, uint8_t times) {
    wls_rgb_indicator_timer = timer_read32();

    wls_rgb_indicator_index    = index;
    wls_rgb_indicator_interval = interval;
    wls_rgb_indicator_times    = times * 2;
    wls_rgb_indicator_rgb      = rgb;
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
    wls_rgb_indicator_reset = reset;

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

bool rgb_matrix_wls_indicator_cb(void) {
    if (*md_getp_state() != MD_STATE_CONNECTED) {
        /* Re-arm the same flash (do not re-run a "change") while the module is
         * still not connected: a pairing-attempt confirmation repeats until it
         * links, which is the indicator's own repeat policy, not a transport
         * change. */
        wls_indicate_devs(wireless_get_current_devs(), wls_rgb_indicator_reset);
        return true;
    }

    // refresh led
    led_wakeup();

    return false;
}

void rgb_matrix_wls_indicator(void) {
    if (wls_rgb_indicator_timer) {
        if (timer_elapsed32(wls_rgb_indicator_timer) >= wls_rgb_indicator_interval) {
            wls_rgb_indicator_timer = timer_read32();

            if (wls_rgb_indicator_times) {
                wls_rgb_indicator_times--;
            }

            if (wls_rgb_indicator_times <= 0) {
                wls_rgb_indicator_timer = 0x00;
                if (rgb_matrix_wls_indicator_cb() != true) {
                    return;
                }
            }
        }

        if (wls_rgb_indicator_times % 2) {
            rgb_matrix_set_color(wls_rgb_indicator_index, wls_rgb_indicator_rgb.r, wls_rgb_indicator_rgb.g, wls_rgb_indicator_rgb.b);
        } else {
            rgb_matrix_set_color(wls_rgb_indicator_index, 0x00, 0x00, 0x00);
        }
    }
}

void rgb_matrix_hs_bat_set(uint8_t index, RGB rgb, uint32_t interval, uint8_t times) {
    for (int i = 0; i < HS_RGB_BAT_COUNT; i++) {
        if (!hs_rgb_bat[i].active) {
            hs_rgb_bat[i].active   = true;
            hs_rgb_bat[i].timer    = timer_read32();
            hs_rgb_bat[i].interval = interval;
            hs_rgb_bat[i].times    = times * 2;
            hs_rgb_bat[i].index    = index;
            hs_rgb_bat[i].rgb      = rgb;
            break;
        }
    }
}

void rgb_matrix_hs_bat(void) {
    for (int i = 0; i < HS_RGB_BAT_COUNT; i++) {
        if (hs_rgb_bat[i].active) {
            if (timer_elapsed32(hs_rgb_bat[i].timer) >= hs_rgb_bat[i].interval) {
                hs_rgb_bat[i].timer = timer_read32();

                if (hs_rgb_bat[i].times) {
                    hs_rgb_bat[i].times--;
                }

                if (hs_rgb_bat[i].times <= 0) {
                    hs_rgb_bat[i].active = false;
                    hs_rgb_bat[i].timer  = 0x00;
                }
            }

            if (hs_rgb_bat[i].times % 2) {
                rgb_matrix_set_color(hs_rgb_bat[i].index, hs_rgb_bat[i].rgb.r, hs_rgb_bat[i].rgb.g, hs_rgb_bat[i].rgb.b);
            } else {
                rgb_matrix_set_color(hs_rgb_bat[i].index, 0x00, 0x00, 0x00);
            }
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
        uint8_t r, g, b;
        if (charging_state && (bat_full_flag)) {
            r = 0x00;
            g = 0xFF;
            b = 0x00;
        } else if (charging_state) {
            r = 0x00;
            g = 0x40;
            b = 0xFF;
        } else if (bat_level >= 50) {
            r = 0x00;
            g = 0xFF;
            b = 0x00;
        } else if (bat_level >= 30) {
            r = 0xFF;
            g = 0x80;
            b = 0x00;
        } else if (bat_level > BATTERY_CAPACITY_LOW) {
            r = 0xFF;
            g = 0x00;
            b = 0x00;
        } else {
            r = 0xFF;
            g = 0x00;
            b = 0x00;
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
            hs_rgb_indicators[i].active   = true;
            hs_rgb_indicators[i].timer    = timer_read32();
            hs_rgb_indicators[i].interval = interval;
            hs_rgb_indicators[i].times    = times * 2;
            hs_rgb_indicators[i].index    = index;
            hs_rgb_indicators[i].rgb      = rgb;
            if (index != 0xFF)
                hs_rgb_indicators[i].blink_cb = NULL;
            else {
                hs_rgb_indicators[i].blink_cb = nkr_indicators_hook;
            }
            break;
        }
    }
}

void rgb_matrix_hs_set_remain_time(uint8_t index, uint8_t remain_time) {
    for (int i = 0; i < HS_RGB_INDICATOR_COUNT; i++) {
        if (hs_rgb_indicators[i].index == index) {
            hs_rgb_indicators[i].times  = 0;
            hs_rgb_indicators[i].active = false;
            break;
        }
    }
}

void rgb_matrix_hs_indicator(void) {
    for (int i = 0; i < HS_RGB_INDICATOR_COUNT; i++) {
        if (hs_rgb_indicators[i].active) {
            if (timer_elapsed32(hs_rgb_indicators[i].timer) >= hs_rgb_indicators[i].interval) {
                hs_rgb_indicators[i].timer = timer_read32();

                if (hs_rgb_indicators[i].times) {
                    hs_rgb_indicators[i].times--;
                }

                if (hs_rgb_indicators[i].times <= 0) {
                    hs_rgb_indicators[i].active = false;
                    hs_rgb_indicators[i].timer  = 0x00;
                    if (hs_rgb_indicators[i].blink_cb != NULL) hs_rgb_indicators[i].blink_cb(i);
                    continue;
                }
            }

            if ((hs_rgb_indicators[i].times % 2)) {
                if (hs_rgb_indicators[i].index == 0xFF) {
                    rgb_matrix_set_color_all(hs_rgb_indicators[i].rgb.r, hs_rgb_indicators[i].rgb.g, hs_rgb_indicators[i].rgb.b);
                } else {
                    rgb_matrix_set_color(hs_rgb_indicators[i].index, hs_rgb_indicators[i].rgb.r, hs_rgb_indicators[i].rgb.g, hs_rgb_indicators[i].rgb.b);
                }
            } else {
                if (hs_rgb_indicators[i].index == 0xFF) {
                    rgb_matrix_set_color_all(0x00, 0x00, 0x00);
                } else {
                    rgb_matrix_set_color(hs_rgb_indicators[i].index, 0x00, 0x00, 0x00);
                }
            }
        }
    }
}

bool rgb_matrix_indicators_advanced_kb(uint8_t led_min, uint8_t led_max) {
#ifdef RGBLIGHT_ENABLE
    if (rgb_matrix_indicators_advanced_user(led_min, led_max) != true) {
        return false;
    }
#endif

    if (ee_clr_timer && timer_elapsed32(ee_clr_timer) > 3000) {
        hs_reset_settings();
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

    if (enable_bat_indicators && !rgbrec_is_started()) {
        rgb_matrix_hs_bat();
        bat_indicators();
        bat_indicator_cnt = timer_read32();
    }

    if (!enable_bat_indicators) {
        if (timer_elapsed32(bat_indicator_cnt) > 2000) {
            enable_bat_indicators = true;
            bat_indicator_cnt     = timer_read32();
        }
    }

#endif

    rgb_matrix_hs_indicator();
    if (confinfo.filp) rgb_matrix_set_color(32, RGB_MATRIX_MAXIMUM_BRIGHTNESS, RGB_MATRIX_MAXIMUM_BRIGHTNESS, RGB_MATRIX_MAXIMUM_BRIGHTNESS);
    query();
    return true;
}

void hs_reset_settings(void) {
    if (is_keyboard_master()) {
        master_to_slave_t m2s = {0};
        slave_to_master_t s2m = {0};
        m2s.cmd               = 0xDD;
        if (transaction_rpc_exec(USER_SYNC_MMS, sizeof(m2s), &m2s, sizeof(s2m), &s2m)) {
            if (s2m.resp == 0x00) {
            }
            dprintf("Slave Sleep OK\n");
        } else {
            dprintf("Slave sync failed!\n");
        }
    }
    enable_bat_indicators = false;
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

    // #if defined(WIRELESS_ENABLE)
    //     wireless_devs_change(wireless_get_current_devs(), DEVS_USB, false);
    // #endif

    if (hs_reset_settings_user() != true) {
        return;
    }
    hs_link_activity();
    keyboard_post_init_kb();
}

void lpwr_wakeup_hook(void) {
    hs_mode_scan(false, hsm_mode_seed(wireless_get_current_devs()), hsm_seed_btdev(wireless_get_current_devs(), confinfo.last_btdevs));

    /* Rail from its single owner (defect 5). */
    wls_led_rail_apply();
}

void user_sync_mms_slave_handler(uint8_t in_buflen, const void *in_data, uint8_t out_buflen, void *out_data) {
    const master_to_slave_t *m2s = (const master_to_slave_t *)in_data;
    slave_to_master_t       *s2m = (slave_to_master_t *)out_data;

    switch (m2s->cmd) {
        case 0x55: // sync multimode
            wireless_devs_change(m2s->body[0], m2s->body[1], false);
            s2m->resp = 0x00;
            break;
        case 0xAA:
            if (wireless_get_current_devs() != DEVS_USB) {
                palSetLineMode(SERIAL_USART_RX_PIN, PAL_OUTPUT_TYPE_OPENDRAIN);
                palSetLineMode(SERIAL_USART_TX_PIN, PAL_OUTPUT_TYPE_OPENDRAIN);
            }
            s2m->resp = 0x00;
            wls_order_sleep(m2s->body[0]);
            lpwr_set_timeout_manual(true);
            break;
        case 0xBB:
            /* The master is suspending. Do not force this half's rail off: on
             * this board the rail is derived from the RGB value and has one
             * owner (`wls_led_rail_apply()`), so a forced-off here is exactly the
             * latch that left a half dark while it still typed (defect 5). The
             * slave's own suspend path drops the rail when it actually sleeps. */
            s2m->resp = 0x00;
            break;
        case 0xCC:
            wls_led_rail_apply();
            s2m->resp = 0x00;
            break;
        case 0xDD:
            hs_reset_settings();
            s2m->resp = 0x00;
            break;
        default:
            break;
    }
}