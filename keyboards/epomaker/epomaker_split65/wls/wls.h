// Copyright 2026
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include QMK_KEYBOARD_H

#include "quantum.h"
#include "battery.h"
#include <stdbool.h>
#include "wireless.h"
#include "wls/transport_logic.h"
#include "wls/diag_logic.h"
#include "wls/boot_code_logic.h"
#include "connection.h"

#define HS_BT_DEF_PIN C14
#define HS_2G4_DEF_PIN C15
#define HS_BT_PIN_STATE 0, 1
#define HS_2G4_PIN_STATE 1, 0
#define HS_USB_PIN_STATE 1, 1

#define HS_GET_MODE_PIN_(pin_bt, pin_2g4) ((((#pin_bt)[0] == 'x') || ((gpio_read_pin(HS_BT_DEF_PIN) + 0x30) == ((#pin_bt)[0]))) && (((#pin_2g4)[0] == 'x') || ((gpio_read_pin(HS_2G4_DEF_PIN) + 0x30) == ((#pin_2g4)[0]))))
#define HS_GET_MODE_PIN(state) HS_GET_MODE_PIN_(state)
#define HS_MODEIO_DETECTION_TIME 50
#define HS_LBACK_TIMEOUT (30 * 1000)
#define HS_SLEEP_TIMEOUT (1 * 60000) //(1 * 60000)

/* How often the board re-asserts the cable/charge state to the module and asks
 * it for the volume. This is a *bounded poll*, not a transition effect: an
 * external USB-C cable can be inserted or removed while the keyboard is idle,
 * with no keycode or switch event to hang the update on, so the module must be
 * told periodically. The cadence is the rule, so it is named here rather than
 * left as a bare literal at the call site. */
#define WLS_MODULE_POLL_MS 1000

/* After an EEPROM reset the battery indicator is suppressed for this long, so
 * the wipe's own RGB re-init does not read as a battery event. Expressed as a
 * deadline (below), not as a flag re-derived every frame. */
#define WLS_BAT_SUPPRESS_MS 2000

extern bool charging_state;
extern bool bat_full_flag;
/* The ordered-sleep flag's single authority: set/clear through the setter,
 * read as a query. One writer, no side-effecting reads. */
void wls_order_sleep(bool ordered);
bool wls_sleep_ordered(void);
bool hs_rgb_blink_hook(void);
bool hs_mode_scan(bool update);
/* The switch gate, stated once: may a wireless keycode for `target_devs` act,
 * given the switch position? Answers via the pure `hsm_keycode_allowed` from
 * `hs_mode_switch_pos()`, the board's one switch read. Replaces the six private
 * copies of the seed+detect+predicate idiom in the board glue. */
bool hs_keycode_allowed(uint8_t target_devs);
bool hs_modeio_detection(bool update);

/* The physical switch position, read from the pins alone (no edge, no persisted
 * state). The one pin read; its meaning is in `transport_logic.c` and the
 * decision built from it is in the transport owner (`wls_transport.c`). */
hsm_switch_pos_t hs_mode_switch_pos(void);

/* Restart the active link countdown (reconnect/sleep) from now, on an activity
 * event such as a keypress or wake. The countdown's policy lives in the pure
 * `hsm_link_watch`; this is only the store-back shell. */
void hs_link_activity(void);
void wls_led_rail_apply(void);

/* Gather the shell-state diagnostic snapshot (see `wls/diag_logic`). The shell
 * half of the observability seam: reads the live switch, device index, host,
 * boot counter, USB line, and module link state into a `wls_diag_t`; the pure
 * `wls_diag_encode()` turns it into bytes. Exposed so any channel that is up
 * (the 0xA4 USB reply, the radio push) can carry it. */
void wls_diag_gather(wls_diag_t *out);

/* Increment the diagnostic boot counter (once per MCU boot). */
void wls_note_boot(void);

/* Read and clear the MCU's latched reset cause (`RCC->RSTSTAT`), once per boot,
 * and return the latched bits. The chip holds the cause of its last reset until
 * the latch is cleared, so this is a *consume*: it must run exactly once, before
 * anything else could clear it, and the returned value is then carried to every
 * reporter (the LED code and the `0xA4` diag block) so the two cannot disagree.
 * Call from the same place as `wls_note_boot()`. Returns 0 on a platform with no
 * such register. */
uint32_t wls_note_reset_cause(void);

/* The reset-cause bits captured by the last `wls_note_reset_cause()`, for
 * readers that must not re-consume the latch. 0 until that call has run. */
uint32_t wls_reset_cause(void);

/* The boot transport decision, as the shell made it, retained so a later
 * diagnostic read can report on a boot whose USB line did not survive (the
 * `0xA4` reply needs the line up; a bad boot tears it down). Set once by the boot
 * seed; read by the diagnostic assembler and by `wls_boot_code_now()`.
 *
 * `switch_pos` and `host` are the raw vocabularies used everywhere else on this
 * board; `settled` says whether the switch read was readable at all. */
typedef struct {
    uint8_t switch_pos; /* hsm_switch_pos_t: 0=UNKNOWN,1=USB,2=BT,3=2.4G */
    uint8_t host;       /* connection host: `WLS_BOOT_HOST_*` (== `HSM_HOST_*`) */
    bool    settled;    /* false = the position was unreadable (nothing applied) */
    bool    master;     /* this half's own `is_keyboard_master()` answer at boot */
} wls_boot_report_t;

/* The boot transport decision, retained so a later diagnostic read can report on
 * a boot whose USB line did not survive. Set once by the boot seed; read by the
 * diagnostic assembler. */
