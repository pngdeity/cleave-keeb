// Copyright 2026
// SPDX-License-Identifier: GPL-2.0-or-later

#include "wls.h"

#ifndef VIA_ENABLE

#    include "raw_hid.h"
#    include "usb_descriptor.h"
#    include "sync_timer.h"
#    include "connection.h"

/* Raw HID leaves through the upstream `raw_hid_send()`: it calls
 * `host_raw_hid_send()`, which selects the driver from the connection host
 * (`host_get_active_driver()->send_raw_hid`) — the USB endpoint, or the
 * Bluetooth/2.4 GHz module sink. There is no board-private raw path; the old
 * `replaced_hid_send()` shim and the `md_raw.h` line-pinned alias that fed it
 * only duplicated this and are deleted. */

static void kb_battery_send(const kb_battery_snapshot_t *snap) {
    uint8_t buf[RAW_EPSIZE];

    /* Assemble from one consistent sample of the live sources. */
    memset(buf, 0, RAW_EPSIZE);
    buf[KB_BATTERY_IDX_CMD]       = KB_BATTERY_CMD_GET;
    buf[KB_BATTERY_IDX_LEVEL]     = snap->percent;
    buf[KB_BATTERY_IDX_CHARGE]    = snap->charge;
    buf[KB_BATTERY_IDX_TRANSPORT] = snap->transport;
    buf[KB_BATTERY_IDX_MODEL]     = KB_BATTERY_MODEL_ID;

    /* Selector readback (items 14b/14c): the persisted host and the BT driver's
     * active profile. `connection_get_host_raw()` is the stored value (not the
     * AUTO-resolved one) so a client can see what persistence holds. */
    buf[KB_BATTERY_IDX_HOST]  = (uint8_t)connection_get_host_raw();
    buf[KB_BATTERY_IDX_BTSUB] = bluetooth_get_profile();

    /* Module fingerprint: fw version (`0x5D`) and link state (`0x5B`). These
     * identify the closed module image that carries the Bluetooth faults. */
    buf[KB_BATTERY_IDX_MD_VERSION] = md_get_version();
    buf[KB_BATTERY_IDX_MD_STATE]   = *md_getp_state();

    raw_hid_send(buf, RAW_EPSIZE);
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

    kb_battery_snapshot_t snap;
    kb_battery_snapshot(&snap);
    kb_battery_send(&snap);
}

#    ifdef WLS_BATTERY_PUSH_ENABLE
void kb_battery_push_task(void) {
    static uint32_t       push_timer = 0x00;
    kb_battery_snapshot_t snap;

    if (!is_keyboard_master() || connection_get_host() == CONNECTION_HOST_USB || *md_getp_state() != MD_STATE_CONNECTED) {
        push_timer = 0x00;
        return;
    }

    /* Send when the value changes; otherwise a slow keepalive. This is a
     * battery-powered radio, so an unconditional fast heartbeat is wasteful. */
    kb_battery_snapshot(&snap);
    if (kb_battery_changed(&snap) || sync_timer_elapsed32(push_timer) >= WLS_BATTERY_PUSH_INTERVAL) {
        push_timer = sync_timer_read32();
        kb_battery_send(&snap);
    }
}
#    else
void kb_battery_push_task(void) {}
#    endif

#endif
