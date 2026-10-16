// Copyright 2024 yangzheng20003 (@yangzheng20003)
// SPDX-License-Identifier: GPL-2.0-or-later

#include QMK_KEYBOARD_H
#include "wls/wls.h"
#include "wls/transport_logic.h"
#include "wls/wls_transport.h"
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

    /* Consume the MCU's latched reset cause, once, right after the boot counter
     * and before anything else can clear it. The chip names why it last reset
     * (power-on / software / watchdog / pin / ...); reading it here turns a
     * reset loop from "the count climbs" into "the count climbs and the cause is
     * X". Must precede `eeconfig_confinfo_init()` and `wls_lifecycle_boot()`,
     * which is where the LED code is first derived from these facts. */
    wls_note_reset_cause();

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
    /* Any key dismisses the boot diagnostic code, whether or not the keymap
     * consumes the record -- the code's job is done the moment the operator is
     * using the board. Idempotent, so it costs nothing once the code has
     * finished on its own. */
    if (record->event.pressed) {
        wls_indicator_boot_code_dismiss();
    }

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
 * here is the physical side of a host change -- the USB data-line connect and
 * disconnect -- not a driver swap (item 14d). `host_get_active_driver()` picks
 * the driver from the host, so there is no `host_set_driver()` glue left.
 *
 * This hook NO LONGER drives the USB line. It used to re-derive intent from the
 * live resolved host (`wls_usb_connect(connection_get_host() == USB)`), which
 * made the physical line follow an *observation* instead of an *intent*: every
 * oscillation of that observation became a physical teardown, and because the
 * teardown cleared the very signal the transport decision consumed, the board
 * livelocked at the enumeration cadence. The line is now driven only by the
 * transport owner, and only when the committed transport actually changes -- see
 * `wls/wls_transport.c` and `docs/ADR-transport-authority.md`. */
void connection_host_changed_kb(connection_host_t host) {
    (void)host;
}

/* The USB lifecycle, as an EVENT rather than a polled signal.
 *
 * This is upstream's own notification (fired from
 * `usb_device_state_set_configuration()`/`set_reset()`/`set_suspend()` via
 * `notify_usb_device_state_change()`), so it arrives from the USB stack rather
 * than from a loop that samples a value our own actions perturb -- which is the
 * distinction that removes the livelock. `CONFIGURED` means the host enumerated
 * and is talking to us, which a dumb PSU never does; feeding it to the owner
 * lets the confirmed host outrank the switch, and lets a lost host fall back to
 * the switch's intent.
 *
 * SUSPEND is deliberately NOT a host loss. It is a host *idle* signal -- the
 * very condition QMK handles with remote wakeup -- so treating it as "the host
 * went away" would re-decide the transport on the switch hint and tear the data
 * line down the moment the host stopped polling. On a board whose switch reads
 * wireless while the cable is attached, that is a self-detach: the failure the
 * transport owner exists to remove, reintroduced at idle. Only a reset or a
 * true unconfigure (NO_INIT/INIT) means the host is gone. */
void notify_usb_device_state_change_kb(struct usb_device_state usb_device_state) {
    wls_host_event_t event = hsm_host_present_on_notify((int)usb_device_state.configure_state) ? WLS_HOST_EVENT_CONFIGURED : WLS_HOST_EVENT_UNCONFIGURED;
    /* The owner commits and drives the USB line itself. A host-lifecycle event
     * is confirmed, so it owns the demotion. */
    wls_transport_host_event(event);
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
    /* The boot diagnostic code OWNS the matrix while it plays: it is the one
     * indicator that must be readable with the USB line down, so nothing else
     * may overwrite it. Painting it first and suppressing the rest is the
     * arbitration -- the previous instrument painted additively and was
     * overwritten by the set-flash pass below.
     *
     * It is STARTED here, not at boot (step 33 of `keyboard_init()`), because
     * this is the first point the RGB frame pass is live: arming it earlier let
     * the schedule advance before it could be painted, and a boot that died
     * before the loop ran had "shown" nothing. `wls_indicator_boot_code_start()`
     * is idempotent, so calling it on the first paint arms exactly once. */
    wls_indicator_boot_code_start();
    wls_indicator_boot_code();
    if (wls_indicator_boot_code_owns_matrix()) {
        return true;
    }

    wls_indicator_channel_flash();
    wls_indicator_link_state();
    /* The at-rest channel cue (X-4), painted after the flash (so a running
     * transition flash is not overwritten by a steady cue) and after the link
     * cue (which it defers to on REJECT). Called from the frame pass, so a
     * person can read the live channel without acting -- the requirement being
     * that the mode be visible *while it is in effect*. */
    wls_indicator_channel_state();

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