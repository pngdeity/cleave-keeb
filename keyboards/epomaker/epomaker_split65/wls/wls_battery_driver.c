// Copyright 2026
// SPDX-License-Identifier: GPL-2.0-or-later

#include "battery_driver.h"
#include "wls.h"

/* Upstream's `custom` battery driver contract. The Split65 has no ADC and no
 * charge-controller telemetry; the only level source is the wireless module,
 * which reports it over the module UART and lands in `*md_getp_bat()`.
 * `battery_get_percent()` (quantum/battery) then caches this for the whole
 * firmware, and `battery_percent_changed_user/kb()` fire on change. */
void battery_driver_init(void) {}

uint8_t battery_driver_sample_percent(void) {
    uint8_t bat = *md_getp_bat();
    return bat > 100 ? 100 : bat;
}
