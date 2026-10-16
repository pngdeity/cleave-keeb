// Copyright 2026
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include QMK_KEYBOARD_H

#include "wls/indicator_logic.h"

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

/* Paint the link-state cue (the refused-link sight), master-gated. Idempotent:
 * it paints only while the link is REJECT and leaves every other state to its
 * owner, so it is one more indicator rather than a shared-state writer. */
void wls_indicator_link_state(void);

/* Paint the at-rest channel cue: which channel is live *right now* (X-4). Master-
 * gated, and it yields the channel LEDs to `wls_indicator_link_state()` while the
 * link is REJECT (that cue wins). Idempotent: it paints the current channel and
 * leaves the LEDs to their owners when no channel LED is owed (USB / BT4 / BT5). */
void wls_indicator_channel_state(void);

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
void rgb_matrix_hs_cancel_index(uint8_t index);

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

/* Start the boot diagnostic code from the boot facts. Called once, at a point
 * where the RGB frame pass is already live, so its phase is not lost. The code
 * loops until dismissed (a keypress) or the timeout expires. */
void wls_indicator_boot_code_start(void);

/* Whether the boot code currently owns the matrix. While true, no other
 * indicator may paint: the code is the highest-priority owner of the frame, and
 * the arbitration is here rather than in the caller so no other pass can
 * overwrite it (the defect that made the previous instrument unreadable). */
bool wls_indicator_boot_code_owns_matrix(void);

/* Paint the current boot-code phase (a no-op unless the code owns the matrix). */
void wls_indicator_boot_code(void);

/* Dismiss the boot code (a keypress, or the timeout). Idempotent. */
void wls_indicator_boot_code_dismiss(void);

/* --- the LED write chokepoint (D3/D4's unrepresentable rung) -------------------
 *
 * The ONE way a painter writes the matrix. Every indicator in this file, and the
 * caps/win-lock/marker paints in the board file, go through here; the write asks
 * the core's `wls_led_may_write(painter, index, ...)` before touching the LEDs,
 * so "a painter writes an LED it does not own" is refused rather than merely
 * caught by a rule test. The exclusive-claim and REJECT facts are gathered here
 * (they are runtime state the pure core cannot read), and a refused write is
 * dropped, never acted on.
 *
 * `index` is a real LED index; use `wls_indicator_led_write_all` for the
 * all-LEDs painters (it passes the `WLS_LED_INDEX_ALL` sentinel). */
void wls_indicator_led_write(wls_led_painter_t painter, uint8_t index, uint8_t r, uint8_t g, uint8_t b);
void wls_indicator_led_write_all(wls_led_painter_t painter, uint8_t r, uint8_t g, uint8_t b);

/* Whether an exclusive claim (the boot code, or the RGBR_PLAY effect) currently
 * owns the frame, and whether the link is REJECT. Exposed because the board file
 * asks them to decide whether to run the battery painter at all -- the same two
 * facts the chokepoint itself uses, from one home. */
bool wls_indicator_led_claim_live(void);
bool wls_indicator_led_reject_live(void);

/* The transport confirmation flash is armed from the mode-switch edge; exposed
 * for the lifecycle concern. */
void wls_indicator_wls_set(uint8_t index, RGB rgb, uint32_t interval, uint8_t times);
