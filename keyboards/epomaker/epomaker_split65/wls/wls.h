// Copyright 2026
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include QMK_KEYBOARD_H

#include "quantum.h"
#include <stdbool.h>
#include "wireless.h"

#define HS_BT_DEF_PIN                     C14
#define HS_2G4_DEF_PIN                    C15
#define HS_BT_PIN_STATE                   0, 1
#define HS_2G4_PIN_STATE                  1, 0
#define HS_USB_PIN_STATE                  1, 1

#define HS_GET_MODE_PIN_(pin_bt, pin_2g4) ((((#pin_bt)[0] == 'x') || ((readPin(HS_BT_DEF_PIN) + 0x30) == ((#pin_bt)[0]))) && (((#pin_2g4)[0] == 'x') || ((readPin(HS_2G4_DEF_PIN) + 0x30) == ((#pin_2g4)[0]))))
#define HS_GET_MODE_PIN(state)            HS_GET_MODE_PIN_(state)
#define HS_MODEIO_DETECTION_TIME          50
#define HS_LBACK_TIMEOUT                  (30 * 1000)
#define HS_SLEEP_TIMEOUT                  (1 * 60000) //(1 * 60000)

enum modeio_mode {
    hs_none = 0,
    hs_usb,
    hs_bt,
    hs_2g4,
    hs_wireless
};

extern bool lower_sleep;
extern bool charging_state;
extern bool bat_full_flag;
bool hs_rgb_blink_hook(void);
bool hs_mode_scan(bool update, uint8_t moude, uint8_t lsat_btdev);
bool hs_modeio_detection(bool update, uint8_t *mode, uint8_t lsat_btdev);
void hs_rgb_blink_set_timer(uint32_t time);
bool hs_transport_arbitrate_cable(bool cable_present, bool prev_present);

/* Battery report over raw HID (see docs/PROTOCOL.md). The report is
 * RAW_EPSIZE bytes; only the following are meaningful, the rest are zero. */
#define KB_BATTERY_CMD_GET 0xA4

#define KB_BATTERY_IDX_CMD   0
#define KB_BATTERY_IDX_LEVEL 1
#define KB_BATTERY_IDX_CHARGE 4
#define KB_BATTERY_IDX_TRANSPORT 5
#define KB_BATTERY_IDX_MODEL 6

/* KB_BATTERY_IDX_CHARGE */
enum kb_battery_charge {
    KB_BATTERY_CHARGE_DISCHARGING = 0,
    KB_BATTERY_CHARGE_CHARGING,
    KB_BATTERY_CHARGE_FULL,
};

/* KB_BATTERY_IDX_TRANSPORT */
#define KB_BATTERY_TRANSPORT_USB 0x01
#define KB_BATTERY_TRANSPORT_BT  0x02
#define KB_BATTERY_TRANSPORT_2G4 0x04

/* Identifies the keyboard model in the battery report (see PROTOCOL.md).
 * Override in the keyboard's config.h. */
#ifndef KB_BATTERY_MODEL_ID
#    define KB_BATTERY_MODEL_ID 0
#endif

uint8_t kb_battery_percent(void);
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

void    kb_battery_snapshot(kb_battery_snapshot_t *out);
bool    kb_battery_changed(const kb_battery_snapshot_t *snap);
void    kb_battery_push_task(void);