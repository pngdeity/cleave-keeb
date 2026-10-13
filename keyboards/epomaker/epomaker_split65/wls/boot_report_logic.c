// Copyright 2026
// SPDX-License-Identifier: GPL-2.0-or-later

#include "boot_report_logic.h"

uint8_t wls_boot_report_encode(const wls_boot_report_t *r, uint8_t *out) {
    out[0] = r->switch_pos;
    out[1] = r->host;
    /* The settled flag rides the top bit of the host byte rather than taking a
     * fourth byte: it is the only boolean, and the host vocabulary is small. The
     * decoded value is masked back off. */
    out[2] = (uint8_t)(r->settled ? 0x80u : 0x00u);
    return WLS_BOOT_REPORT_ENC_SIZE;
}

bool wls_boot_report_decode(const uint8_t *in, wls_boot_report_t *out) {
    if (in == NULL || out == NULL) {
        return false;
    }
    out->switch_pos = in[0];
    out->host       = in[1];
    out->settled    = (in[2] & 0x80u) != 0;
    return true;
}

wls_boot_blink_step_t wls_boot_report_blink_step(const wls_boot_report_t *r, uint8_t index) {
    wls_boot_blink_step_t step = {0, 0, 0, 0};

    switch (index) {
        case 0:
            /* Switch position: RED = USB, GREEN = BT, BLUE = 2.4G; the UNKNOWN
             * (unreadable) case is the one that matters most, so it gets the
             * distinctive colour rather than being folded into another. */
            step.r     = (r->switch_pos == 0) ? 0xFF : 0x00;
            step.g     = (r->switch_pos == 1) ? 0xFF : 0x00;
            step.b     = (r->switch_pos == 3) ? 0xFF : ((r->switch_pos == 2) ? 0x40 : 0x00);
            step.count = (uint8_t)(r->switch_pos + 1);
            break;
        case 1:
            /* Applied host: GREEN = USB, BLUE = BT, WHITE = 2.4G. */
            step.r     = (r->host == 5) ? 0xFF : 0x00;
            step.g     = (r->host == 3) ? 0xFF : 0x00;
            step.b     = ((r->host == 4) || (r->host == 5)) ? 0xFF : 0x00;
            step.count = (uint8_t)(r->host + 1);
            break;
        case 2:
            /* Settled: one WHITE pulse means the read was usable; three RED
             * pulses means it was NOT (nothing was applied). */
            if (r->settled) {
                step.r = step.g = step.b = 0xFF;
                step.count               = 1;
            } else {
                step.r     = 0xFF;
                step.count = 3;
            }
            break;
        default:
            break;
    }

    return step;
}
