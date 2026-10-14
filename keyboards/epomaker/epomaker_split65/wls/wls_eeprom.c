// Copyright 2026
// SPDX-License-Identifier: GPL-2.0-or-later

#include "wls_eeprom.h"

#include "wls/wls.h"
#include "wls/transport_logic.h"

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
    if (hsm_confinfo_needs_default(confinfo.raw, confinfo.version, CONFINFO_VERSION)) {
        eeconfig_confinfo_default();
    }
}

uint8_t last_btdev_from_confinfo(void) {
    return hsm_btdev_or(confinfo.devs, confinfo.last_btdevs);
}

void wls_eeprom_wipe_config(void) {
    eeconfig_init();
    eeconfig_update_rgb_matrix_default();

#ifdef RGBLIGHT_ENABLE
    extern void rgblight_init(void);
    is_rgblight_initialized = false;
    rgblight_init();
    eeconfig_update_rgblight_default();
    rgblight_enable();
#endif

    eeconfig_read_keymap(&keymap_config);

#if defined(NKRO_ENABLE) && defined(FORCE_NKRO)
    keymap_config.nkro = 0;
    eeconfig_update_keymap(&keymap_config);
#endif
}
