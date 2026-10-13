// Copyright 2026
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Pure transport-selection decisions, extracted from the board's switch scan
 * (`wls/wls.c`) so the transport state machine can be tested without the MCU.
 *
 * The physical 3-position switch is absolute authority for its own domain
 * (USB / BT / 2.4 GHz); software persistence may only model what the switch
 * cannot express — which of BT1..5, once the switch merely says "BT". The defect
 * this encodes against is a *persisted* transport surviving a reboot with the
 * switch elsewhere ("enumerated but no keys"; TODO.md defect 4). */

/* Which switch position the two pins encode. */
typedef enum {
    HSM_SWITCH_UNKNOWN = 0,
    HSM_SWITCH_USB,
    HSM_SWITCH_BT,
    HSM_SWITCH_2G4,
} hsm_switch_pos_t;

/* The switch position from the board's numeric mode (the `now_mode` a pin read
 * produces in `wls.c`: 3 = USB, 1 = BT, 2 = 2.4 GHz, anything else = unknown).
 * Lets every caller resolve through one canonical position type. */
hsm_switch_pos_t hsm_switch_pos_of_mode(uint8_t now_mode);

/* The device index the switch position selects at boot, given the persisted
 * sub-selection (`last_bt`) to use if the switch says BT. Returns one of
 * DEVS_USB / DEVS_BT1..5 / DEVS_2G4 (values 0..6, matching `module.h`). */
uint8_t hsm_boot_devs(hsm_switch_pos_t pos, uint8_t last_bt);

/* ---------------------------------------------------------------------------
 * One switch position, one transport — the pair of selectors, as a pure rule.
 *
 * The transport is chosen by TWO selectors that must agree: the vendor device
 * index (what the module is told) and upstream's connection host (which driver
 * sends HID, and whether the physical USB data line is up). Every path that
 * changes transport must set *both*, from one read of the switch, or the board
 * routes the vendor link while sending HID through the wrong driver — "split
 * brain". Historically the rule lived as three private copies (the boot path,
 * the switch edge, the keycode path); the switch-edge copy had drifted to set
 * only the device index, so a runtime BT->USB flip never re-engaged USB ("no
 * keys on either half until a power cycle"; TODO.md defect 7).
 *
 * The rule is a pure function of the switch position and the persisted BT
 * sub-index, so it is stated once here and every caller *applies* its result
 * instead of recomputing it. The sub-index exists only because the switch
 * cannot express which of BT1..5; all other positions ignore it.
 * ------------------------------------------------------------------------- */

/* The connection-host values this core returns, mirroring upstream's
 * `connection_host_t` (AUTO, NONE, USB, BLUETOOTH, 2P4GHZ -- so USB = 3,
 * BLUETOOTH = 4, 2P4GHZ = 5). Exposed so callers and tests share the vocabulary;
 * callers cast the returned `uint8_t` to `connection_host_t`. */
#define HSM_HOST_USB 3
#define HSM_HOST_BLUETOOTH 4
#define HSM_HOST_2P4GHZ 5

/* The connection host a device index maps onto: USB -> USB, 2.4 GHz -> 2P4GHZ,
 * any BT index (and anything else) -> BLUETOOTH. Values match the firmware's
 * `connection_host_t` so the shell needs no translation. This is the single
 * `devs -> host` mapping the board uses; it existed as three inline copies
 * before (defect 7). */
uint8_t hsm_host_of_devs(uint8_t devs);

/* The pair the switch position selects. `devs` is the vendor device index (as
 * `hsm_boot_devs`), `host` is the matching connection host. The caller decides
 * whether the pair persists (a user keycode: yes) or not (the switch: no, the
 * switch is re-read every boot and must not overwrite stored intent). */
typedef struct {
    uint8_t devs; /* DEVS_USB / DEVS_BT1..5 / DEVS_2G4 */
    uint8_t host; /* CONNECTION_HOST_* */
} hsm_switch_apply_t;

hsm_switch_apply_t hsm_switch_apply(hsm_switch_pos_t pos, uint8_t last_bt);

/* Whether a keycode may act, given the current switch position and target.
 * A BT key may only act in the BT position (or an unknown/forgiving one); a
 * 2.4 GHz key only in the 2.4 GHz position. This keeps a keycode from crossing
 * modes the switch does not allow. */
bool hsm_keycode_allowed(hsm_switch_pos_t pos, uint8_t target_devs);

/* The switch position for the *detector's output* mode (`wls.c`'s modeio_mode:
 * hs_none=0, hs_usb=1, hs_bt=2, hs_2g4=3, hs_wireless=4). Distinct from
 * `hsm_switch_pos_of_mode`, which maps the raw pin read (1=BT, 2=2.4G, 3=USB);
 * the detector's output uses the enum's own numbering, so the two must not be
 * conflated. `hs_wireless` is an input mode the detector never emits, so it maps
 * to UNKNOWN; `hs_none` (pins unreadable) also maps to UNKNOWN, which the rules
 * treat as the forgiving/keep-persisted case. */
