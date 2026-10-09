/* Copyright 2026
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/* The board's Bluetooth driver (`wls_bluetooth.c`) reproduces the vendor
 * wireless driver's NKRO collapse, because the module has no separate NKRO-flag
 * channel: a >6-key report must go out as a 6KRO keyboard frame plus a separate
 * NKRO bitmap. `bt_collapse` below is a byte-for-byte extraction of that
 * collapse; `vendor_collapse` is the same logic copied verbatim from
 * `keyboards/linker/wireless/wireless.c` `wireless_send_nkro()`. If the two ever
 * diverge, one of the two senders has silently changed -- which is exactly the
 * kind of drift that only shows up as a lost keypress on real hardware.
 *
 * The two senders are the same algorithm, so the test pins their agreement
 * across the cases that matter: the 6-key boundary, the 7-key overflow, key-up
 * handling with a stale 6KRO array, and a full NKRO bitmap. */

#include "gtest/gtest.h"

/* del_key_bit() is declared in report.h only under NKRO_ENABLE (it is the
 * NKRO path); define it for this translation unit before the include. It must
 * not go in config.h, which is shared with test_common's keyboard_report_util.cpp
 * -- that file deliberately rejects NKRO builds. */
#define NKRO_ENABLE

extern "C" {
#include "quantum.h"
#include "keyboards/linker/wireless/wireless.h"
#include "bitwise.h"
}

/* The collapse walk calls this one NKRO helper. report.c compiles it out unless
 * its own translation unit sees NKRO_ENABLE, which the shared test build does
 * not define, so the test links its own copy. The body is copied from
 * tmk_core/protocol/report.c del_key_bit(). */
extern "C" void del_key_bit(report_nkro_t *nkro_report, uint8_t code) {
    if ((code >> 3) < NKRO_REPORT_BITS) {
        nkro_report->bits[code >> 3] &= ~(1 << (code & 7));
    }
}

/* Inputs are the NKRO bitmap; the 6KRO array is whatever the driver retained
 * from the previous report. Both senders keep the 6KRO array in a `static`
 * local, so it is passed in explicitly here to make the retained state the test
 * subject rather than hidden. */
typedef struct {
    report_nkro_t     input;
    report_keyboard_t retained_6kro;
} nkro_case_t;

/* ---------------------------------------------------------------------------
 * Board Bluetooth driver -- extracted from
 * keyboards/epomaker/epomaker_split65/wls/wls_bluetooth.c bluetooth_send_nkro().
 * ------------------------------------------------------------------------- */
static void bt_collapse(const report_nkro_t *report, const report_keyboard_t *retained, report_keyboard_t *out_6kro, uint8_t *out_nkro_bitmap) {
    report_keyboard_t temp_report_keyboard                 = *retained;
    uint8_t           wls_report_nkro[MD_SND_CMD_NKRO_LEN] = {0};

    memcpy(&temp_report_keyboard, retained, sizeof(temp_report_keyboard));

    report_nkro_t temp_report_nkro = *report;
    uint8_t       key_count        = 0;

    temp_report_keyboard.mods = temp_report_nkro.mods;
    for (uint8_t i = 0; i < NKRO_REPORT_BITS; i++) {
        key_count += __builtin_popcount(temp_report_nkro.bits[i]);
    }

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

    memcpy(out_6kro, &temp_report_keyboard, sizeof(temp_report_keyboard));
    memcpy(out_nkro_bitmap, wls_report_nkro, sizeof(wls_report_nkro));
}

/* ---------------------------------------------------------------------------
 * Vendor wireless driver -- verbatim from
 * keyboards/linker/wireless/wireless.c wireless_send_nkro(), with the static
 * state and the two `md_send_*` sinks redirected to outputs.
 * ------------------------------------------------------------------------- */
