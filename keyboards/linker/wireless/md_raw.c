// Copyright 2024 QMK
// SPDX-License-Identifier: GPL-2.0-or-later

#if defined(RAW_ENABLE)

#    include "quantum.h"
#    include "raw_hid.h"
#    include "wireless.h"
#    include "usb_endpoints.h"
#    include "usb_main.h"
#    include "host.h"

void replaced_hid_send(uint8_t *data, uint8_t length) {

    if (length != RAW_EPSIZE) {
        return;
    }

    /* Raw HID follows the same active-driver resolution as every other report
     * (item 14d): `host_raw_hid_send()` picks the driver from the connection
     * host, so on USB it writes the raw endpoint and on Bluetooth/2.4 GHz it
     * reaches the module sink through the driver's `send_raw_hid`. Branching on
     * `usb_connected_state()` (the bus state) would diverge from every other
     * report path, which keys off `connection_get_host()`. */
    host_raw_hid_send(data, length);
}

void md_receive_raw_cb(uint8_t *data, uint8_t length) {
    raw_hid_receive(data, length);
}

#endif
