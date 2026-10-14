// Copyright 2026
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

/* The board's single gate in front of the vendor wireless module.
 *
 * `wireless_devs_change()` looks like a plain setter, but it reaches the
 * module's protocol ring buffer (`md_devs_change` -> `md_send_devctrl` ->
 * `smsg_push`), and that ring is initialized only by `md_init()` (from
 * `wireless_init()`, which the board calls at boot step 33). Before then the
 * ring is zeroed statics: `smsg_push()`'s guards all pass and it copies into a
 * NULL `buffer`, HardFaulting the MCU mid-`keyboard_init()` and leaving the
 * board pointed up on the bus with no application running.
 *
 * That is not a hazard a caller can be trusted to remember. **This module owns
 * the "the module is up" fact and refuses the change before it.** Every board
 * call site goes through `wls_module_devs_change()`, so a caller that runs too
 * early is a no-op rather than a fault -- for the present call sites and any
 * future one. `wls_module_ready()` is the single place the fact is set.
 *
 * The gate is stated purely (`hsm_module_may_act`) and tested, so the rule is
 * pinned rather than living in the shell. */

/* Mark the module up. Called once, immediately after `wireless_init()`. */
void wls_module_ready(void);

/* Change the vendor device index, if the module is up. A no-op otherwise.
 * `old_devs` is what the caller believes is current; `new_devs` the target; the
 * vendor change sends its own command, so `old_devs` only feeds the vendor's
 * bookkeeping. */
void wls_module_devs_change(uint8_t old_devs, uint8_t new_devs, bool reset);

/* The board's view of the vendor device index, current even while the module is
 * still down (a caller may read it before it can act). */
uint8_t wls_module_devs(void);
