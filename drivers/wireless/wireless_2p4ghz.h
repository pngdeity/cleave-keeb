// Copyright 2026
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdint.h>
#include "report.h"

/**
 * \brief 2.4 GHz wireless driver contract.
 *
 * Mirrors `drivers/bluetooth/bluetooth.h`: a set of weak functions a board
 * overrides to present a 2.4 GHz radio over the upstream `connection` model.
 * The host stack selects this driver when `connection_get_host()` resolves to
 * `CONNECTION_HOST_2P4GHZ`, exactly as it selects `bt_driver` for Bluetooth.
 */

/**
 * \brief Initialize the 2.4 GHz wireless system.
 */
void wireless_2p4ghz_init(void);

/**
 * \brief Perform housekeeping tasks.
 */
void wireless_2p4ghz_task(void);

/**
 * \brief Detects if the 2.4 GHz link is connected.
 *
 * \return `true` if connected, `false` otherwise.
 */
bool wireless_2p4ghz_is_connected(void);

/**
 * \brief Detects if `wireless_2p4ghz_send_nkro` should be used over `wireless_2p4ghz_send_keyboard`.
 */
bool wireless_2p4ghz_can_send_nkro(void);

/**
 * \brief Get current LED state.
 */
uint8_t wireless_2p4ghz_keyboard_leds(void);

/**
 * \brief Send a keyboard report.
 *
 * \param report The keyboard report to send.
 */
void wireless_2p4ghz_send_keyboard(report_keyboard_t *report);

/**
 * \brief Send a nkro report.
 *
 * \param report The nkro report to send.
 */
void wireless_2p4ghz_send_nkro(report_nkro_t *report);

/**
 * \brief Send a mouse report.
 *
 * \param report The mouse report to send.
 */
void wireless_2p4ghz_send_mouse(report_mouse_t *report);

/**
 * \brief Send a consumer usage.
 *
 * \param usage The consumer usage to send.
 */
void wireless_2p4ghz_send_consumer(uint16_t usage);

/**
 * \brief Send a system usage.
 *
 * \param usage The system usage to send.
 */
void wireless_2p4ghz_send_system(uint16_t usage);

/**
 * \brief Send a raw_hid packet.
 *
 * \param data A pointer to the buffer to be sent. Always 32 bytes in length.
 * \param length The length of the buffer. Always 32.
 */
void wireless_2p4ghz_send_raw_hid(uint8_t *data, uint8_t length);
