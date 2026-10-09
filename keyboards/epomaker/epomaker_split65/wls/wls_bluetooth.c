// Copyright 2026
// SPDX-License-Identifier: GPL-2.0-or-later

#include "bluetooth.h"
#include "wls.h"

/* Upstream's `custom` Bluetooth driver contract (drivers/bluetooth/bluetooth.h).
 * This board's Bluetooth is a third-party UART module driven over the vendor
 * `module.c`/`smsg.c` protocol, selected by the physical mode switch, not an
 * RN-42 or Bluefruit. So the driver is a thin adapter: it presents the module
 * stack through the upstream contract, the same shape `wls_battery_driver.c`
 * uses for BATTERY_DRIVER = custom.
 *
 * The transport is owned by upstream's connection model, whose
 * `host_get_active_driver()` returns `bt_driver` when the active host is
 * CONNECTION_HOST_BLUETOOTH — so these functions are the path every report
 * takes while the module is on a Bluetooth profile. (The vendor `set_transport()`
 * that used to swap `host_driver_t` was deleted in item 14d.) */

/* The BT profile sub-index (BT1..BT5) is a property of this driver, not a
 * transport. Upstream's `connection` model can express "send over Bluetooth"
 * (CONNECTION_HOST_BLUETOOTH) but not *which* profile, so owning the index here
 * keeps the vendor `confinfo.devs` from acting as a second transport selector
 * (item 14b). `md_devs_change()` is the module-facing primitive that carries
 * the profile to the CH582F (DEVCTRL_BT1..BT5, plus CLEAN+devinfo+PAIR on
 * re-pair), so the driver's notion of "current profile" and the module's stay
 * in lockstep without a second persisted selector. */
static uint8_t wls_bt_profile = DEVS_BT1;

static bool wls_is_bt_devs(uint8_t devs) {
    return devs >= DEVS_BT1 && devs <= DEVS_BT5;
}

static bool wls_on_bt_profile(void) {
    return wls_is_bt_devs(wireless_get_current_devs());
}

void bluetooth_select_profile(uint8_t profile, bool reset) {
    if (!wls_is_bt_devs(profile)) {
        profile = DEVS_BT1;
    }

    wls_bt_profile = profile;

    /* Change the *device index*, not just the module. `wireless_devs_change()`
     * sets `wls_devs` (so `wireless_get_current_devs()` and the vendor device
     * index follow the profile), resets the module link state, pushes the
     * profile to the module via `md_devs_change()`, and drives the indicator
     * through `wireless_devs_change_kb()`. Calling `md_devs_change()` directly
     * (as this did) told the module but left `wls_devs` on the old profile, so
     * the left-half LED kept blinking the original profile (BT1 → Q) forever —
     * the profile selection was invisible and unobservable.
     *
     * `reset` preserves the vendor distinction: false = select the profile,
     * true = re-pair it (CLEAN + devinfo + PAIR on the module). A short keycode
     * tap selects; a long press re-pairs. */
    wireless_devs_change(wireless_get_current_devs(), profile, reset);
}

uint8_t bluetooth_get_profile(void) {
    return wls_bt_profile;
}

static bool wls_bt_link_up(void) {
    return wls_on_bt_profile() && *md_getp_state() == MD_STATE_CONNECTED;
}

/* The vendor send path pokes the module stack when a report arrives while the
 * link is down, so the stack re-detects/re-pairs instead of silently dropping
 * input. Preserved here so behaviour matches the vendor driver; only done on a
 * BT profile so a stray send cannot reset USB or 2.4 GHz state. */
static void wls_bt_poke_if_down(void) {
    if (wls_on_bt_profile() && *md_getp_state() != MD_STATE_CONNECTED) {
        wireless_devs_change(wireless_get_current_devs(), wireless_get_current_devs(), false);
    }
}

void bluetooth_init(void) {}

void bluetooth_task(void) {}

bool bluetooth_is_connected(void) {
    /* Gated on the current device being a BT profile on purpose. Upstream's
     * connection_auto_detect_host() calls this to resolve CONNECTION_HOST_AUTO;
     * the module UART reports MD_STATE_CONNECTED on 2.4 GHz and USB too, so
     * without this gate AUTO would resolve to Bluetooth on every transport. */
    return wls_bt_link_up();
}

bool bluetooth_can_send_nkro(void) {
    return true;
}

uint8_t bluetooth_keyboard_leds(void) {
    if (!wls_bt_link_up()) {
        return 0;
    }

    return *md_getp_indicator();
}

void bluetooth_send_keyboard(report_keyboard_t *report) {
    uint8_t wls_report_kb[MD_SND_CMD_KB_LEN] = {0};

    if (!wls_bt_link_up()) {
        wls_bt_poke_if_down();
        return;
    }

    if (report != NULL) {
        memcpy(wls_report_kb, (uint8_t *)report, sizeof(wls_report_kb));
    }

    md_send_kb(wls_report_kb);
}

