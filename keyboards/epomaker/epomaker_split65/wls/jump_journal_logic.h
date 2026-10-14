// Copyright 2026
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdint.h>

/* The pure encoder for the jump journal (`wls/wls_jump_journal`).
 *
 * The journal's *storage* is a shell concern: it lives in `section(".ram0")` so
 * it survives the reset a `bootloader_jump()` performs, and the shell stamps it
 * from live state. Mapping that record to the wire bytes the host reads is a
 * pure rule, so it lives here with no include beyond `stdint.h` -- it can be
 * tested on the host, and the host decoder's golden vector pins it (the F9
 * shape).
 *
 * `present` is the discriminator: 1 only if `magic` matches, i.e. this board
 * wrote the journal at least once since power-on. `version`, `generation`,
 * `stage`, `code`, `reset_cause` and `uptime_ms` are reported raw.
 * Layout (WLS_JUMP_JOURNAL_ENC_SIZE bytes). The widths are what the report has
 * room for, not the struct's: the journal block must fit the `0xA4` report after
 * the boot-code pair, so `reset_cause` is one byte (its latch has six bits) and
 * `uptime_ms` is three (little-endian). See `wls/wls.h` for the block offset.
 *
 *   [0 present][1 version][2 generation][3 stage][4 code]
 *   [5 reset_cause][6..8 uptime_ms] */

#define WLS_JUMP_JOURNAL_ENC_SIZE 9

/* `magic` distinguishes "this journal has been written at least once since
 * power-on" from the arbitrary RAM a cold boot leaves. A reader that sees this
 * value knows the record is live. Defined here so the pure encoder and the
 * shell storage share one constant. */
#define WLS_JUMP_JOURNAL_MAGIC 0x4A4D5052u /* "JMPR" */

/* Encode `magic`/`version`/`generation`/`stage`/`code`/`reset_cause`/`uptime_ms`
 * into `out` (at least WLS_JUMP_JOURNAL_ENC_SIZE bytes). Pure. */
void wls_jump_journal_encode_parts(uint32_t magic, uint8_t version, uint8_t generation, uint8_t stage, uint8_t code, uint32_t reset_cause, uint32_t uptime_ms, uint8_t *out);
