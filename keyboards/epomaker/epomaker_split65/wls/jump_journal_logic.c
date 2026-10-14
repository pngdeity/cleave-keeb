// Copyright 2026
// SPDX-License-Identifier: GPL-2.0-or-later

#include "jump_journal_logic.h"

void wls_jump_journal_encode_parts(uint32_t magic, uint8_t version, uint8_t generation, uint8_t stage, uint8_t code, uint32_t reset_cause, uint32_t uptime_ms, uint8_t *out) {
    out[0]  = (magic == WLS_JUMP_JOURNAL_MAGIC) ? 1u : 0u;
    out[1]  = version;
    out[2]  = generation;
    out[3]  = stage;
    out[4]  = code;
    out[5]  = (uint8_t)(reset_cause & 0xFFu);
    out[6]  = (uint8_t)((reset_cause >> 8) & 0xFFu);
    out[7]  = (uint8_t)((reset_cause >> 16) & 0xFFu);
    out[8]  = (uint8_t)((reset_cause >> 24) & 0xFFu);
    out[9]  = (uint8_t)(uptime_ms & 0xFFu);
    out[10] = (uint8_t)((uptime_ms >> 8) & 0xFFu);
    out[11] = (uint8_t)((uptime_ms >> 16) & 0xFFu);
}
