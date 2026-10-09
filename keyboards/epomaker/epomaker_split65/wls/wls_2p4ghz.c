// Copyright 2026
// SPDX-License-Identifier: GPL-2.0-or-later

#include "wireless_2p4ghz.h"
#include "wls.h"

/* The board's 2.4 GHz adapter for the upstream-shaped `wireless_2p4ghz`
 * contract (drivers/wireless/wireless_2p4ghz.h). Like the Bluetooth adapter
 * (`wls_bluetooth.c`), this is a thin peer that presents the CH582F module
 * over the upstream contract instead of swapping `host_driver_t` in the vendor
 * `set_transport()`. When `connection_get_host()` resolves to
 * `CONNECTION_HOST_2P4GHZ`, `host_get_active_driver()` returns `w2p4_driver`,
 * whose members are these functions — so this is the path every report takes
 * while the mode switch is on 2.4 GHz.
 *
 * The module is a modeled peer (item 14e): its link state is its UART state
 * (`md_getp_state()`), its sink is the `md_send_*` frame API. */

static bool wls_is_2g4(void) {
    return wireless_get_current_devs() == DEVS_2G4;
}

static bool wls_2g4_link_up(void) {
    return wls_is_2g4() && *md_getp_state() == MD_STATE_CONNECTED;
}

/* Mirror the vendor send path: poke the module when a report arrives while the
 * link is down, but only on the 2.4 GHz device so a stray send cannot disturb
 * USB or a BT profile. */
static void wls_2g4_poke_if_down(void) {
    if (wls_is_2g4() && *md_getp_state() != MD_STATE_CONNECTED) {
        wireless_devs_change(wireless_get_current_devs(), wireless_get_current_devs(), false);
    }
}

void wireless_2p4ghz_init(void) {}

void wireless_2p4ghz_task(void) {}

bool wireless_2p4ghz_is_connected(void) {
    /* Gated on the current device being 2.4 GHz on purpose: upstream's
     * connection_auto_detect_host() calls this to resolve CONNECTION_HOST_AUTO,
     * and the module UART reports MD_STATE_CONNECTED on Bluetooth and USB too. */
    return wls_2g4_link_up();
}

bool wireless_2p4ghz_can_send_nkro(void) {
    return true;
}

uint8_t wireless_2p4ghz_keyboard_leds(void) {
    if (!wls_2g4_link_up()) {
        return 0;
    }

    return *md_getp_indicator();
}

void wireless_2p4ghz_send_keyboard(report_keyboard_t *report) {
    uint8_t wls_report_kb[MD_SND_CMD_KB_LEN] = {0};

    if (!wls_2g4_link_up()) {
        wls_2g4_poke_if_down();
        return;
    }

    if (report != NULL) {
        memcpy(wls_report_kb, (uint8_t *)report, sizeof(wls_report_kb));
    }

    md_send_kb(wls_report_kb);
}

void wireless_2p4ghz_send_nkro(report_nkro_t *report) {
    static report_keyboard_t temp_report_keyboard                 = {0};
    uint8_t                  wls_report_nkro[MD_SND_CMD_NKRO_LEN] = {0};

    if (!wls_2g4_link_up()) {
        wls_2g4_poke_if_down();
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

    /* As the vendor path does: send the collapsed 6KRO report alongside the
     * NKRO bitmap, because the module has no separate NKRO-flag channel. */
    md_send_kb((uint8_t *)&temp_report_keyboard);
    md_send_nkro(wls_report_nkro);
}

void wireless_2p4ghz_send_mouse(report_mouse_t *report) {
    typedef struct {
        uint8_t buttons;
        int8_t  x;
        int8_t  y;
        int8_t  z;
        int8_t  h;
    } __attribute__((packed)) wls_report_mouse_t;

    wls_report_mouse_t wls_report_mouse = {0};

    if (!wls_2g4_link_up()) {
        wls_2g4_poke_if_down();
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

void wireless_2p4ghz_send_consumer(uint16_t usage) {
    if (!wls_2g4_link_up()) {
        wls_2g4_poke_if_down();
        return;
    }

    md_send_consumer((uint8_t *)&usage);
}

void wireless_2p4ghz_send_system(uint16_t usage) {
    uint16_t sys = 0;

    if (!wls_2g4_link_up()) {
        wls_2g4_poke_if_down();
        return;
    }

    if (usage >= 0x81 && usage <= 0x83) {
        sys = 0x01 << (usage - 0x81);
        md_send_system((uint8_t *)&sys);
    }
}

void wireless_2p4ghz_send_raw_hid(uint8_t *data, uint8_t length) {
    if (!wls_2g4_link_up()) {
        return;
    }

    md_send_raw(data, length);
}
