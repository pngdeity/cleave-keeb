// Copyright 2026
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include QMK_KEYBOARD_H

#include "quantum.h"
#include <stdbool.h>
#include <stdint.h>

/* Persisted board configuration (the `confinfo` EEPROM block) and the split
 * serial port, as one concern.
 *
 * This is the board's durable intent: the user's transport choice and BT
 * sub-index, the RGB-record settings, the macOS/Windows flip, the direction
 * flag. It is *intent*, not live state — the live transport authority is
 * `wireless_get_current_devs()` (see `docs/FINDINGS.md`); nothing here is read
 * to decide the current transport. Kept behind this interface so the storage,
 * the version gate, and the default values live in one place instead of being
 * spread through the board glue. */

/* The block layout. A union so the whole thing can be persisted as one word and
 * the version gate can reject a block written by a different firmware. */
typedef union {
    uint32_t raw;
    struct {
        uint8_t flag : 1;
        uint8_t devs : 3;
        uint8_t record_channel : 4;
        uint8_t record_last_mode;
        uint8_t last_btdevs : 3;
        uint8_t dir_flag : 1;
        uint8_t filp : 1;
        uint8_t last_wireless_devs : 3;
        uint8_t version : 8;
    };
} confinfo_t;

/* Bump when a field's meaning or position changes. An EEPROM block written by
 * an older firmware then fails the version gate and is re-defaulted once, which
 * is deterministic where a per-field heuristic is not. */
#define CONFINFO_VERSION 1

/* The one instance. Exposed because the board reads and writes individual
 * fields (transport intent, the flip flag) at many sites; the storage itself
 * stays owned here. */
extern confinfo_t confinfo;

/* Persist the block. The single write path — every mutation ends here. */
void eeconfig_confinfo_update(uint32_t raw);

/* Read the block straight from EEPROM (no gate, no default). */
uint32_t eeconfig_confinfo_read(void);

/* Reset the block to its defaults and persist them. */
void eeconfig_confinfo_default(void);

/* Load the block and apply the version gate: a blank block, or one written by a
 * different firmware version, is re-defaulted. Called once at boot. */
void eeconfig_confinfo_init(void);

/* Configure the split serial port (the module UART is a separate driver). */
void usart_init(void);

/* The persisted BT device index to fall back on when the switch is unreadable.
 * Lives here because it reads `confinfo` (the storage this concern owns); the
 * transport owner and the boot pass both use it. */
uint8_t last_btdev_from_confinfo(void);

/* Wipe the board's persisted configuration to defaults: re-initialise the
 * EEPROM, restore the rgb-matrix and rgblight defaults, re-read the keymap and
 * clear NKRO. This is the settings-reset's `WIPE_CONFIG` step, owned here so the
 * reset executor (wls_lifecycle) does not have to know how the board's storage
 * is laid out. Idempotent and side-effect-only. */
void wls_eeprom_wipe_config(void);
