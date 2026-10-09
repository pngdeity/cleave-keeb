// Copyright 2026
// SPDX-License-Identifier: GPL-2.0-or-later

#include "wls.h"
#include "usb_descriptor.h"
#include "lowpower_logic.h"
#include "transport_logic.h"

static ioline_t col_pins_left[MATRIX_COLS]  = MATRIX_COL_PINS;
static ioline_t col_pins_right[MATRIX_COLS] = MATRIX_COL_PINS_RIGHT;

/* The LED rail: A5/A8 (`LED_POWER_EN_PIN`/`LED_POWER_EN2_PIN`) are the
 * backlight rail ENABLE, one writer's concern. Historically the rail was written
 * ad hoc from four sites that disagreed -- a boot/wake snapshot of the RGB
 * value, the local brightness keycodes, and the cross-half 0xBB/0xCC relay --
 * so a retained-LOW write (a stray 0xBB whose matching 0xCC sender is USB-gated
 * off, or a local value-down press) could leave a half permanently dark while it
 * still typed over the split link (defect 5). Rather than add a fifth writer,
 * make the rail a pure function of the RGB value and give it one home: every
 * writer now calls this, so the rail self-derives and cannot be latched. */
void wls_led_rail_apply(void) {
    bool on = rgb_matrix_get_val() != 0;
#if defined(LED_POWER_EN_PIN)
    gpio_write_pin(LED_POWER_EN_PIN, on);
#endif
#if defined(LED_POWER_EN2_PIN)
    gpio_write_pin(LED_POWER_EN2_PIN, on);
#endif
}

bool hs_modeio_detection(bool update, uint8_t *mode, uint8_t lsat_btdev) {
    static uint32_t scan_timer = 0x00;

    if ((update != true) && (timer_elapsed32(scan_timer) <= (HS_MODEIO_DETECTION_TIME))) {
        return false;
    }
    scan_timer = timer_read32();
#if defined(HS_BT_DEF_PIN) && defined(HS_2G4_DEF_PIN)
    uint8_t        now_mode  = 0x00;
    uint8_t        hs_mode   = 0x00;
    static uint8_t last_mode = 0x00;
    bool           sw_mode   = false;
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
            if (sw_mode) wireless_devs_change(wireless_get_current_devs(), DEVS_USB, false);

            break;
        default:
            break;
    }
    if (sw_mode) {
        /* `sw_mode` is the one predicate that means "the switch really moved",
         * so this is the correct place to confirm the new channel: the
         * indicator fires here and nowhere a re-assert could reach (defect:
         * see `wls_indicate_devs()`). */
        wls_indicate_devs(wireless_get_current_devs(), false);
        hs_link_activity();
        suspend_wakeup_init();
        return true;
    }
#else
    *mode = hs_none;
#endif

    return false;
}

/* Derive the device index from the physical switch LEVEL alone (no edge, no
 * persisted state). Used once at boot so the switch is authoritative over a
 * persisted `confinfo.devs`: a stale wireless index restored from EEPROM (the
 * defect-4 lockout) is corrected the moment the board comes up, instead of
 * waiting for a switch edge that never comes. The switch cannot express which
 * of BT1..5, so a BT position returns the caller's `lsat_btdev`. The decision
 * itself is the tested pure core (`transport_logic.c`); this only reads pins. */
uint8_t hs_mode_switch_devs(uint8_t lsat_btdev) {
#if defined(HS_BT_DEF_PIN) && defined(HS_2G4_DEF_PIN)
    hsm_switch_pos_t pos;
    if (HS_GET_MODE_PIN(HS_USB_PIN_STATE)) {
        pos = HSM_SWITCH_USB;
    } else if (HS_GET_MODE_PIN(HS_BT_PIN_STATE)) {
        pos = HSM_SWITCH_BT;
    } else if (HS_GET_MODE_PIN(HS_2G4_PIN_STATE)) {
        pos = HSM_SWITCH_2G4;
    } else {
        pos = HSM_SWITCH_UNKNOWN;
    }
    return hsm_boot_devs(pos, lsat_btdev);
#else
    (void)lsat_btdev;
    return DEVS_USB;
#endif
}

static hsm_link_timers_t hs_link_timers = {0, 0};
static uint8_t           hs_link_last_status;

bool hs_mode_scan(bool update, uint8_t moude, uint8_t lsat_btdev) {
    if (hs_modeio_detection(update, &moude, lsat_btdev)) {
        return true;
    }
    hs_rgb_blink_hook();
    return false;
}