void              wls_note_boot_decision(wls_boot_report_t report);
wls_boot_report_t wls_boot_decision(void);

/* The shell's adapter: read the retained boot report plus the two live values
 * into the pure `wls_boot_facts()` of `wls/boot_code_logic` and return its
 * stage/code. Read by the `0xA4` assembler and the indicator. */
wls_boot_code_t wls_boot_code_now(void);
/* BT profile selection, owned by the Bluetooth driver (item 14b): which of the
 * module's BT1..BT5 profiles is active, and re-pairing it. The sub-index has no
 * home in upstream's `connection` model, so it lives with the driver that
 * actually carries it to the module. */
void    bluetooth_select_profile(uint8_t profile, bool reset);
uint8_t bluetooth_get_profile(void);

/* Battery report over raw HID (see docs/PROTOCOL.md). The report is
 * RAW_EPSIZE bytes; only the following are meaningful, the rest are zero. */
#define KB_BATTERY_CMD_GET 0xA4

#define KB_BATTERY_IDX_CMD 0
#define KB_BATTERY_IDX_LEVEL 1
#define KB_BATTERY_IDX_CHARGE 4
#define KB_BATTERY_IDX_TRANSPORT 5
#define KB_BATTERY_IDX_MODEL 6

/* Selector readback (provisional, for hardware verification of items 14b/14c).
 * The persisted host and the BT sub-profile are otherwise unobservable without a
 * working BLE link, so they are exposed here: index 7 = upstream's
 * `config.desired_host` (CONNECTION_HOST_* raw value), index 8 = the BT driver's
 * active profile (DEVS_BT1..BT5). Both are test-only and may be reclaimed. */
#define KB_BATTERY_IDX_HOST 7
#define KB_BATTERY_IDX_BTSUB 8

/* Module fingerprint (see docs/PROTOCOL.md). The CH582F module is the source of
 * the Bluetooth faults and its image is closed and undumpable, so its identity
 * is worth reporting: index 9 = the module firmware version (`0x5D`, 0 if the
 * image does not answer that query), index 10 = the module link state (`0x5B`
 * sub-code range, i.e. the MD_STATE_* value). Read-only diagnostics. */
#define KB_BATTERY_IDX_MD_VERSION 9
#define KB_BATTERY_IDX_MD_STATE 10

/* Shell-state diagnostic block (the observability seam; `wls/diag_logic`). A
 * fixed 10-byte view of how the firmware currently reads the world — the switch
 * position, the device index, the connection host, a boot counter, the USB data
 * line, the module link state, the raw `C14`/`C15`/`B9` pin levels behind
 * `switch_pos`, and the consumed MCU reset cause. Encoded by the pure
 * `wls_diag_encode()` so a host reader and the firmware cannot drift. This is the
 * one instrument that works regardless of which transport is up, which is exactly
 * what the shell-only defects (7, 8, 9, and the USB self-detach) needed and
 * lacked. */
#define KB_BATTERY_IDX_DIAG 11

/* The boot diagnostic code, as the two counts the LED plays: `{amber, white}`
 * (Dell E-series convention; the full fault table is `wls/boot_code_logic.h`).
 * It is retained across the boot, so a boot that tore its own USB line down is
 * still reportable on the NEXT successful reply — the case the diag block
 * cannot cover, because it is sampled live and the line is gone. Sending the
 * same pair the eye counts means a host reader and the operator cannot disagree
 * about what the boot said. Offset to follow the 10-byte diag block (11..20). */
#define KB_BATTERY_IDX_BOOT_CODE 21

/* The jump journal (`wls/wls_jump_journal`). A `dfa0` DFU window means the magic
 * word at `__ram0_end__ - 4` was set and the MCU reset; the word is cleared by
 * the reset handler before any of our code runs, so the jump itself cannot be
 * observed after the fact. This 12-byte record is written, in `.ram0`, at the one
 * place the board intends to jump (`bootloader_jump()`), survives the reset, and
 * is reported here. It discriminates "our `QK_BOOT` ran" (presence byte set)
 * from "the word appeared with no stamp" (present = 0) — the reading that tells
 * a dispatched `QK_BOOT` from a corrupt-value write or an electrical one. It
 * ends the report: offset 23 through the last byte (`RAW_EPSIZE - 1`). */
#define KB_BATTERY_IDX_JUMP 23

/* KB_BATTERY_IDX_CHARGE */
enum kb_battery_charge {
    KB_BATTERY_CHARGE_DISCHARGING = 0,
    KB_BATTERY_CHARGE_CHARGING,
    KB_BATTERY_CHARGE_FULL,
};

/* KB_BATTERY_IDX_TRANSPORT */
#define KB_BATTERY_TRANSPORT_USB 0x01
#define KB_BATTERY_TRANSPORT_BT 0x02
#define KB_BATTERY_TRANSPORT_2G4 0x04

/* Identifies the keyboard model in the battery report (see PROTOCOL.md).
 * Override in the keyboard's config.h. */
#ifndef KB_BATTERY_MODEL_ID
#    define KB_BATTERY_MODEL_ID 0
#endif

uint8_t kb_battery_charge(void);
uint8_t kb_battery_transport(void);

/* A consistent point-in-time read of the battery state. The three fields are
 * sampled together so that change detection and report assembly observe the
 * same values instead of re-reading live globals at different moments. */
typedef struct {
    uint8_t percent;
    uint8_t charge;
    uint8_t transport;
} kb_battery_snapshot_t;

void kb_battery_snapshot(kb_battery_snapshot_t *out);
bool kb_battery_changed(const kb_battery_snapshot_t *snap);
void kb_battery_push_task(void);