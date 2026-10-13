// Copyright 2026
// SPDX-License-Identifier: GPL-2.0-or-later

#include QMK_KEYBOARD_H

#include "wls_split.h"

#include "quantum.h"
#include "transactions.h"
#include "wireless.h"
#include "usb_main.h"

bool wls_split_send(uint8_t cmd, const uint8_t body[4]) {
    master_to_slave_t m2s = {0};
    slave_to_master_t s2m = {0};
    m2s.cmd               = cmd;
    if (body != NULL) {
        memcpy(m2s.body, body, sizeof(m2s.body));
    }
    return transaction_rpc_exec(USER_SYNC_MMS, sizeof(m2s), &m2s, sizeof(s2m), &s2m);
}
