// Copyright 2026
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include QMK_KEYBOARD_H

#include "quantum.h"
#include "battery.h"
#include <stdbool.h>
#include "wireless.h"
#include "wls/transport_logic.h"
#include "wls/diag_logic.h"
#include "connection.h"

#define HS_BT_DEF_PIN C14
#define HS_2G4_DEF_PIN C15
#define HS_BT_PIN_STATE 0, 1
#define HS_2G4_PIN_STATE 1, 0
#define HS_USB_PIN_STATE 1, 1

#define HS_GET_MODE_PIN_(pin_bt, pin_2g4) ((((#pin_bt)[0] == 'x') || ((gpio_read_pin(HS_BT_DEF_PIN) + 0x30) == ((#pin_bt)[0]))) && (((#pin_2g4)[0] == 'x') || ((gpio_read_pin(HS_2G4_DEF_PIN) + 0x30) == ((#pin_2g4)[0]))))
#define HS_GET_MODE_PIN(state) HS_GET_MODE_PIN_(state)
#define HS_MODEIO_DETECTION_TIME 50
#define HS_LBACK_TIMEOUT (30 * 1000)
#define HS_SLEEP_TIMEOUT (1 * 60000) //(1 * 60000)

/* How often the board re-asserts the cable/charge state to the module and asks
 * it for the volume. This is a *bounded poll*, not a transition effect: an
 * external USB-C cable can be inserted or removed while the keyboard is idle,
 * with no keycode or switch event to hang the update on, so the module must be
 * told periodically. The cadence is the rule, so it is named here rather than
 * left as a bare literal at the call site. */
#define WLS_MODULE_POLL_MS 1000

/* After an EEPROM reset the battery indicator is suppressed for this long, so
 * the wipe's own RGB re-init does not read as a battery event. Expressed as a
 * deadline (below), not as a flag re-derived every frame. */
#define WLS_BAT_SUPPRESS_MS 2000

enum modeio_mode { hs_none = 0, hs_usb, hs_bt, hs_2g4, hs_wireless };

extern bool charging_state;
extern bool bat_full_flag;
/* The ordered-sleep flag's single authority: set/clear through the setter,
 * read as a query. One writer, no side-effecting reads. */
void wls_order_sleep(bool ordered);
bool wls_sleep_ordered(void);
bool hs_rgb_blink_hook(void);
bool hs_mode_scan(bool update, uint8_t moude, uint8_t lsat_btdev);
/* The switch gate, stated once: may a wireless keycode for `target_devs` act,
 * given the switch position the detector reads? Seeds the detector from the live
 * device index and the persisted BT sub-index `last_bt`, then answers via the
 * pure `hsm_keycode_allowed`. Replaces the six private copies of the
 * seed+detect+predicate idiom in the board glue. */
bool hs_keycode_allowed(uint8_t target_devs, uint8_t last_bt);
bool hs_modeio_detection(bool update, uint8_t *mode, uint8_t lsat_btdev);

/* The physical switch position, read from the pins alone (no edge, no persisted
 * state). The one pin read, shared by the boot seed and the transport applier. */
hsm_switch_pos_t hs_mode_switch_pos(void);

/* Apply the switch position's transport, stated once. Reads the pure
 * `hsm_switch_apply(pos, last_bt)` and sets BOTH selectors from that single
 * decision: upstream's connection host (`_noeeprom` — the switch must not
 * overwrite the user's stored intent; only a keycode writes EEPROM) and the
 * vendor device index. Every switch-driven change goes through here, so the two
 * selectors can never be set apart (defect 7). */
connection_host_t apply_switch_transport(hsm_switch_pos_t pos, uint8_t last_bt);
/* Restart the active link countdown (reconnect/sleep) from now, on an activity
 * event such as a keypress or wake. The countdown's policy lives in the pure
 * `hsm_link_watch`; this is only the store-back shell. */
void hs_link_activity(void);
void wls_persist_devs(void);
void wls_led_rail_apply(void);
void wls_indicate_devs(uint8_t new_devs, bool reset);