hsm_switch_pos_t hsm_switch_pos_of_hs_mode(uint8_t hs_mode);

/* The device index a wireless keycode re-pairs on a long-press, or -1 when the
 * keycode has no long-press action. Pure: the keycode alone decides the target,
 * so the caller can capture the value at arm time instead of holding a pointer
 * into shared state that every later keypress overwrites (the defect this
 * encodes against). */
int16_t hsm_long_press_devs(uint16_t keycode);

/* The BT sub-profile (1..5) for a device index, or 0 when the index is not on a
 * BT profile. The profile is *derived* from the device index — the one transport
 * authority — rather than kept in a second variable that can desync and then
 * lie in the battery readback. */
uint8_t hsm_profile_of_devs(uint8_t devs);

/* The BT sub-selection to seed the mode scan with: the current device index when
 * it is already on a BT profile, otherwise the persisted fallback (`last_bt`).
 * This lets callers seed from the *live* authority (`wireless_get_current_devs()`)
 * rather than a RAM mirror of the persisted index. */
uint8_t hsm_seed_btdev(uint8_t current_devs, uint8_t last_bt);

/* The mode seed for the switch scan, from the live device index. Identity today,
 * but named so the intent ("seed the edge-detector with the live truth, not a
 * persisted copy") is explicit and testable. */
uint8_t hsm_mode_seed(uint8_t current_devs);

/* ---------------------------------------------------------------------------
 * Link watch: the reconnect/sleep countdowns, as a pure decision.
 *
 * The board runs one periodic watch over the module link state and, on two
 * *different* timeouts, either pokes the module to reconnect or orders the half
 * to sleep. Historically a single RAM timestamp served both meanings and was
 * interpreted in one fall-through pass, so the two countdowns could not be
 * reasoned about independently (the "two-cadence" smell).
 *
 * The model here is explicit: state carries two *deadlines* (absolute, in the
 * caller's timer units; 0 means "unarmed"), and this function is the only place
 * that reads them. It is pure — it takes `now` and the deadlines, returns the
 * single action to perform and the deadlines to store back — so the shell holds
 * no policy.
 * ------------------------------------------------------------------------- */

/* The link state the watch keys on, mirroring the module's MD_STATE_* (and a
 * NONE for "no link machine active"). Kept as its own enum so this pure core
 * has no firmware include graph. */
typedef enum {
    HSM_LINK_NONE = 0,
    HSM_LINK_PAIRING,
    HSM_LINK_CONNECTED,
    HSM_LINK_DISCONNECTED,
    HSM_LINK_REJECT,
} hsm_link_state_t;

/* What the watch should do this tick. Exactly one action per tick. */
typedef enum {
    HSM_LINK_ACT_NONE = 0,  /* nothing to do */
    HSM_LINK_ACT_RECONNECT, /* the disconnected countdown elapsed: poke the module */
    HSM_LINK_ACT_SLEEP,     /* the connected countdown elapsed: order sleep */
} hsm_link_action_t;

/* The watch's whole state: two independent deadlines, in the caller's timer
 * units. A deadline of 0 means unarmed. */
typedef struct {
    uint32_t reconnect_at; /* when a DISCONNECTED link should be re-poked */
    uint32_t sleep_at;     /* when a CONNECTED link should be ordered to sleep */
} hsm_link_timers_t;

/* Advance the link watch by one tick.
 *
 * `now` is the current time in the caller's units (e.g. `timer_read32()`).
 * `state_changed` is true on the first tick after the link state changed; it
 * restarts the active countdown from `now`.
 *
 * Returns the single action to perform and, via `*timers`, the deadlines to
 * store back. Pure: same inputs, same outputs. The caller (the shell) performs
 * the returned action and persists `*timers`. */
hsm_link_action_t hsm_link_watch(hsm_link_state_t state, bool state_changed, uint32_t now, uint32_t reconnect_timeout, uint32_t sleep_timeout, hsm_link_timers_t *timers);

/* Restart the active countdown from `now` (an activity event: a keypress, a
 * wake, a re-pair). Only the deadline belonging to `state` is touched; the
 * other stays cleared, so activity cannot arm a countdown for a state we are
 * not in. Pure; the shell stores the result. */
void hsm_link_restart(hsm_link_state_t state, uint32_t now, uint32_t reconnect_timeout, uint32_t sleep_timeout, hsm_link_timers_t *timers);

/* ---------------------------------------------------------------------------
 * Ordered sleep: when the battery alone justifies a stop, as a pure decision.
 *
 * A half may only take a STOP when it has been *ordered* to (the board's
 * `lpwr_stop_is_allowed()` refuses an unordered stop). One order source is the
 * low-battery path: at or below the stop threshold, while not charging, the
 * half orders itself to sleep. The decision is pure — it depends only on the
 * battery facts — so the tick that samples them holds no policy.
 * ------------------------------------------------------------------------- */

/* Whether the low-battery path should order a stop, given the battery level and
 * whether the board is charging. True only at/below `stop_threshold` while not
 * charging. */
bool hsm_should_order_sleep(uint8_t level, bool charging, uint8_t stop_threshold);