/* Map the module's MD_STATE_* onto the pure core's link-state enum. */
static hsm_link_state_t hs_link_state(void) {
    switch (*md_getp_state()) {
        case MD_STATE_PAIRING:
            return HSM_LINK_PAIRING;
        case MD_STATE_CONNECTED:
            return HSM_LINK_CONNECTED;
        case MD_STATE_DISCONNECTED:
            return HSM_LINK_DISCONNECTED;
        case MD_STATE_REJECT:
            return HSM_LINK_REJECT;
        case MD_STATE_NONE:
        default:
            return HSM_LINK_NONE;
    }
}

/* Restart the active link countdown from now (an activity event). The policy is
 * the pure `hsm_link_restart`; this only supplies `now`, the timeouts, and the
 * timers store. */
void hs_link_activity(void) {
    hsm_link_restart(hs_link_state(), timer_read32(), HS_LBACK_TIMEOUT, HS_SLEEP_TIMEOUT, &hs_link_timers);
}

bool hs_rgb_blink_hook() {
    if (!is_keyboard_master()) {
        return false;
    }

    hsm_link_state_t state         = hs_link_state();
    bool             state_changed = (hs_link_last_status != (uint8_t)state);
    hs_link_last_status            = (uint8_t)state;

    /* The whole reconnect/sleep policy is a pure decision; this shell only
     * performs the returned action. Two independent deadlines live in
     * `hs_link_timers` (see transport_logic), never one shared timestamp. */
    hsm_link_action_t action = hsm_link_watch(state, state_changed, timer_read32(), HS_LBACK_TIMEOUT, HS_SLEEP_TIMEOUT, &hs_link_timers);

    switch (action) {
        case HSM_LINK_ACT_RECONNECT:
            md_send_devctrl(MD_SND_CMD_DEVCTRL_USB);
            wait_ms(200);
            lpwr_set_timeout_manual(true);
            break;
        case HSM_LINK_ACT_SLEEP:
            lpwr_set_timeout_manual(true);
            break;
        case HSM_LINK_ACT_NONE:
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

bool lpwr_stop_is_allowed(void) {
    /* This board's sleep policy: only the master may stop, and only when it has
     * been ordered to (lower_sleep), which is the low-battery path. A plain idle
     * timeout reaches STOP unordered, and on that path the wake is not
     * interpreted — the board's lpwr_stop_hook_post() is gated on lower_sleep,
     * and a wake code of LPWR_WAKEUP_UART returns the machine to LPWR_STOP
     * without ever running lpwr_wakeup_cb(). The half then re-enters STOP with
     * no rail re-raise and no matrix re-init: dark and unresponsive (observed on
     * Bluetooth, where the master is not USB-exempt). Refuse any unordered stop.
     *
     * This is a policy, not a statement that no wake source is armed — on both
     * paths the master arms its own rows, the mode-switch pins and the cable
     * pin. What lower_sleep actually gates is the cross-half column drive, the
     * module-sleep command, and the post-stop wake interpretation. */
    return lpwr_stop_is_allowed_decide(is_keyboard_master(), lower_sleep);
}

uint32_t lpwr_wakeup_armed_mask(void) {
    /* The codes this board actually arms, and only those. UART is deliberately
     * absent: this board's lpwr_exti_init() never arms UART_RX_PIN (the
     * module's own traffic would defeat deep sleep), yet the WB32 EXTI reports
     * by pad number alone, so the module UART RX pad is aliased to a matrix
     * column and a column edge arrives here stamped LPWR_WAKEUP_UART. Naming
     * UART armed would let that phantom be acted on as a real wake. */
    return LPWR_WAKEUP_MATRIX | LPWR_WAKEUP_CABLE | LPWR_WAKEUP_SWITCH | LPWR_WAKEUP_USB;
}

void lpwr_stop_hook_post(void) {
    /* The state transition itself is now owned by lpwr_stop_cb(), which
     * interprets the wake as a set against lpwr_wakeup_armed_mask(). All this
     * hook needs to do is drop the board's ordered-sleep flag on a real wake,
     * so the next stop starts unordered. The switch that used to live here
     * predates the wake set and would misread a multi-bit code. */
    if (lower_sleep && lpwr_get_state() == LPWR_WAKEUP) {
        lower_sleep = false;
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