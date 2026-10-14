// Copyright 2026
// SPDX-License-Identifier: GPL-2.0-or-later

#include "wls_jump_journal.h"

#include "wls.h"
#include "boot_code_logic.h"

#include "timer.h"

/* In `section(".ram0")`: `__init_ram_areas()` zeroes `.bss` and the pre-noinit
 * part of the RAM area, then stops at `__ram0_noinit__`; a `*(.ram0)` object
 * lands after that and is never touched by startup. So this survives a
 * `NVIC_SystemReset` (what `bootloader_jump()` performs) and gives the next boot
 * a record of the jump. A power-on reset leaves it at its reset value (all
 * zeros), which is why `magic` gates every read. */
static wls_jump_journal_t wls_journal __attribute__((section(".ram0"))) = {0};

const wls_jump_journal_t *wls_jump_journal(void) {
    return &wls_journal;
}

void wls_jump_journal_stamp(void) {
    wls_boot_code_t code = wls_boot_code_now();

    /* Publish the header last, so a torn write cannot leave `magic` set over a
     * half-written record. `generation` advances only on a real jump. */
    wls_journal.version = WLS_JUMP_JOURNAL_VERSION;
    wls_journal.generation++;
    wls_journal.stage       = code.stage;
    wls_journal.code        = code.code;
    wls_journal.reset_cause = wls_reset_cause();
    wls_journal.uptime_ms   = timer_read32();
    wls_journal.magic       = WLS_JUMP_JOURNAL_MAGIC;
}

void wls_jump_journal_encode(const wls_jump_journal_t *j, uint8_t *out) {
    /* The byte mapping is the pure `jump_journal_logic`; this is only the
     * struct-to-parts unpack. */
    wls_jump_journal_encode_parts(j->magic, j->version, j->generation, j->stage, j->code, j->reset_cause, j->uptime_ms, out);
}