void bluetooth_send_nkro(report_nkro_t *report) {
    static report_keyboard_t temp_report_keyboard                 = {0};
    uint8_t                  wls_report_nkro[MD_SND_CMD_NKRO_LEN] = {0};

    if (!wls_bt_link_up()) {
        wls_bt_poke_if_down();
        return;
    }

    if (report != NULL) {
        report_nkro_t temp_report_nkro = *report;
        uint8_t       key_count        = 0;

        temp_report_keyboard.mods = temp_report_nkro.mods;
        for (uint8_t i = 0; i < NKRO_REPORT_BITS; i++) {
            key_count += __builtin_popcount(temp_report_nkro.bits[i]);
        }

        // find key up and del it.
        for (uint8_t i = 0; i < KEYBOARD_REPORT_KEYS && temp_report_keyboard.keys[i]; i++) {
            uint8_t usageid = 0x00;
            uint8_t n;

            for (uint8_t c = 0; c < key_count; c++) {
                for (n = 0; n < NKRO_REPORT_BITS && !temp_report_nkro.bits[n]; n++) {
                }
                usageid = (n << 3) | biton(temp_report_nkro.bits[n]);
                del_key_bit(&temp_report_nkro, usageid);
                if (usageid == temp_report_keyboard.keys[i]) {
                    break;
                }
            }

            if (usageid != temp_report_keyboard.keys[i]) {
                temp_report_keyboard.keys[i] = 0x00;
            }
        }

        /*
         * Use NKRO for sending when more than 6 keys are pressed
         * to solve the issue of the lack of a protocol flag in wireless mode.
         */

        temp_report_nkro = *report;

        for (uint8_t i = 0; i < key_count; i++) {
            uint8_t usageid;
            uint8_t idx, n = 0;

            for (n = 0; n < NKRO_REPORT_BITS && !temp_report_nkro.bits[n]; n++) {
            }
            usageid = (n << 3) | biton(temp_report_nkro.bits[n]);
            del_key_bit(&temp_report_nkro, usageid);

            for (idx = 0; idx < KEYBOARD_REPORT_KEYS; idx++) {
                if (temp_report_keyboard.keys[idx] == usageid) {
                    break;
                }
                if (temp_report_keyboard.keys[idx] == 0x00) {
                    temp_report_keyboard.keys[idx] = usageid;
                    break;
                }
            }

            if (idx == KEYBOARD_REPORT_KEYS && (usageid < (MD_SND_CMD_NKRO_LEN * 8))) {
                wls_report_nkro[usageid / 8] |= 0x01 << (usageid % 8);
            }
        }
    } else {
        memset(&temp_report_keyboard, 0, sizeof(temp_report_keyboard));
    }

    /* The module has no separate NKRO-flag channel, so the collapsed 6KRO
     * keyboard report is sent alongside the NKRO bitmap (as the vendor
     * wireless_send_nkro does). */
    md_send_kb((uint8_t *)&temp_report_keyboard);
    md_send_nkro(wls_report_nkro);
}

void bluetooth_send_mouse(report_mouse_t *report) {
    typedef struct {
        uint8_t buttons;
        int8_t  x;
        int8_t  y;
        int8_t  z;
        int8_t  h;
    } __attribute__((packed)) wls_report_mouse_t;

    wls_report_mouse_t wls_report_mouse = {0};

    if (!wls_bt_link_up()) {
        wls_bt_poke_if_down();
        return;
    }

    if (report != NULL) {
        wls_report_mouse.buttons = report->buttons;
        wls_report_mouse.x       = report->x;
        wls_report_mouse.y       = report->y;
        wls_report_mouse.z       = report->h;
        wls_report_mouse.h       = report->v;
    }

    md_send_mouse((uint8_t *)&wls_report_mouse);
}

void bluetooth_send_consumer(uint16_t usage) {
    if (!wls_bt_link_up()) {
        wls_bt_poke_if_down();
        return;
    }

    md_send_consumer((uint8_t *)&usage);
}

void bluetooth_send_system(uint16_t usage) {
    uint16_t sys = 0;

    if (!wls_bt_link_up()) {
        wls_bt_poke_if_down();
        return;
    }

    /* The module's system frame carries the bitmask the vendor stack derives
     * from usages 0x81..0x83; anything else is not a system usage. */
    if (usage >= 0x81 && usage <= 0x83) {
        sys = 0x01 << (usage - 0x81);
        md_send_system((uint8_t *)&sys);
    }
}

void bluetooth_send_raw_hid(uint8_t *data, uint8_t length) {
    if (!wls_bt_link_up()) {
        return;
    }

    md_send_raw(data, length);
}
