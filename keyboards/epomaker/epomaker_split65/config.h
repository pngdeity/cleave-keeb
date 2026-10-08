// Copyright 2024 yangzheng20003 (@yangzheng20003)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#define USB_POWER_EN_PIN                    B1 // USB ENABLE pin
#define LED_POWER_EN_PIN                    A5 // LED ENABLE pin
#define LED_POWER_EN2_PIN                    A8 // LED ENABLE pin
#define HS_BAT_CABLE_PIN                    A7 // USB insertion detection pin

#define BAT_FULL_PIN                        A15
#define BAT_FULL_STATE                      1

/* Battery reporting over raw HID (see PROTOCOL.md) */
#define KB_BATTERY_MODEL_ID 1 // Split65
#define WLS_BATTERY_PUSH_ENABLE
/* Push reloads the value to the host on change; this bounds how stale an
 * unchanged host view may become (a slow keepalive, not the send rate). */
#define WLS_BATTERY_PUSH_INTERVAL 10000

#define MATRIX_ROWS 12
#define MATRIX_COLS 9

#define HS_RGB_INDICATOR_COUNT              99
#define HS_RGB_BAT_COUNT                    1

#define MD_BT1_NAME                         "Split65-1"
#define MD_BT2_NAME                         "Split65-2"
#define MD_BT3_NAME                         "Split65-3"
#define MD_DONGLE_PRODUCT                   "2.4G Dongle"

/* Device Connection RGB Indicator Light Index And Color */
#define HS_RGB_BLINK_INDEX_BT1              17
#define HS_RGB_BLINK_INDEX_BT2              18
#define HS_RGB_BLINK_INDEX_BT3              19
#define HS_RGB_BLINK_INDEX_2G4              20

#define HS_LBACK_COLOR_BT1                  RGB_BLUE
#define HS_LBACK_COLOR_BT2                  RGB_BLUE
#define HS_LBACK_COLOR_BT3                  RGB_BLUE
#define HS_LBACK_COLOR_2G4                  RGB_RED
#define HS_LBACK_COLOR_USB                  RGB_GREEN

#define HS_PAIR_COLOR_BT1                   RGB_BLUE
#define HS_PAIR_COLOR_BT2                   RGB_BLUE
#define HS_PAIR_COLOR_BT3                   RGB_BLUE
#define HS_PAIR_COLOR_2G4                   RGB_RED
#define HS_PAIR_COLOR_USB                   RGB_GREEN

/* Battery */
#define BATTERY_CAPACITY_LOW                15
#define BATTERY_CAPACITY_STOP               0
#define RGB_MATRIX_BAT_INDEX_MAP            {27, 26, 25, 24, 23, 22, 29, 30, 31, 32}

/* Status Indicator Lamp */
#define HS_MATRIX_BLINK_INDEX_BAT           63
#define HS_MATRIX_BAT_SOFT_INDEX            64
#define HS_MATRIX_BAT_SOFT_INDEX2           65
#define HS_RGB_INDEX_CAPS                   2
#define HS_RGB_INDEX_WIN_LOCK               1

#define HS_RGB_BLINK_INDEX_WIN              15
#define HS_RGB_BLINK_INDEX_MAC              14

/* UART */
#define SERIAL_DRIVER                       SD3
#define SD1_TX_PIN                          C10
#define SD1_RX_PIN                          C11

/* The current ChibiOS uart_serial driver reads UART_* directly; the old
 * SERIAL_DRIVER -> UART_DRIVER and SD1_* -> UART_* alias layer was removed
 * upstream. Without UART_DRIVER the module UART would default to SD1, which
 * collides with the split link below (also SD1). Keep it on SD3 (UART3). */
#define UART_DRIVER                         SD3
#define UART_TX_PIN                         C10
#define UART_RX_PIN                         C11
#define UART_TX_PAL_MODE                    7
#define UART_RX_PAL_MODE                    7

#define SERIAL_USART_DRIVER SD1
#define SERIAL_USART_TX_PIN A9
#define SERIAL_USART_RX_PIN A10
#define SERIAL_USART_TX_PAL_MODE 7
#define SERIAL_USART_RX_PAL_MODE 7
#define SERIAL_USART_CONFIG {115200, 3, 0, 0, 0};
#define SERIAL_USART_FULL_DUPLEX
#define SELECT_SOFT_SERIAL_SPEED 1
#define SERIAL_DEBUG

#define SPLIT_TRANSACTION_IDS_USER USER_SYNC_MMS    //multimode status

/* SPLIT_WATCHDOG_ENABLE is deliberately NOT defined. Upstream's watchdog
 * reboots a slave via mcu_reset() after SPLIT_WATCHDOG_TIMEOUT on a missed
 * master ping, but its slave-side "done" flag is refreshed only by a
 * one-way ping the master emits while the master's own flag is clear, so on
 * this board the slave could never re-arm and reset-looped (~3 s), killing
 * the backlight. The vendor firmware never enabled it. Not needed for any
 * functional requirement; leave off. */

/* Encoder */
#define ENCODER_MAP_KEY_DELAY               1

/* SPI */
#define SPI_DRIVER                          SPIDQ
#define SPI_SCK_PIN                         B3
#define SPI_MOSI_PIN                        B5
#define SPI_MISO_PIN                        B4

/* Flash */
#define EXTERNAL_FLASH_SPI_SLAVE_SELECT_PIN C12
/* WEAR_LEVELING_BACKING_SIZE is set in keyboard.json (eeprom.wear_leveling). */

/* RGB Matrix */
#define RGB_MATRIX_FRAMEBUFFER_EFFECTS
#define RGB_MATRIX_KEYPRESSES

/* Default lighting lives in keyboard.json (`rgb_matrix.default`): a solid
 * comfortable white at half brightness rather than the vendor's rainbow wave.
 * Do not define RGB_MATRIX_DEFAULT_* here — config.h and keyboard.json would
 * then disagree and `qmk lint` warns on the duplicate. */

/* WS2812 */
#define WS2812_SPI_DRIVER  SPIDM2
#define WS2812_SPI_DIVISOR 32

/* rgb_record */
#define ENABLE_RGB_MATRIX_RGBR_PLAY
#define RGBREC_CHANNEL_NUM         4
#define EECONFIG_CONFINFO_USE_SIZE (4 + 16)
#define EECONFIG_RGBREC_USE_SIZE   (RGBREC_CHANNEL_NUM * MATRIX_ROWS * MATRIX_COLS * 2)
#define EECONFIG_USER_DATA_SIZE    (EECONFIG_RGBREC_USE_SIZE + EECONFIG_CONFINFO_USE_SIZE)
#define RGBREC_EECONFIG_ADDR       (uint8_t *)(EECONFIG_USER_DATABLOCK)
#define CONFINFO_EECONFIG_ADDR     (uint32_t *)((uint32_t)RGBREC_EECONFIG_ADDR + (uint32_t)EECONFIG_RGBREC_USE_SIZE)
