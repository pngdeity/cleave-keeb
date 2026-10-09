// Copyright 2024 Su (@isuua)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "quantum.h"
#include "module.h"
#include "usb_main.h"
#include "usb_device_state.h"
#include "connection.h"

#ifndef USB_POWER_DOWN_DELAY
#    define USB_POWER_DOWN_DELAY 3000
#endif

/* The two USB functions the shared stack still needs after the vendor
 * `set_transport()` driver swap was deleted (item 14d). `usb_remote_wakeup()`
 * is called from `wireless_task()`; `process_action_kb()` requests a remote
 * wake on keypress while the active host is USB. The driver choice is
 * upstream's (`host_get_active_driver()`), so nothing here touches a driver
 * pointer, and the physical connect/disconnect lives in the board's
 * `wls_usb_connect()`. */

void usb_remote_wakeup(void) {

#ifdef USB_REMOTE_USE_QMK
    if (USB_DRIVER.state == USB_SUSPENDED) {
        dprintln("suspending keyboard");
        while (USB_DRIVER.state == USB_SUSPENDED) {
            /* Do this in the suspended state */
            suspend_power_down(); // on AVR this deep sleeps for 15ms
            /* Remote wakeup */
            if ((USB_DRIVER.status & 2U) && suspend_wakeup_condition()) {
                usbWakeupHost(&USB_DRIVER);
#    if USB_SUSPEND_WAKEUP_DELAY > 0
                // Some hubs, kvm switches, and monitors do
                // weird things, with USB device state bouncing
                // around wildly on wakeup, yielding race
                // conditions that can corrupt the keyboard state.
                //
                // Pause for a while to let things settle...
                wait_ms(USB_SUSPEND_WAKEUP_DELAY);
#    endif
            }
        }
        /* Woken up */
    }
#else
    static uint32_t suspend_timer = 0x00;

    if ((USB_DRIVER.state == USB_SUSPENDED)) {
        if (!suspend_timer) suspend_timer = sync_timer_read32();
        if (sync_timer_elapsed32(suspend_timer) >= USB_POWER_DOWN_DELAY) {
            suspend_timer = 0x00;
            suspend_power_down();
        }
    } else {
        suspend_timer = 0x00;
    }
#endif
}

#ifndef USB_REMOTE_USE_QMK
void usb_remote_host(void) {

    /* This runs from process_action_kb() on every action record while on USB.
     * The genuine wake transition is handled once by the QMK USB core: the
     * USB_EVENT_WAKEUP path enqueues the event from the USB ISR and
     * usb_event_queue_task() -> usb_event_wakeup_handler() calls
     * suspend_wakeup_init() in the main loop (protocol_pre_task), which runs
     * here regardless of NO_USB_STARTUP_CHECK. So this function only needs to
     * request the remote wakeup; it must NOT call suspend_wakeup_init() itself,
     * or it would re-run the whole wake init on every keypress while the host
     * keeps the bus suspended. */
    if (USB_DRIVER.state == USB_SUSPENDED) {
        if ((USB_DRIVER.status & 2U) && suspend_wakeup_condition()) {
            usbWakeupHost(&USB_DRIVER);
#    if USB_SUSPEND_WAKEUP_DELAY > 0
            // Some hubs, kvm switches, and monitors do
            // weird things, with USB device state bouncing
            // around wildly on wakeup, yielding race
            // conditions that can corrupt the keyboard state.
            //
            // Pause for a while to let things settle...
            wait_ms(USB_SUSPEND_WAKEUP_DELAY);
#    endif
        }
    }
}

bool process_action_kb(keyrecord_t *record) {

    (void)record;
    if (connection_get_host() == CONNECTION_HOST_USB) {
        usb_remote_host();
    }

    return true;
}
#endif