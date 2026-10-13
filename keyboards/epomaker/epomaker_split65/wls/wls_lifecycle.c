// Copyright 2026
// SPDX-License-Identifier: GPL-2.0-or-later

#include "wls_lifecycle.h"

#include "wls/wls.h"
#include "wls/wls_eeprom.h"
#include "wls/wls_split.h"
#include "wls/split_logic.h"
#include "wls/reset_logic.h"
#include "wls/wls_indicator.h"
#include "wls/transport_logic.h"
#include "wls/wls_usb.h"
#include "quantum.h"
#include "usb_util.h"

#ifdef WIRELESS_ENABLE
#    include "wireless.h"
#    include "usb_main.h"
#    include "lowpower.h"
#    include "module.h"
#endif

bool charging_state = false;
bool bat_full_flag  = false;

#ifdef WIRELESS_ENABLE
static uint32_t post_init_timer = 0x00;
#endif

/* The board's durable state lives in `wls_eeprom` (confinfo). The boot pass
 * seeds the transport from the physical switch: one switch read, applied to
 * both selectors, and the USB line driven from the applied host (see the A.2
 * note in `apply_switch_transport`). */
static uint8_t last_btdev_from_confinfo(void) {
    return (confinfo.devs >= DEVS_BT1 && confinfo.devs <= DEVS_BT5) ? confinfo.devs : confinfo.last_btdevs;
}

void wls_lifecycle_boot(void) {
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
        /* Seed the transport from the switch before any report can be sent: the
         * shared applier sets BOTH selectors (host + device index) from one
         * switch read, so no report can precede the switch's authority. */
        /* Drive the USB data line from the host we *applied*, not from a re-read
         * of `connection_get_host()`: `connection_set_host_noeeprom` early-returns
         * when EEPROM already holds this host, so a re-read can be stale and drop
         * the line. One read, one decision, applied consistently. */
        connection_host_t applied = apply_switch_transport(hs_mode_switch_pos(), last_btdev_from_confinfo());
        wls_usb_connect(applied == CONNECTION_HOST_USB);
    }
#    endif
    post_init_timer = timer_read32();
#endif
}

#ifdef WIRELESS_ENABLE

void wls_lifecycle_suspend_power_down_kb(void) {
#    ifdef LED_POWER_EN_PIN
    gpio_write_pin_low(LED_POWER_EN_PIN);
#    endif

#    ifdef LED_POWER_EN2_PIN
    gpio_write_pin_low(LED_POWER_EN2_PIN);
#    endif
}

void wls_lifecycle_suspend_wakeup_init_kb(void) {
    /* Rail from its single owner (defect 5). */
    wls_led_rail_apply();

    wireless_devs_change(wireless_get_current_devs(), wireless_get_current_devs(), false);
}

void wls_lifecycle_suspend_wakeup_init_user(void) {
    usart_init();

    /* The 0xCC/0xBB pair relay LED-rail power to the slave, so they only make
     * sense on battery. While cabled the rail must stay up on both halves;
     * broadcasting it would extinguish the slave's backlight. */
    if (wireless_get_current_devs() == DEVS_USB) {
        return;
    }

    if (wls_split_send(WLS_SPLIT_CMD_WAKEUP, NULL)) {
        dprintf("Slave Sleep OK\n");
    } else {
        dprint("Slave sync failed!\n");
    }
}

void wls_lifecycle_suspend_power_down_user(void) {
    if (wireless_get_current_devs() == DEVS_USB) {
        return;
    }

    if (wls_split_send(WLS_SPLIT_CMD_POWER_DOWN, NULL)) {
        dprintf("Slave Sleep OK\n");
    } else {
        dprint("Slave sync failed!\n");
    }
}

