// Copyright 2026
// SPDX-License-Identifier: GPL-2.0-or-later

#include "jump_journal_logic.h"

void wls_jump_journal_encode_parts(uint32_t magic, uint8_t version, uint8_t generation, uint8_t stage, uint8_t code, uint32_t reset_cause, uint32_t uptime_ms, uint8_t *out) {
    out[0] = (magic == WLS_JUMP_JOURNAL_MAGIC) ? 1u : 0u;
    out[1] = version;
    out[2] = generation;
    out[3] = stage;
    out[4] = code;
    /* The reset cause is a six-bit latch (`RCC->RSTSTAT` bits 0..5), so only the
     * low byte is ever meaningful -- the same width the diag block reports. */
    out[5] = (uint8_t)(reset_cause & 0xFFu);
    /* Uptime is reported as 24 bits: the wrap at ~4.6 hours is far longer than a
     * diagnostic needs, and the byte it saves is what lets the record fit the
     * `0xA4` report. */
    out[6] = (uint8_t)(uptime_ms & 0xFFu);
    out[7] = (uint8_t)((uptime_ms >> 8) & 0xFFu);
    out[8] = (uint8_t)((uptime_ms >> 16) & 0xFFu);
}
