// Copyright 2026
// SPDX-License-Identifier: GPL-2.0-or-later

#include QMK_KEYBOARD_H

#include "wls_split.h"

#include "quantum.h"
#include "transactions.h"
#include "serial_usart.h"
#include "wireless.h"
#include "usb_main.h"
#include "lowpower.h"
#include "wls/wls.h"
#include "wls/wls_lifecycle.h"
#include "wls/wls_module.h"

bool wls_split_send(uint8_t cmd, const uint8_t body[4]) {
    master_to_slave_t m2s = {0};
    slave_to_master_t s2m = {0};
    m2s.cmd               = cmd;
    if (body != NULL) {
        memcpy(m2s.body, body, sizeof(m2s.body));
    }
    return transaction_rpc_exec(USER_SYNC_MMS, sizeof(m2s), &m2s, sizeof(s2m), &s2m);
}

void master_sync_mms_slave(uint8_t last_mode, uint8_t now_mode, uint8_t reset) {
    const uint8_t body[4] = {last_mode, now_mode, reset, 0};
    if (wls_split_send(WLS_SPLIT_CMD_SYNC_MULTIMODE, body)) {
        dprintf("Slave Sleep OK1\n");
    } else {
        dprintf("Slave sync failed1!\n");
    }
}

void user_sync_mms_slave_handler(uint8_t in_buflen, const void *in_data, uint8_t out_buflen, void *out_data) {
    (void)in_buflen;
    (void)out_buflen;
    const master_to_slave_t *m2s = (const master_to_slave_t *)in_data;
    slave_to_master_t       *s2m = (slave_to_master_t *)out_data;

    /* The wire is upstream's RPC; *what each command means* is the pure
     * `wls_split_decide_slave()` (wls/split_logic). This handler only applies
     * the decided action. */
    wls_split_action_t action = wls_split_decide_slave(m2s->cmd, m2s->body);

    switch (action.kind) {
        case WLS_SPLIT_ACT_SYNC_DEVICE_INDEX:
            wls_module_devs_change(action.last_devs, action.now_devs, false);
            s2m->resp = 0x00;
            break;
        case WLS_SPLIT_ACT_ORDER_SLEEP:
            if (wireless_get_current_devs() != DEVS_USB) {
                palSetLineMode(SERIAL_USART_RX_PIN, PAL_OUTPUT_TYPE_OPENDRAIN);
                palSetLineMode(SERIAL_USART_TX_PIN, PAL_OUTPUT_TYPE_OPENDRAIN);
            }
            s2m->resp = 0x00;
            wls_order_sleep(action.sleep_flag);
            lpwr_set_timeout_manual(true);
            break;
        case WLS_SPLIT_ACT_REAPPLY_RAIL:
            wls_led_rail_apply();
            s2m->resp = 0x00;
            break;
        case WLS_SPLIT_ACT_RESET_SETTINGS:
            wls_reset_settings(WLS_RESET_CAUSE_REMOTE);
            s2m->resp = 0x00;
            break;
        case WLS_SPLIT_ACT_NONE:
        default:
            /* The master-suspend command (0xBB) lands here deliberately: the
             * slave must not force its own rail off (defect 5). An unknown
             * command also lands here -- stay silent, act on nothing. */
            break;
    }
}
