// Copyright 2026
// SPDX-License-Identifier: GPL-2.0-or-later

#include "wls.h"

#ifndef VIA_ENABLE

#    include "raw_hid.h"
#    include "usb_descriptor.h"
#    include "sync_timer.h"

/* The wireless stack tunnels raw HID through md_raw.c's replaced_hid_send()
 * instead of raw_hid_send(): on USB it writes the raw endpoint, otherwise it
 * forwards the report over the 2.4G/BT link. md_raw.h remaps raw_hid_send to
 * replaced_hid_send via line-specific macros, so this new file must call it
 * directly. */
void replaced_hid_send(uint8_t *, uint8_t);

static void kb_battery_send(void) {
    uint8_t buf[RAW_EPSIZE];

    kb_battery_report_fill(buf);
    replaced_hid_send(buf, RAW_EPSIZE);
}

void raw_hid_receive(uint8_t *data, uint8_t length) {
    /* Reply only to our command; anything else must draw no response, as an
     * unsolicited report would collide with other raw HID clients. */
    if (length < 1 || data[0] != KB_BATTERY_CMD_GET) {
        return;
    }

    if (!is_keyboard_master()) {
        return;
    }

    kb_battery_send();
}

#    ifdef WLS_BATTERY_PUSH_ENABLE
void kb_battery_push_task(void) {
    static uint32_t push_timer = 0x00;

    if (!is_keyboard_master() || get_transport() == TRANSPORT_USB || *md_getp_state() != MD_STATE_CONNECTED) {
        push_timer = 0x00;
        return;
    }

    if (sync_timer_elapsed32(push_timer) < WLS_BATTERY_PUSH_INTERVAL) {
        return;
    }
    push_timer = sync_timer_read32();

    kb_battery_send();
}
#    else
void kb_battery_push_task(void) {}
#    endif

#endif
