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
    return WLS_DIAG_ENC_SIZE;
}

bool wls_diag_decode(const uint8_t *in, wls_diag_t *out) {
    if (in == NULL || out == NULL) {
        return false;
    }
    out->switch_pos = in[0];
    out->devs       = in[1];
    out->host       = in[2];
    out->boot_count = in[3];
    out->usb_line   = in[4];
    out->link_state = in[5];
    return true;
}
