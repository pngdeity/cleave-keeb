// Copyright 2026
// SPDX-License-Identifier: GPL-2.0-or-later

/* The board's strong override of the weak `bootloader_jump()`.
 *
 * Why this exists. A `dfa0` window means our `QK_BOOT` path -- or something
 * else -- set the DFU magic word at `__ram0_end__ - 4` and reset. The word
 * itself is gone by the time any of our code runs: the reset handler's
 * `enter_bootloader_mode_if_requested()` clears it (`*check = 0`) before the
 * first line of ours. So the only way to tell "our `QK_BOOT` ran" from "the
 * word appeared with no stamp" is to record something of our own that outlives
 * a reset.
 *
 * `wls_jump_journal` lives in `section(".ram0")`, which the ChibiOS startup
 * never clears (only a power-on reset does). This file stamps that journal at
 * the one place the board ever intends to jump, then performs the jump exactly
 * as upstream does.
 *
 * Upstream's `bootloader_jump()` is `__attribute__((weak))` (in
 * `platforms/chibios/bootloaders/wb32_dfu.c`), so this strong definition
 * replaces it with no upstream edit. The only caller is our own
 * `wls_keymap.c` `QK_BOOT` arm, which keeps calling `bootloader_jump()` and so
 * reaches this body transparently.
 *
 * The two statements upstream's weak body holds (the magic write and the reset)
 * are deliberately not copied: the magic word's address and value are
 * file-local to `wb32_dfu.c`, and duplicating them here would create a second
 * statement of one fact -- the drift class this project keeps finding. Instead
 * this body writes the same word at the same address and then calls the real,
 * strong `enter_bootloader_mode_if_requested()`, which performs the actual jump
 * into `WB32_BOOTLOADER_ADDRESS`. That call is what upstream's reset would have
 * produced a moment later, and it reuses upstream's jump logic verbatim. */

#include "bootloader.h"

#include "wls_jump_journal.h"

extern uint32_t __ram0_end__;

/* Upstream declares this only as a local prototype inside
 * `tmk_core/protocol/chibios/chibios.c:108`; no header carries it. It is the
 * strong (non-weak) definition in `platforms/chibios/bootloaders/wb32_dfu.c`
 * that performs the actual jump into `WB32_BOOTLOADER_ADDRESS`. */
void enter_bootloader_mode_if_requested(void);

#define WLS_BOOTLOADER_MAGIC 0xDEADBEEF
#define WLS_MAGIC_ADDR ((volatile unsigned long *)((uint32_t)&__ram0_end__ - 4u))

void bootloader_jump(void) {
    /* Record what we are about to do, and where the board was when it decided
     * to. Published into `.ram0`, so the next boot can read it. */
    wls_jump_journal_stamp();

    /* The DFU magic word the bootloader entry checks for, then the jump. */
    *WLS_MAGIC_ADDR = WLS_BOOTLOADER_MAGIC;
    enter_bootloader_mode_if_requested();
}