/* Gather the shell-state diagnostic snapshot (see `wls/diag_logic`). The shell
 * half of the observability seam: reads the live switch, device index, host,
 * boot counter, USB line, and module link state into a `wls_diag_t`; the pure
 * `wls_diag_encode()` turns it into bytes. Exposed so any channel that is up
 * (the 0xA4 USB reply, the radio push) can carry it. */
void wls_diag_gather(wls_diag_t *out);

/* Increment the diagnostic boot counter (once per MCU boot). */
void wls_note_boot(void);

/* BT profile selection, owned by the Bluetooth driver (item 14b): which of the
 * module's BT1..BT5 profiles is active, and re-pairing it. The sub-index has no
 * home in upstream's `connection` model, so it lives with the driver that
 * actually carries it to the module. */
void    bluetooth_select_profile(uint8_t profile, bool reset);
uint8_t bluetooth_get_profile(void);

/* Battery report over raw HID (see docs/PROTOCOL.md). The report is
 * RAW_EPSIZE bytes; only the following are meaningful, the rest are zero. */
#define KB_BATTERY_CMD_GET 0xA4

#define KB_BATTERY_IDX_CMD 0
#define KB_BATTERY_IDX_LEVEL 1
#define KB_BATTERY_IDX_CHARGE 4
#define KB_BATTERY_IDX_TRANSPORT 5
#define KB_BATTERY_IDX_MODEL 6

/* Selector readback (provisional, for hardware verification of items 14b/14c).
 * The persisted host and the BT sub-profile are otherwise unobservable without a
 * working BLE link, so they are exposed here: index 7 = upstream's
 * `config.desired_host` (CONNECTION_HOST_* raw value), index 8 = the BT driver's
 * active profile (DEVS_BT1..BT5). Both are test-only and may be reclaimed. */
#define KB_BATTERY_IDX_HOST 7
#define KB_BATTERY_IDX_BTSUB 8

/* Module fingerprint (see docs/PROTOCOL.md). The CH582F module is the source of
 * the Bluetooth faults and its image is closed and undumpable, so its identity
 * is worth reporting: index 9 = the module firmware version (`0x5D`, 0 if the
 * image does not answer that query), index 10 = the module link state (`0x5B`
 * sub-code range, i.e. the MD_STATE_* value). Read-only diagnostics. */
#define KB_BATTERY_IDX_MD_VERSION 9
#define KB_BATTERY_IDX_MD_STATE 10

/* Shell-state diagnostic block (the observability seam; `wls/diag_logic`). A
 * fixed 6-byte view of how the firmware currently reads the world — the switch
 * position, the device index, the connection host, a boot counter, the USB data
 * line, and the module link state. Encoded by the pure `wls_diag_encode()` so a
 * host reader and the firmware cannot drift. This is the one instrument that
 * works regardless of which transport is up, which is exactly what the
 * shell-only defects (7, 8, 9, and the USB self-detach) needed and lacked. */
#define KB_BATTERY_IDX_DIAG 11

/* KB_BATTERY_IDX_CHARGE */
enum kb_battery_charge {
    KB_BATTERY_CHARGE_DISCHARGING = 0,
    KB_BATTERY_CHARGE_CHARGING,
    KB_BATTERY_CHARGE_FULL,
};

/* KB_BATTERY_IDX_TRANSPORT */
#define KB_BATTERY_TRANSPORT_USB 0x01
#define KB_BATTERY_TRANSPORT_BT 0x02
#define KB_BATTERY_TRANSPORT_2G4 0x04

/* Identifies the keyboard model in the battery report (see PROTOCOL.md).
 * Override in the keyboard's config.h. */
#ifndef KB_BATTERY_MODEL_ID
#    define KB_BATTERY_MODEL_ID 0
#endif

uint8_t kb_battery_charge(void);
uint8_t kb_battery_transport(void);

/* A consistent point-in-time read of the battery state. The three fields are
 * sampled together so that change detection and report assembly observe the
 * same values instead of re-reading live globals at different moments. */
typedef struct {
    uint8_t percent;
    uint8_t charge;
    uint8_t transport;
} kb_battery_snapshot_t;

void kb_battery_snapshot(kb_battery_snapshot_t *out);
bool kb_battery_changed(const kb_battery_snapshot_t *snap);
void kb_battery_push_task(void);