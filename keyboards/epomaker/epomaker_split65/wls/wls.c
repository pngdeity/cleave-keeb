// Copyright 2026
// SPDX-License-Identifier: GPL-2.0-or-later

#include "wls.h"
#include "usb_descriptor.h"

static ioline_t col_pins_left[MATRIX_COLS]  = MATRIX_COL_PINS;
static ioline_t col_pins_right[MATRIX_COLS] = MATRIX_COL_PINS_RIGHT;

bool hs_modeio_detection(bool update, uint8_t *mode, uint8_t lsat_btdev) {
    static uint32_t scan_timer = 0x00;

    if ((update != true) && (timer_elapsed32(scan_timer) <= (HS_MODEIO_DETECTION_TIME))) {
        return false;
    }
    scan_timer = timer_read32();
#if defined(HS_BT_DEF_PIN) && defined(HS_2G4_DEF_PIN)
    uint8_t now_mode         = 0x00;
    uint8_t hs_mode          = 0x00;
    static uint8_t last_mode = 0x00;
    bool sw_mode             = false;
    now_mode                 = (HS_GET_MODE_PIN(HS_USB_PIN_STATE) ? 3 : (HS_GET_MODE_PIN(HS_BT_PIN_STATE) ? 1 : ((HS_GET_MODE_PIN(HS_2G4_PIN_STATE) ? 2 : 0))));
    hs_mode                  = (*mode >= DEVS_BT1 && *mode <= DEVS_BT5) ? 1 : ((*mode == DEVS_2G4) ? 2 : ((*mode == DEVS_USB) ? 3 : 0));
    sw_mode                  = ((update || (last_mode == now_mode)) && (hs_mode != now_mode)) ? true : false;
    last_mode                = now_mode;

    switch (now_mode) {
        case 1:
            *mode = hs_bt;
            if (sw_mode) {
                wireless_devs_change(wireless_get_current_devs(), lsat_btdev, false);
            }
            break;
        case 2:
            *mode = hs_2g4;
            if (sw_mode) {
                wireless_devs_change(wireless_get_current_devs(), DEVS_2G4, false);
            }
            break;
        case 3:
            *mode = hs_usb;
            if (sw_mode)
                wireless_devs_change(wireless_get_current_devs(), DEVS_USB, false);

            break;
        default:
            break;
    }

    if (sw_mode) {
        hs_rgb_blink_set_timer(timer_read32());
        suspend_wakeup_init();
        return true;
    }
#else
    *mode = hs_none;
#endif

    return false;
}

static uint32_t hs_linker_rgb_timer = 0x00;

bool hs_mode_scan(bool update, uint8_t moude, uint8_t lsat_btdev) {

    if (hs_modeio_detection(update, &moude, lsat_btdev)) {

        return true;
    }
    hs_rgb_blink_hook();
    return false;
}

void hs_rgb_blink_set_timer(uint32_t time) {
    hs_linker_rgb_timer = time;
}

uint32_t hs_rgb_blink_get_timer(void) {
    return hs_linker_rgb_timer;
}

bool hs_rgb_blink_hook() {
    static uint8_t last_status;

    if (!is_keyboard_master())  {
        return false;
    }
    
    if (last_status != *md_getp_state()) {
        last_status = *md_getp_state();
        hs_rgb_blink_set_timer(0x00);
    }

    switch (*md_getp_state()) {
        case MD_STATE_NONE: {
            hs_rgb_blink_set_timer(0x00);
        } break;

        case MD_STATE_DISCONNECTED:
            if (hs_rgb_blink_get_timer() == 0x00) {
                hs_rgb_blink_set_timer(timer_read32());
                extern void wireless_devs_change_kb(uint8_t old_devs, uint8_t new_devs, bool reset);
                wireless_devs_change_kb(wireless_get_current_devs(), wireless_get_current_devs(), false);
            } else {
                if (timer_elapsed32(hs_rgb_blink_get_timer()) >= HS_LBACK_TIMEOUT) {
                    hs_rgb_blink_set_timer(timer_read32());
                    md_send_devctrl(MD_SND_CMD_DEVCTRL_USB);
                    wait_ms(200);
                    lpwr_set_timeout_manual(true);
                }
            }
        case MD_STATE_CONNECTED:
            if (hs_rgb_blink_get_timer() == 0x00) {
                hs_rgb_blink_set_timer(timer_read32());
            } else {
                if (timer_elapsed32(hs_rgb_blink_get_timer()) >= HS_SLEEP_TIMEOUT) {
                    hs_rgb_blink_set_timer(timer_read32());
                    lpwr_set_timeout_manual(true);
                }
            }
        default:
            break;
    }
    return true;
}

