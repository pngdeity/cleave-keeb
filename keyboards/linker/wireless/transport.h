// Copyright 2024 Su (@isuua)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

/* The vendor transport selector is gone (item 14d): the active driver is
 * upstream's `host_get_active_driver()`, and the physical USB connect/
 * disconnect is the board's `wls_usb_connect()`. Only the USB power helpers
 * and remote wakeup remain, implemented by the board and `transport.c`. */
void usb_power_connect(void);
void usb_power_disconnect(void);
void usb_remote_wakeup(void);
