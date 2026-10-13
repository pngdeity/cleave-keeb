// Copyright 2026
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "wls/split_logic.h"

/* The split link's wire half: the one way the master sends over upstream's
 * `USER_SYNC_MMS` RPC, and the shape of the two ends' payloads.
 *
 * The *rule* (what each command byte means, and what the slave should do with
 * it) is the pure `wls/split_logic`. This is the effect shell around it: it
 * builds the payload, performs the RPC, and reports success. Every master-side
 * sender routes through `wls_split_send()`, so the wire call is stated once
 * instead of open-coded at each site. */

typedef struct _master_to_slave_t {
    uint8_t cmd;
    uint8_t body[4];
} master_to_slave_t;

typedef struct _slave_to_master_t {
    uint8_t resp;
    uint8_t body[4];
} slave_to_master_t;

/* Send `cmd` with an optional 4-byte body to the slave. Returns whether the
 * transaction completed (the response is not interpreted here — callers log it).
 * `body` may be NULL for a command that carries no arguments. */
bool wls_split_send(uint8_t cmd, const uint8_t body[4]);

/* The slave-side RPC handler, registered with `transaction_register_rpc()`. It
 * decodes the command with the pure `wls_split_decide_slave()` and applies the
 * decided action. */
void user_sync_mms_slave_handler(uint8_t in_buflen, const void *in_data, uint8_t out_buflen, void *out_data);

/* Tell the slave which device index the master is on. Sent by the master's
 * periodic housekeeping sync; only meaningful when the master's own device index
 * changed. */
void master_sync_mms_slave(uint8_t last_mode, uint8_t now_mode, uint8_t reset);
