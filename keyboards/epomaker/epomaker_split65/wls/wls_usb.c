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

/* Whether THIS half has the host cable, from the settled `B9` read.
 *
 * `B9` is the board's USB cable-detect pin ("high when plugged",
 * `docs/HARDWARE.md`), and the same pin the core uses for handedness. The core's
 * `is_keyboard_left_impl()` settles the line before reading it (`wait_us(100)`,
 * `quantum/split_common/split_util.c`); this does the same, because an un-settled
 * read of a pull-up line can sample the old floating level and lie -- which is
 * exactly the defect this fact exists to correct.
 *
 * Declared here rather than beside the switch read because it is a USB fact: it
 * answers "is there a host on this half", not "what did the switch say". */
bool wls_usb_cable_present(void) {
#ifdef SPLIT_HAND_PIN
    gpio_set_pin_input(SPLIT_HAND_PIN);
    wait_us(100);
    return gpio_read_pin(SPLIT_HAND_PIN);
#else
    return false;
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
        /* A confirmed host is never torn down.
         *
         * `USB_ACTIVE` means the peripheral enumerated and the host is talking
         * to us: the transport rule says a confirmed host outranks every hint,
         * so a teardown command issued while confirmed is by definition wrong.
         * Without this gate a hint-level caller could cut the line the
         * confirmation just established, and the board would enumerate and
         * self-detach about once a second (the livelock this exists to
         * prevent). Gating the *actuator* rather than each caller makes every
         * path -- present and future -- safe at once. */
        if (usb_connected_state()) {
            return;
        }
        if (USB_DRIVER.state != USB_STOP) {
            usbDisconnectBus(&USBD1);
            usbStop(&USBD1);
            usb_power_disconnect();
        }
#endif
    }
}
