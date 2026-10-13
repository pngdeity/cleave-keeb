// Copyright 2026
// SPDX-License-Identifier: GPL-2.0-or-later

#include "wls_eeprom.h"

#include "wls/wls.h"

confinfo_t confinfo;

void usart_init(void) {
    // palSetLineMode(SERIAL_USART_TX_PIN, PAL_MODE_ALTERNATE(SERIAL_USART_TX_PAL_MODE) | PAL_OUTPUT_TYPE_OPENDRAIN);
    palSetLineMode(SERIAL_USART_TX_PIN, PAL_MODE_ALTERNATE(SERIAL_USART_TX_PAL_MODE) | PAL_OUTPUT_TYPE_PUSHPULL | PAL_OUTPUT_SPEED_HIGHEST);
    palSetLineMode(SERIAL_USART_RX_PIN, PAL_MODE_ALTERNATE(SERIAL_USART_RX_PAL_MODE) | PAL_OUTPUT_TYPE_PUSHPULL | PAL_OUTPUT_SPEED_HIGHEST);
}

void eeconfig_confinfo_update(uint32_t raw) {
    eeconfig_update_kb(raw);
}

uint32_t eeconfig_confinfo_read(void) {
    return eeconfig_read_kb();
}

void eeconfig_confinfo_default(void) {
    confinfo.flag               = true;
    confinfo.record_channel     = 0;
    confinfo.record_last_mode   = 0xff;
    confinfo.last_btdevs        = 1;
    confinfo.dir_flag           = 0;
    confinfo.last_wireless_devs = DEVS_BT1;
    confinfo.version            = CONFINFO_VERSION;

    eeconfig_init_user_datablock();
    eeconfig_confinfo_update(confinfo.raw);
}

void eeconfig_confinfo_init(void) {
    confinfo.raw = eeconfig_confinfo_read();
    /* A blank block, or one written by older firmware (different layout), is
     * re-defaulted. The version gate makes the migration deterministic instead
     * of guessing per field. */
    if (!confinfo.raw || confinfo.version != CONFINFO_VERSION) {
        eeconfig_confinfo_default();
    }
}
