// Copyright 2026
// SPDX-License-Identifier: GPL-2.0-or-later

#include "wls_module.h"

#include "wls.h"
#include "transport_logic.h"

#include "quantum.h"
#include "wireless.h"

/* The board's view of the vendor device index. Kept here rather than read from
 * the vendor (`wireless_get_current_devs()`) so a caller that runs before the
 * module is up still gets the board's intent, and so the index has one owner on
 * this side of the edge. */
static uint8_t devs = DEVS_USB;

/* Whether the module is up. Set once, by `wls_module_ready()`. */
static bool ready = false;

void wls_module_ready(void) {
    ready = true;
}

void wls_module_devs_change(uint8_t old_devs, uint8_t new_devs, bool reset) {
    /* Refuse before the module exists. `wireless_devs_change` reaches the
     * module's ring buffer, which is zeroed statics until `wireless_init()`:
     * the guards pass and it copies into a NULL buffer, HardFaulting the MCU
     * (see the header). A caller that runs too early must be a no-op. */
    if (!hsm_module_may_act(ready)) {
        return;
    }
    (void)old_devs;
    devs = new_devs;
    wireless_devs_change(wireless_get_current_devs(), new_devs, reset);
}

uint8_t wls_module_devs(void) {
    return devs;
}
