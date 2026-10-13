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
#include "wls/wls_indicator.h"
#include "wls/wls_keymap.h"
#include "rgb_record/rgb_record.h"
#include "quantum.h"
#include "connection.h"
#include "serial_usart.h"
#include "transactions.h"
#include "usb_util.h"
#ifdef WIRELESS_ENABLE
#    include "wireless.h"
#    include "usb_main.h"
#    include "lowpower.h"
#    include "module.h"
#endif

/* The board state that belongs to `process_record_user` (a QMK hook the keymap
 * may override, so it stays here rather than moving to a concern module). */
static bool no_record_fg;

void keyboard_post_init_kb(void) {
#ifdef CONSOLE_ENABLE
    debug_enable = true;
#endif
    /* Diagnostic boot counter: one increment per MCU boot, so a host reader can
     * tell a firmware reset loop from a mere USB-line drop (see `wls_diag`). */
    wls_note_boot();

    eeconfig_confinfo_init();
#ifdef RGBLIGHT_ENABLE
    rgblight_mode(wls_rgblight_modes[0]);
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

bool process_record_kb(uint16_t keycode, keyrecord_t *record) {
    /* `process_record_user` first (the QMK-mandated delegation); then the board's
     * own keycode concern, which decides and applies everything else. */
    if (process_record_user(keycode, record) != true) {
        return false;
    }

    return wls_keymap_process_record(keycode, record);
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

#    ifdef WIRELESS_ENABLE
    if (is_keyboard_master()) {
        static uint32_t last_sync = 0;
        if (timer_elapsed32(last_sync) > 2000) {
            last_sync = timer_read32();
            master_sync_mms_slave(wireless_get_current_devs(), wireless_get_current_devs(), *md_getp_bat());
        }
    }
#    endif
}

#endif /* WIRELESS_ENABLE */

#ifdef RGB_MATRIX_ENABLE

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

#endif

bool rgb_matrix_indicators_advanced_kb(uint8_t led_min, uint8_t led_max) {
#ifdef RGBLIGHT_ENABLE
    if (rgb_matrix_indicators_advanced_user(led_min, led_max) != true) {
        return false;
    }
#else
    (void)led_min;
    (void)led_max;
#endif

    /* The EEPROM-clear hold: once past the threshold, the reset runs (LOCAL). */
    if (wls_ee_clr_timer && timer_elapsed32(wls_ee_clr_timer) > 3000) {
        wls_reset_settings(WLS_RESET_CAUSE_LOCAL);
        wls_ee_clr_timer = 0;
    }

    if (host_keyboard_led_state().caps_lock) rgb_matrix_set_color(HS_RGB_INDEX_CAPS, 0x20, 0x20, 0x20);

    if (!keymap_is_mac_system() && keymap_config.no_gui) rgb_matrix_set_color(HS_RGB_INDEX_WIN_LOCK, 0x20, 0x20, 0x20);

#ifdef RGBLIGHT_ENABLE
    if (rgb_matrix_indicators_advanced_rgblight(led_min, led_max) != true) {
        return false;
    }
#endif

#ifdef WIRELESS_ENABLE
    wls_indicator_channel_flash();

    /* The boot-report readout: only paints while its sequence runs (a few
     * seconds after boot), so it is additive and cannot disturb the passes
     * below. It is the only indicator that works with the USB line down. */
    wls_indicator_boot_report();

    /* While suppressed (post-reset) the battery indicator is hidden; the
     * suppression is a deadline, so it simply expires — no re-arm, no flag to
     * flip back. Otherwise the indicator is shown and its own bounded blink
     * (wls_indicator_battery_blink) drives the cadence. */
    if (timer_read32() >= wls_bat_suppress_deadline() && !rgbrec_is_started()) {
        wls_indicator_battery_blink();
        wls_indicator_battery();
    }
#endif

    wls_indicator_set_flash();
    if (confinfo.filp) rgb_matrix_set_color(32, RGB_MATRIX_MAXIMUM_BRIGHTNESS, RGB_MATRIX_MAXIMUM_BRIGHTNESS, RGB_MATRIX_MAXIMUM_BRIGHTNESS);
    query();
    return true;
}

void lpwr_wakeup_hook(void) {
    wls_lifecycle_lpwr_wakeup();
}