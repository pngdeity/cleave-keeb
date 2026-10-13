// Copyright 2026
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>

/* The USB data line as an explicit, owned actuator.
 *
 * Before item 14d the vendor `set_transport()` drove the USB line by swapping
 * `host_driver_t`; with that swap gone the board owns the line directly. The
 * driver *choice* is upstream's (`host_get_active_driver()`), so this concern
 * touches no driver pointer — it only connects or disconnects the physical USB
 * data line, and drives the board's VBUS control pin.
 *
 * This is a leaf concern: it depends on nothing else in the board layer. Its
 * caller is `connection_host_changed_kb()` (upstream fires it when
 * `config.desired_host` changes), which is the single point that decides whether
 * the line should be up. A defect here is observable through `wls_diag`'s
 * `usb_line` field, which is why it was extracted before the self-detach bug was
 * chased (the seam exists to instrument exactly this). */

/* Drive the board's USB VBUS control pin on/off (`USB_POWER_EN_PIN`). */
void usb_power_connect(void);
void usb_power_disconnect(void);

/* Connect or disconnect the USB data line. On connect, re-assert
 * `last_suspend_state` (the flag is not reliably set after a bus restart); on
 * disconnect, tear the driver down first. Guarded by
 * `KEEP_USB_CONNECTION_IN_WIRELESS_MODE`: when that is defined the line is left
 * alone and this is a no-op. */
void wls_usb_connect(bool enable);
