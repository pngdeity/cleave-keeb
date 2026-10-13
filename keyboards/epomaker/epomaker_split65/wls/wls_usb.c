// Copyright 2026
// SPDX-License-Identifier: GPL-2.0-or-later

#include "wls_usb.h"

#include "quantum.h"
#include "usb_util.h"
#include "usb_main.h"

void usb_power_connect(void) {
#ifdef USB_POWER_EN_PIN
    gpio_write_pin_low(USB_POWER_EN_PIN);
#endif
}

void usb_power_disconnect(void) {
#ifdef USB_POWER_EN_PIN
    gpio_write_pin_high(USB_POWER_EN_PIN);
#endif
}

void wls_usb_connect(bool enable) {
    extern bool last_suspend_state;

    if (enable) {
        if (!usb_connected_state()) {
            last_suspend_state = true;
#if !defined(KEEP_USB_CONNECTION_IN_WIRELESS_MODE)
            usb_power_connect();
            restart_usb_driver(&USBD1);
#endif
        }
    } else {
#if !defined(KEEP_USB_CONNECTION_IN_WIRELESS_MODE)
        if (USB_DRIVER.state != USB_STOP) {
            usbDisconnectBus(&USBD1);
            usbStop(&USBD1);
            usb_power_disconnect();
        }
#endif
    }
}
