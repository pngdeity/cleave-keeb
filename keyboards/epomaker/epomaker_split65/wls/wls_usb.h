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
 * callers are the three sites that act on a *change of the committed transport*
 * -- the boot seed, the +100 ms re-seed, and the USB lifecycle event -- each
 * driving the line only when `wls_transport_decide()` reports a change. The line
 * follows an *intent*, never an observation of itself: the earlier contract,
 * where `connection_host_changed_kb()` drove it from the live resolved host, made
 * every oscillation of that host a physical teardown and livelocked the board
 * against its own enumeration (see `docs/ADR-transport-authority.md`). A defect
 * here is observable through `wls_diag`'s `usb_line` field, which is why it was
 * extracted before the self-detach bug was chased (the seam exists to instrument
 * exactly this). */

/* Drive the board's USB VBUS control pin on/off (`USB_POWER_EN_PIN`). */
void usb_power_connect(void);
void usb_power_disconnect(void);

/* Connect or disconnect the USB data line. On connect, re-assert
 * `last_suspend_state` (the flag is not reliably set after a bus restart); on
 * disconnect, tear the driver down first. Guarded by
 * `KEEP_USB_CONNECTION_IN_WIRELESS_MODE`: when that is defined the line is left
 * alone and this is a no-op. */
void wls_usb_connect(bool enable);

/* Whether THIS half has the host cable, from the settled `B9` read. This is the
 * independent signal the transport rule checks the switch against: a cable is a
 * physical fact and outranks a switch preference. Settled with the same
 * `wait_us(100)` the core uses for the same pin. */
bool wls_usb_cable_present(void);
