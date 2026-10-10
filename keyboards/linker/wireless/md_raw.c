// Copyright 2024 QMK
// SPDX-License-Identifier: GPL-2.0-or-later

#if defined(RAW_ENABLE)

#    include "quantum.h"
#    include "raw_hid.h"
#    include "wireless.h"

/* The receive path for a raw HID frame tunneled over the module link: the
 * module decoder forwards it to `raw_hid_receive()`, exactly as the USB path
 * does. The send path needs no board glue -- `raw_hid_send()` (upstream) calls
 * `host_raw_hid_send()`, which selects the driver from the connection host, so
 * the USB endpoint and the Bluetooth/2.4 GHz module sinks are reached the same
 * way every other report is. */
void md_receive_raw_cb(uint8_t *data, uint8_t length) {
    raw_hid_receive(data, length);
}

#endif
