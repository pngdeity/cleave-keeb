// Copyright 2026
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include QMK_KEYBOARD_H

#include <stdbool.h>
#include <stdint.h>

/* The board's LED concern: the channel flash, the "set"-driven flashes, the
 * battery blink, the caps/winlock paint, and the settings-reset flash.
 *
 * The indicator *decisions* are the pure `wls/indicator_logic` (the blink tick
 * and the battery colour table). What lives here is the shell: the three
 * indicator arrays, the timers, and the LED writes. `rgb_matrix_indicators_
 * advanced_kb` stays in the board file (it is the QMK hook) and delegates here.
 */

/* Paint the channel flash if armed (called every RGB frame). */
void wls_indicator_channel_flash(void);

/* Paint the battery blink and drive the low-battery sleep order. Master-gated. */
void wls_indicator_battery(void);

/* Paint the battery blink only (the "set"-driven flashes). */
void wls_indicator_battery_blink(void);

/* Paint the "set"-driven flashes (the reset/ack/nkr family). */
void wls_indicator_set_flash(void);

/* Arm the channel flash for `new_devs` (a genuine transition). `reset` picks the
 * re-pair (fast) vs select (slow) cadence. */
void wls_indicate_devs(uint8_t new_devs, bool reset);

/* Arm a flash on `index` for `times` blinks at `interval` ms. `index` 0xFF means
 * "all LEDs" and chains the nkr colour hook. */
void rgb_matrix_hs_indicator_set(uint8_t index, RGB rgb, uint32_t interval, uint8_t times);

/* Cancel any armed flash on `index` (e.g. a pending A/B toast). */
void rgb_matrix_hs_set_remain_time(uint8_t index, uint8_t remain_time);

/* Cancel the four direction-LED toasts (a threshold ack). */
void rgb_blink_dir(void);

/* The settings-reset acknowledgement flash; returns whether it was accepted. */
bool hs_reset_settings_user(void);

/* The battery-query latch, owned by the keycode concern (wls_keymap) and read
 * by the battery paint. */
extern bool im_bat_req_charging_flag;

/* The EEPROM-clear hold deadline, owned by the keycode concern and read by the
 * indicator pass to fire the reset. */
extern uint32_t wls_ee_clr_timer;

/* The battery suppression deadline (post-reset). */
uint32_t wls_bat_suppress_deadline(void);

/* Arm the post-reset battery suppression. */
void wls_bat_suppress_arm(void);

/* The transport confirmation flash is armed from the mode-switch edge; exposed
 * for the lifecycle concern. */
void wls_indicator_wls_set(uint8_t index, RGB rgb, uint32_t interval, uint8_t times);
