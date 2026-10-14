// Copyright 2026
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "jump_journal_logic.h"

/* A cross-reset record of the last `bootloader_jump()` this board performed.
 *
 * `enter_bootloader_mode_if_requested()` CLEARS the DFU magic word (`*check = 0`)
 * before any of our code runs, so on the boot after a jump the value itself is
 * gone and a `dfa0` window cannot say who set it. This journal is the answer: it
 * lives in `section(".ram0")`, which `__init_ram_areas()` never zeroes, so it
 * survives a `NVIC_SystemReset` (but not a power-on reset).
 *
 * The board overrides the weak `bootloader_jump()` to stamp the journal *before*
 * the jump. The next boot can then tell the two cases apart:
 *
 *   - a `0xA4` reading with `stamp` non-zero and `generation` advanced => our
 *     own `QK_BOOT` path drove the jump (which the Stage-3 analysis says needs a
 *     physical key, so this would falsify that conclusion); and
 *   - a `dfa0` window with the `generation` unchanged => the magic word was
 *     written by something that is not our jump path.
 *
 * The struct is a fixed, verified layout: the host reads `KB_BATTERY_IDX_JUMP_*`
 * bytes and the golden vector pins them, so the two sides cannot drift (the F9
 * shape). */

/* Bumped when the journal layout changes; a reader that does not know this
 * version should treat every field as opaque. */
#define WLS_JUMP_JOURNAL_VERSION 1u

typedef struct {
    uint32_t magic;       /* WLS_JUMP_JOURNAL_MAGIC once the journal is live */
    uint8_t  version;     /* WLS_JUMP_JOURNAL_VERSION */
    uint8_t  generation;  /* increments on each jump we perform */
    uint8_t  stage;       /* `wls_boot_code_t.stage` at the moment of the jump */
    uint8_t  code;        /* `wls_boot_code_t.code` at the moment of the jump */
    uint32_t reset_cause; /* `wls_reset_cause()` bits at the moment of the jump */
    uint32_t uptime_ms;   /* `timer_read32()` at the moment of the jump */
} wls_jump_journal_t;

/* How many bytes `wls_jump_journal_encode()` writes (8.., see below). Fixed so
 * the `0xA4` host reader and the firmware share one length. */
#define WLS_JUMP_JOURNAL_ENC_SIZE 12

/* The live journal (in `section(".ram0")`). Read-only for callers; written only
 * by `wls_jump_journal_stamp()`. */
const wls_jump_journal_t *wls_jump_journal(void);

/* Record a jump about to happen. Called by the board's strong
 * `bootloader_jump()` override, immediately before the weak implementation sets
 * the magic word and resets. Idempotent for a jump (it always advances
 * `generation`). */
void wls_jump_journal_stamp(void);

/* Encode the journal into `WLS_JUMP_JOURNAL_ENC_SIZE` bytes at `out`:
 * `[magic?1][version][generation][stage][code][reset_cause x4][uptime_ms x4]`.
 * Pure: maps a struct to bytes and decides nothing. The caller passes the live
 * journal (`wls_jump_journal()`) or, in a test, a fixture. */
void wls_jump_journal_encode(const wls_jump_journal_t *j, uint8_t *out);