void lpwr_exti_init_hook(void) {

#ifdef HS_BT_DEF_PIN
    if (is_keyboard_master()) {
        gpio_set_pin_input_high(HS_BT_DEF_PIN);
        waitInputPinDelay();
        palEnableLineEvent(HS_BT_DEF_PIN, PAL_EVENT_MODE_BOTH_EDGES);
    }
#endif

#ifdef HS_2G4_DEF_PIN
    if (is_keyboard_master()) {
        gpio_set_pin_input_high(HS_2G4_DEF_PIN);
        waitInputPinDelay();
        palEnableLineEvent(HS_2G4_DEF_PIN, PAL_EVENT_MODE_BOTH_EDGES);
    }
#endif
    
    if (lower_sleep) {
#if DIODE_DIRECTION == ROW2COL
        /* Drive both halves' columns high so a keypress on either half pulls
         * its row low and can wake the board. */
        for (uint8_t i = 0; i < ARRAY_SIZE(col_pins_left); i++) {
            if (col_pins_left[i] != NO_PIN) {
                gpio_set_pin_output(col_pins_left[i]);
                gpio_write_pin_high(col_pins_left[i]);
            }
        }

        for (uint8_t i = 0; i < ARRAY_SIZE(col_pins_right); i++) {
            if (col_pins_right[i] != NO_PIN) {
                gpio_set_pin_output(col_pins_right[i]);
                gpio_write_pin_high(col_pins_right[i]);
            }
        }
#endif
    }
    gpio_set_pin_input(HS_BAT_CABLE_PIN);
    waitInputPinDelay();
    palEnableLineEvent(HS_BAT_CABLE_PIN, PAL_EVENT_MODE_RISING_EDGE);
}

void palcallback_cb(uint8_t line) {
    switch (line) {
        case PAL_PAD(HS_BAT_CABLE_PIN): {
            lpwr_set_sleep_wakeupcd(LPWR_WAKEUP_CABLE);
        } break;
#ifdef HS_BT_DEF_PIN
        case PAL_PAD(HS_BT_DEF_PIN): {
            lpwr_set_sleep_wakeupcd(LPWR_WAKEUP_SWITCH);
        } break;
#endif

#ifdef HS_2G4_DEF_PIN
        case PAL_PAD(HS_2G4_DEF_PIN): {
            lpwr_set_sleep_wakeupcd(LPWR_WAKEUP_SWITCH);
        } break;
#endif
        default: {

        } break;
    }
}

void lpwr_stop_hook_pre(void) {

    gpio_write_pin_low(LED_POWER_EN_PIN);

    if (lower_sleep) {
        md_send_devctrl(MD_SND_CMD_DEVCTRL_USB);
        wait_ms(200);
    }
}

bool lpwr_wakeup_is_armed(void) {

    /* This board has no unconditional wake source: the mode-switch EXTIs are
     * armed master-only in lpwr_exti_init_hook(), and the column drive that
     * lets a keypress on either half pull a row low is gated on lower_sleep.
     * So a half is only wake-armed on the ordered low-battery sleep, where
     * lower_sleep was set first. Any other path (a plain idle timeout, or a
     * slave timing itself out) would stop unwakeable, so report false and let
     * the shared state machine refuse the stop. */
    return is_keyboard_master() && lower_sleep;
}

void lpwr_stop_hook_post(void) {
    if (lower_sleep) {
        switch (lpwr_get_sleep_wakeupcd()) {
            case LPWR_WAKEUP_USB:
            case LPWR_WAKEUP_CABLE:
            case LPWR_WAKEUP_SWITCH:
            case LPWR_WAKEUP_MATRIX: {
                lower_sleep = false;
                lpwr_set_state(LPWR_WAKEUP);
            } break;
            default: {
                lpwr_set_state(LPWR_STOP);
            } break;
        }
    }
}

/* The percentage itself is owned upstream: `battery_driver_sample_percent()`
 * (wls_battery_driver.c) feeds quantum/battery's cached `battery_get_percent()`
 * (see `kb_battery_snapshot()` below). */

uint8_t kb_battery_charge(void) {
    if (!charging_state) {
        return KB_BATTERY_CHARGE_DISCHARGING;
    }
    return bat_full_flag ? KB_BATTERY_CHARGE_FULL : KB_BATTERY_CHARGE_CHARGING;
}

uint8_t kb_battery_transport(void) {
    uint8_t devs = wireless_get_current_devs();

    if (devs == DEVS_USB) {
        return KB_BATTERY_TRANSPORT_USB;
    }
    if (devs == DEVS_2G4) {
        return KB_BATTERY_TRANSPORT_2G4;
    }
    return KB_BATTERY_TRANSPORT_BT;
}

bool kb_battery_changed(const kb_battery_snapshot_t *snap) {
    static uint8_t last_percent = 0xff, last_charge = 0xff, last_transport = 0xff;

    if (snap->percent == last_percent && snap->charge == last_charge && snap->transport == last_transport) {
        return false;
    }
    last_percent   = snap->percent;
    last_charge    = snap->charge;
    last_transport = snap->transport;
    return true;
}

void kb_battery_snapshot(kb_battery_snapshot_t *out) {
    /* Sample live sources exactly once, then derive every field from that
     * single sample so callers cannot observe a torn state. The level comes
     * from upstream's cached `battery_get_percent()` so the whole firmware
     * reports one consistent value. */
    out->transport = kb_battery_transport();
    out->percent   = battery_get_percent();
    out->charge    = kb_battery_charge();
}