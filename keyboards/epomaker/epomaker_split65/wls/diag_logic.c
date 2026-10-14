// Copyright 2026
// SPDX-License-Identifier: GPL-2.0-or-later

#include "diag_logic.h"

uint8_t wls_diag_encode(const wls_diag_t *d, uint8_t *out) {
    out[0] = d->switch_pos;
    out[1] = d->devs;
    out[2] = d->host;
    out[3] = d->boot_count;
    out[4] = d->usb_line;
    out[5] = d->link_state;
    out[6] = d->pin_c14;
    out[7] = d->pin_c15;
    out[8] = d->pin_b9;
    out[9] = d->reset_cause;
    return WLS_DIAG_ENC_SIZE;
}

bool wls_diag_decode(const uint8_t *in, wls_diag_t *out) {
    if (in == NULL || out == NULL) {
        return false;
    }
    out->switch_pos  = in[0];
    out->devs        = in[1];
    out->host        = in[2];
    out->boot_count  = in[3];
    out->usb_line    = in[4];
    out->link_state  = in[5];
    out->pin_c14     = in[6];
    out->pin_c15     = in[7];
    out->pin_b9      = in[8];
    out->reset_cause = in[9];
    return true;
}