bool wls_lifecycle_lpwr_allow_timeout(void) {
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

bool wls_lifecycle_lpwr_allow_presleep(void) {
    if (is_keyboard_master()) {
        const uint8_t body[4] = {wls_sleep_ordered(), 0, 0, 0};
        if (wls_split_send(WLS_SPLIT_CMD_SLEEP, body)) {
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

void wls_lifecycle_lpwr_wakeup(void) {
    hs_mode_scan(false, hsm_mode_seed(wireless_get_current_devs()), hsm_seed_btdev(wireless_get_current_devs(), confinfo.last_btdevs));

    /* Rail from its single owner (defect 5). */
    wls_led_rail_apply();
}

void wls_lifecycle_wireless_post_task(void) {
    // auto switching devs
    if (post_init_timer && timer_elapsed32(post_init_timer) >= 100) {
        md_send_devctrl(MD_SND_CMD_DEVCTRL_FW_VERSION);   // get the module fw version.
        md_send_devctrl(MD_SND_CMD_DEVCTRL_SLEEP_BT_EN);  // timeout 30min to sleep in bt mode, enable
        md_send_devctrl(MD_SND_CMD_DEVCTRL_SLEEP_2G4_EN); // timeout 30min to sleep in 2.4g mode, enable
        /* The physical switch is authoritative at boot. Restoring the persisted
         * index blindly let a stale wireless selection survive a reboot with the
         * switch on USB — the board came up on a dead wireless link while USB
         * stayed enumerated ("enumerated but no keys"; defect 4). The shared
         * applier derives BOTH selectors (the vendor device index *and*
         * upstream's `config.desired_host`, which resolves
         * `host_get_active_driver()`) from the switch LEVEL, so they can never
         * disagree and route HID through the wrong driver (split brain). It is a
         * switch-driven change, so it does not persist (14c). Persistence still
         * supplies the BT1..5 sub-selection the switch cannot express. */
        apply_switch_transport(hs_mode_switch_pos(), last_btdev_from_confinfo());
        post_init_timer = 0x00;
    }
#    if defined(HS_BT_DEF_PIN) && defined(HS_2G4_DEF_PIN)
    hs_mode_scan(false, hsm_mode_seed(wireless_get_current_devs()), hsm_seed_btdev(wireless_get_current_devs(), confinfo.last_btdevs));
#    endif
}

#endif /* WIRELESS_ENABLE */

void wls_lifecycle_housekeeping(void) {
#ifdef HS_BAT_CABLE_PIN
    charging_state = gpio_read_pin(HS_BAT_CABLE_PIN);
#endif

#ifdef BAT_FULL_PIN
    bat_full_flag = gpio_read_pin(BAT_FULL_PIN);
#endif

    /* Single owner for the LED rail: re-derive A5/A8 from the current RGB value
     * on every half. See `wls_led_rail_apply()`. */
    wls_led_rail_apply();

#ifdef WIRELESS_ENABLE
    uint8_t hs_now_mode;
    if (charging_state && (bat_full_flag)) {
        hs_now_mode = MD_SND_CMD_DEVCTRL_CHARGING_DONE;
    } else if (charging_state) {
        hs_now_mode = MD_SND_CMD_DEVCTRL_CHARGING;
    } else {
        hs_now_mode = MD_SND_CMD_DEVCTRL_CHARGING_STOP;
    }

    static uint32_t hs_current_time;
    if (!hs_current_time || timer_elapsed32(hs_current_time) > WLS_MODULE_POLL_MS) {
        hs_current_time = timer_read32();
        md_send_devctrl(hs_now_mode);
        md_send_devctrl(MD_SND_CMD_DEVCTRL_INQVOL);
    }
#endif
}

/* Execute the reset plan. The *policy* (which consequences, in what order, and
 * the one thing the cause changes) is the pure `wls_reset_plan_of()`
 * (wls/reset_logic); this function only performs the steps it names. Two
 * unrelated triggers reach it -- the `EE_CLR` hold timer (LOCAL) and the split
 * slave handler (REMOTE); the cause is the whole reason that handler no longer
 * needs to call back into the lifecycle module. */
void wls_reset_settings(wls_reset_cause_t cause) {
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
                wls_bat_suppress_arm();
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
                wls_lifecycle_boot();
                break;
        }
    }
}