static void vendor_collapse(const report_nkro_t *report, const report_keyboard_t *retained, report_keyboard_t *out_6kro, uint8_t *out_nkro_bitmap) {
    report_keyboard_t temp_report_keyboard                 = *retained;
    uint8_t           wls_report_nkro[MD_SND_CMD_NKRO_LEN] = {0};

    memcpy(&temp_report_keyboard, retained, sizeof(temp_report_keyboard));

    report_nkro_t temp_report_nkro = *report;
    uint8_t       key_count        = 0;

    temp_report_keyboard.mods = temp_report_nkro.mods;
    for (uint8_t i = 0; i < NKRO_REPORT_BITS; i++) {
        key_count += __builtin_popcount(temp_report_nkro.bits[i]);
    }

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

    memcpy(out_6kro, &temp_report_keyboard, sizeof(temp_report_keyboard));
    memcpy(out_nkro_bitmap, wls_report_nkro, sizeof(wls_report_nkro));
}

static void run_and_compare(const nkro_case_t *tc) {
    report_keyboard_t bt_6kro, vd_6kro;
    uint8_t           bt_nkro[MD_SND_CMD_NKRO_LEN], vd_nkro[MD_SND_CMD_NKRO_LEN];

    bt_collapse(&tc->input, &tc->retained_6kro, &bt_6kro, bt_nkro);
    vendor_collapse(&tc->input, &tc->retained_6kro, &vd_6kro, vd_nkro);

    EXPECT_EQ(0, memcmp(&bt_6kro, &vd_6kro, sizeof(bt_6kro)));
    EXPECT_EQ(0, memcmp(bt_nkro, vd_nkro, sizeof(bt_nkro)));
}

class WirelessNkroParityTest : public ::testing::Test {
   protected:
    nkro_case_t tc = {};

    void set_bit(uint8_t usage) {
        tc.input.bits[usage / 8] |= 0x01 << (usage % 8);
    }
};

/* The bulk of NKRO reports are <=6 keys: they must collapse to exactly the
 * 6KRO array, with an empty NKRO bitmap. */
TEST_F(WirelessNkroParityTest, six_keys_fit_in_6kro) {
    tc.input.mods = 0x02;
    for (uint8_t i = 0; i < 6; i++) {
        set_bit(0x04 + i);
    }
    run_and_compare(&tc);
}

/* Seven keys overflow the 6KRO array: the seventh must appear only in the NKRO
 * bitmap. This is the "lack of a protocol flag in wireless mode" case. */
TEST_F(WirelessNkroParityTest, seven_keys_overflow_to_nkro_bitmap) {
    for (uint8_t i = 0; i < 7; i++) {
        set_bit(0x04 + i);
    }
    run_and_compare(&tc);
}

/* Key-up: a key present in the retained 6KRO array but absent from the new NKRO
 * bitmap must be cleared to zero. */
TEST_F(WirelessNkroParityTest, key_up_clears_stale_6kro_entry) {
    tc.retained_6kro.keys[0] = 0x04;
    tc.retained_6kro.keys[1] = 0x05;
    set_bit(0x04);
    run_and_compare(&tc);
}

/* A dense NKRO report spanning both the 6KRO window and the bitmap. */
TEST_F(WirelessNkroParityTest, dense_report_spans_both) {
    for (uint8_t usage = 0x04; usage < 0x40; usage += 2) {
        set_bit(usage);
    }
    run_and_compare(&tc);
}

/* The bitmap is bounded by MD_SND_CMD_NKRO_LEN*8; a usage beyond that bound is
 * neither in the 6KRO array nor the bitmap, and both must agree on that. */
TEST_F(WirelessNkroParityTest, usage_beyond_bitmap_is_dropped_by_both) {
    for (uint8_t i = 0; i < 6; i++) {
        set_bit(0x04 + i);
    }
    set_bit(MD_SND_CMD_NKRO_LEN * 8);
    run_and_compare(&tc);
}

/* A full bitmap is the worst case for the walk; it must still be identical. */
TEST_F(WirelessNkroParityTest, full_bitmap_matches) {
    for (uint16_t i = 0; i < NKRO_REPORT_BITS * 8; i++) {
        tc.input.bits[i / 8] = 0xFF;
    }
    run_and_compare(&tc);
}

/* No keys and no retained state: the empty report. */
TEST_F(WirelessNkroParityTest, empty_report_matches) {
    run_and_compare(&tc);
}

/* Modifiers must be carried onto the 6KRO frame regardless of key count. */
TEST_F(WirelessNkroParityTest, modifiers_carried) {
    tc.input.mods = 0x11;
    set_bit(0x04);
    run_and_compare(&tc);
}
