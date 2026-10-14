// Copyright 2026
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Pure split-link protocol decisions, extracted from the board's split glue.
 *
 * The master half and the slave half exchange a fixed command byte plus a body
 * over upstream's `USER_SYNC_MMS` RPC (`transaction_rpc_exec`). The *wire* is
 * upstream's; what this file owns is the *rule*: what each command byte means,
 * and therefore what the slave should do with it. Keeping that decision pure
 * (cmd + body -> an action descriptor) means the split protocol is pinned by a
 * test rather than by matching two switch statements by hand across the two
 * halves. */

/* Command bytes the master sends (`master_to_slave_t.cmd`). */
typedef enum {
    WLS_SPLIT_CMD_NONE           = 0,
    WLS_SPLIT_CMD_SYNC_MULTIMODE = 0x55, /* sync the vendor device index to the slave */
    WLS_SPLIT_CMD_SLEEP          = 0xAA, /* order the slave to stop */
    WLS_SPLIT_CMD_POWER_DOWN     = 0xBB, /* master is suspending */
    WLS_SPLIT_CMD_WAKEUP         = 0xCC, /* master woke: re-assert the LED rail */
    WLS_SPLIT_CMD_RESET_SETTINGS = 0xDD, /* master reset its settings */
} wls_split_cmd_t;

/* The device-index range the wire may name: `DEVS_USB` (0) through `DEVS_2G4`
 * (6). The split body is untrusted input -- a corrupted byte must not become
 * board state, because the index is what the module is told to select and what
 * `hsm_host_of_devs()` maps to a transport (0xFF would read as BLUETOOTH). The
 * decision clamps to this range so the *rule* is testable and the shell cannot
 * be handed an out-of-range index at all. Mirrors `DEVS_USB`..`DEVS_2G4`. */
#define WLS_SPLIT_DEVS_MIN 0
#define WLS_SPLIT_DEVS_MAX 6

/* What the slave should do on receiving a command. Exactly one action. */
typedef enum {
    WLS_SPLIT_ACT_NONE = 0,
    WLS_SPLIT_ACT_SYNC_DEVICE_INDEX, /* apply body[0]=last, body[1]=now */
    WLS_SPLIT_ACT_ORDER_SLEEP,       /* body[0] is the ordered-sleep flag */
    WLS_SPLIT_ACT_REAPPLY_RAIL,      /* re-derive the LED rail from the RGB value */
    WLS_SPLIT_ACT_RESET_SETTINGS,    /* run the reset-settings path */
} wls_split_act_kind_t;

/* The slave's decoded command. `body` is only meaningful for actions that carry
 * arguments (see the enum above). */
typedef struct {
    wls_split_act_kind_t kind;
    uint8_t              last_devs;  /* SYNC_DEVICE_INDEX: body[0] */
    uint8_t              now_devs;   /* SYNC_DEVICE_INDEX: body[1] */
    bool                 sleep_flag; /* ORDER_SLEEP: body[0] != 0 */
} wls_split_action_t;

/* Decide the slave's action from a received command byte and its 4-byte body.
 * Pure: no I/O, no firmware state. An unknown command decodes to NONE (the slave
 * must stay silent rather than act on a byte it does not understand). */
wls_split_action_t wls_split_decide_slave(uint8_t cmd, const uint8_t body[4]);
