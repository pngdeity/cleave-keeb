// Copyright 2026
// SPDX-License-Identifier: GPL-2.0-or-later

#include "wls.h"
#include "usb_descriptor.h"
#include "lowpower_logic.h"
#include "transport_logic.h"
#include "wls_transport.h"
#include "wls_usb.h"
#include "wls_indicator.h"
#include "connection.h"

static ioline_t col_pins_left[MATRIX_COLS]  = MATRIX_COL_PINS;
static ioline_t col_pins_right[MATRIX_COLS] = MATRIX_COL_PINS_RIGHT;

/* The LED rail: A5/A8 (`LED_POWER_EN_PIN`/`LED_POWER_EN2_PIN`) are the
 * backlight rail ENABLE, one writer's concern. Historically the rail was written
 * ad hoc from four sites that disagreed -- a boot/wake snapshot of the RGB
 * value, the local brightness keycodes, and the cross-half 0xBB/0xCC relay --
 * so a retained-LOW write (a stray 0xBB whose matching 0xCC sender is USB-gated
 * off, or a local value-down press) could leave a half permanently dark while it
 * still typed over the split link (defect 5). Rather than add a fifth writer,
 * make the rail a pure function of the RGB value and give it one home: every
 * writer now calls this, so the rail self-derives and cannot be latched.
 *
 * ONE deliberate exception, and it is not a fifth writer: `lpwr_stop_hook_pre()`
 * (below) writes A5 LOW directly before an ordered stop, because this function
 * derives the rail from `rgb_matrix_get_val()`, which is still non-zero at that
 * moment -- routing that site through here would turn the rail back ON and
 * defeat the pre-stop kill. It is the one place the rail is forced off
 * regardless of RGB, so it stays raw and says so. */
void wls_led_rail_apply(void) {
    bool on = rgb_matrix_get_val() != 0;
#if defined(LED_POWER_EN_PIN)
    gpio_write_pin(LED_POWER_EN_PIN, on);
#endif
#if defined(LED_POWER_EN2_PIN)
    gpio_write_pin(LED_POWER_EN2_PIN, on);
#endif
}

/* The ordered-sleep flag: whether this half has been *ordered* to stop (as
 * opposed to reaching an idle timeout). One authority, one setter. It is set by
 * the low-battery path (`wls_order_sleep`) or by the master's order over the
 * split link, and cleared when a stop wakes. Reads are queries
 * (`wls_sleep_ordered`) so no consumer mutates it as a side effect. */
static bool ordered_sleep = false;

void wls_order_sleep(bool ordered) {
    ordered_sleep = ordered;
}

bool wls_sleep_ordered(void) {
    return ordered_sleep;
}

/* Boot counter for the diagnostic snapshot: increments once per MCU boot (the
 * board calls `wls_note_boot()` from `keyboard_post_init_kb`). A stable value
 * across successive diagnostic reads proves the MCU is NOT resetting; a value
 * that climbs once per observed event proves it is (the disambiguator the USB
 * self-detach investigation needed and lacked). */
static uint8_t wls_boot_count = 0;

void wls_note_boot(void) {
    wls_boot_count++;
}

/* The MCU's latched reset cause, consumed once at boot (see `wls_note_reset_cause`
 * in `wls.h`). Read-and-clear in one place so the two readers -- the LED boot
 * code and the `0xA4` diag block -- report the same value; a clear without a
 * carried read would destroy the evidence, and two reads of the latch after a
 * clear would disagree. Held as the raw bits; the codec names them. */
static uint32_t wls_reset_cause_bits = 0;

#if defined(WB32FQ95xx) && defined(RCC)
uint32_t wls_note_reset_cause(void) {
    wls_reset_cause_bits = RCC->RSTSTAT;
    RCC->CLRRSTSTAT      = RCC_CLRRSTSTAT_CLR;
    return wls_reset_cause_bits;
}
#else
uint32_t wls_note_reset_cause(void) {
    /* No reset-status register on this platform: nothing to consume. */
    return wls_reset_cause_bits;
}
#endif

uint32_t wls_reset_cause(void) {
    return wls_reset_cause_bits;
}

/* The boot transport decision, retained across the boot that made it. The
 * boot-time switch read (and its settled-ness) is the one decision that
 * `wls_diag` cannot report on the failing path, because `wls_diag` rides the
 * `0xA4` raw-HID reply and that reply needs the USB data line — exactly what a
 * bad boot tears down. Retaining the decision here means the NEXT successful
 * reply is a post-mortem of the failing boot. Owned here, beside the boot
 * counter, because it is the same fact: what this boot did. */
static wls_boot_report_t wls_boot_report;

void wls_note_boot_decision(wls_boot_report_t report) {
    wls_boot_report = report;
}

wls_boot_report_t wls_boot_decision(void) {
    return wls_boot_report;
}

/* The boot diagnostic code: the *shell* half of the codec. It knows what the
 * boot did and reads the live values; HOW those values become a stage and a code
 * is the pure `wls_boot_facts()` in `wls/boot_code_logic` (and how a code becomes
 * pulses is the rest of that file).
 *
 * This function used to hold the precedence table itself -- the last decision in
 * the board glue that no test could reach. It now gathers three values and
 * delegates, so the precedence is host-testable (`tests/boot_code_logic`).
 * The name says what it is: the shell's adapter that reads the *now*, distinct
 * from the core's `wls_boot_facts(in, reset_cause, boot_count)`. */
wls_boot_code_t wls_boot_code_now(void) {
    wls_boot_report_t r = wls_boot_report;
    wls_boot_inputs_t in;
    in.master  = r.master;
    in.settled = r.settled;
    in.host    = r.host;
    return wls_boot_facts(in, wls_reset_cause(), wls_boot_count);
}

/* The shell half of the observability seam: read each live source exactly once
 * into the snapshot. `usb_line` is the firmware's own view of the USB data
 * line, taken from the transport OWNER's committed host rather than from
 * upstream's resolved host: the committed value is the intent the actuator was
 * driven from, so this field reports what the board decided instead of what
 * upstream would resolve on its own (the two differ exactly when a confirmed
 * host outranks a switch that reads wireless). */
void wls_diag_gather(wls_diag_t *out) {
    out->switch_pos = (uint8_t)hs_mode_switch_pos();
    out->devs       = wireless_get_current_devs();
    out->host       = (uint8_t)connection_get_host_raw();
    out->boot_count = wls_boot_count;
    out->usb_line   = (wls_transport_current().host == HSM_HOST_USB) ? 1 : 0;
    out->link_state = *md_getp_state();
    /* The raw levels behind `switch_pos`, so the decode can be audited against
     * the part. `C14`/`C15` are configured as inputs by `wls_lifecycle_boot()`
     * (with their pull-ups), so a plain read reports the same lines the switch
     * rule uses. `B9` goes through `wls_usb_cable_present()`, which reads
     * `SPLIT_HAND_PIN` after `wait_us(100)` -- the same idiom upstream's
     * `is_keyboard_left_impl()` uses, and *not* a measured settle for this line:
     * the read is of a bare input with the pull-up dropped. The field is the
     * board's cable view, not a guarantee that the line was settled. */
#if defined(HS_BT_DEF_PIN) && defined(HS_2G4_DEF_PIN)
    out->pin_c14 = gpio_read_pin(HS_BT_DEF_PIN) ? 1 : 0;
    out->pin_c15 = gpio_read_pin(HS_2G4_DEF_PIN) ? 1 : 0;
#else
    out->pin_c14 = 0xFF;
    out->pin_c15 = 0xFF;
#endif
#ifdef SPLIT_HAND_PIN
    out->pin_b9 = wls_usb_cable_present() ? 1 : 0;
#else
    out->pin_b9 = 0xFF;
#endif
    /* The consumed reset cause (raw `RSTSTAT` bits, truncated to the low byte --
     * the six named causes all live in bits 0..5). Reported from the retained
     * value, never re-read from the latch: the LED code already consumed it. */
    out->reset_cause = (uint8_t)(wls_reset_cause() & 0xFF);
}

bool hs_modeio_detection(bool update) {
    static uint32_t scan_timer = 0x00;

    if ((update != true) && (timer_elapsed32(scan_timer) <= (HS_MODEIO_DETECTION_TIME))) {
        return false;
    }
    scan_timer = timer_read32();
#if defined(HS_BT_DEF_PIN) && defined(HS_2G4_DEF_PIN)
    static hsm_switch_pos_t last_pos = HSM_SWITCH_UNKNOWN;
    /* Whether the *switch* moved -- a true edge on the pin position, not a
     * comparison against the resolved transport. Comparing the two asks "does
     * the transport disagree with the pins?", which under the transport rule is
     * the *normal* state (a confirmed host outranks a lying switch); that
     * predicate stayed true every scan and re-drove the actuator, which cut the
     * data line and re-enumerated about once a second. See `hsm_switch_moved`.
     *
     * The position comes from `hs_mode_switch_pos()`, the board's one switch
     * read. This used to decode the same three macros a second time into a
     * 1/2/3/`now_mode` vocabulary only to map it straight back with
     * `hsm_switch_pos_of_mode`; two decoders of one pin read is one decoder too
     * many, and the round-trip was the only consumer of the intermediate. */
    hsm_switch_pos_t pos     = hs_mode_switch_pos();
    bool             sw_mode = hsm_switch_moved(pos, last_pos, update);
    last_pos                 = pos;
    if (sw_mode) {
        /* The switch edge applies both selectors from one read, through the
         * transport's single owner (defect 7: this once set only the device
         * index, so a runtime BT->USB flip never re-engaged USB and neither half
         * typed until a power cycle re-ran the boot path).
         *
         * The owner commits once and drives the USB line itself on a real
         * change. This site is `CONFIRMED`, unlike boot: the switch moved, a real
         * user action, and a confirmed event owns the demotion. A switch flip
         * must not route HID to the module while a host cable is present, any
         * more than the boot may (the same "attached but silent" failure);
         * unplugging a wireless-mode board stays wireless because `cable_here`
         * is then false. */
        wls_transport_decide(WLS_TRANSPORT_INPUT_CONFIRMED);
        /* `sw_mode` is the one predicate that means "the switch really moved",
         * so this is the correct place to confirm the new channel: the
         * indicator fires here and nowhere a re-assert could reach (defect:
         * see `wls_indicate_devs()`). */
        wls_indicate_devs(wireless_get_current_devs(), false);
        hs_link_activity();
        suspend_wakeup_init();
        return true;
    }
#else
    (void)update;
#endif

    return false;
}

/* The physical switch position, read from the pins alone (no edge, no persisted
 * state). The one pin read; the meaning of a position lives in
 * `transport_logic.c`, and the decision built from it lives in the transport
 * owner (`wls_transport.c`). */
hsm_switch_pos_t hs_mode_switch_pos(void) {
#if defined(HS_BT_DEF_PIN) && defined(HS_2G4_DEF_PIN)
    /* The pins become the board's numeric mode, and the mode becomes a position
     * through the one tested core mapper. This is deliberately *not* an inline
     * `if/else` chain here: the decode existed in two places (this one and the
     * detector's), one of which has now been removed, and the canonical mapping
     * is the core's job (see `hsm_switch_pos_of_mode`). */
    uint8_t now_mode = (HS_GET_MODE_PIN(HS_USB_PIN_STATE) ? 3 : (HS_GET_MODE_PIN(HS_BT_PIN_STATE) ? 1 : ((HS_GET_MODE_PIN(HS_2G4_PIN_STATE) ? 2 : 0))));
    return hsm_switch_pos_of_mode(now_mode);
#else
    return HSM_SWITCH_UNKNOWN;
#endif
}

/* The three `apply_switch_transport*` shells that used to live here are gone:
 * the transport owner (`wls_transport.c`) is now the sole writer of the
 * `{host, devs}` pair, and it decides from the same pure cores. Keeping the
 * appliers would have left unsynchronised second writers of the same state --
 * the exact defect the owner removes (`docs/ADR-transport-authority.md`). */

static hsm_link_timers_t hs_link_timers = {0, 0};
static uint8_t           hs_link_last_status;

bool hs_mode_scan(bool update) {
    if (hs_modeio_detection(update)) {
        return true;
    }
    hs_rgb_blink_hook();
    return false;
}

bool hs_keycode_allowed(uint8_t target_devs) {
    /* The switch gate, stated once. Every wireless keycode asked the same
     * three-part question inline — seed the detector from the live device index,
     * read the switch position, and test whether that position permits this
     * target — and answered it with six private copies of the predicate
     * (defect-7's shape: a rule written N times). The decision itself is the
     * tested pure `hsm_keycode_allowed`; this only supplies it the switch
     * position, from the board's one switch read.
     *
     * The persisted BT sub-index used to be threaded in here to seed the
     * detector; the detector now reads the pins directly, so it is no longer a
     * parameter. A caller that needs the persisted index still reads
     * `confinfo.last_btdevs` itself (see `wls_eeprom.c`). */
    return hsm_keycode_allowed(hs_mode_switch_pos(), target_devs);
}

/* Map the module's MD_STATE_* onto the pure core's link-state enum. The map
 * itself is `hsm_link_state_of_md` -- one rule, because the indicator needs the
 * same answer for the render and two copies is the defect class (defect 7). */
static hsm_link_state_t hs_link_state(void) {
    return hsm_link_state_of_md((uint8_t)*md_getp_state());
}

/* Restart the active link countdown from now (an activity event). The policy is
 * the pure `hsm_link_restart`; this only supplies `now`, the timeouts, and the
 * timers store. */
void hs_link_activity(void) {
    hs_link_timers = hsm_link_restart(hs_link_state(), timer_read32(), HS_LBACK_TIMEOUT, HS_SLEEP_TIMEOUT);
}

bool hs_rgb_blink_hook() {
    if (!is_keyboard_master()) {
        return false;
    }

    hsm_link_state_t state         = hs_link_state();
    bool             state_changed = (hs_link_last_status != (uint8_t)state);
    hs_link_last_status            = (uint8_t)state;

    /* The whole reconnect/sleep policy is a pure decision; this shell only
     * performs the returned action. Two independent deadlines live in
     * `hs_link_timers` (see transport_logic), never one shared timestamp. */
    hsm_link_action_t action = hsm_link_watch(state, state_changed, timer_read32(), HS_LBACK_TIMEOUT, HS_SLEEP_TIMEOUT, &hs_link_timers);

    switch (action) {
        case HSM_LINK_ACT_RECONNECT:
            md_send_devctrl(MD_SND_CMD_DEVCTRL_USB);
            wait_ms(200);
            lpwr_set_timeout_manual(true);
            break;
        case HSM_LINK_ACT_SLEEP:
            lpwr_set_timeout_manual(true);
            break;
        case HSM_LINK_ACT_NONE:
        default:
            break;
    }

    return true;
}

void lpwr_exti_init_hook(void) {
#ifdef HS_BT_DEF_PIN
    if (is_keyboard_master()) {
        gpio_set_pin_input_high(HS_BT_DEF_PIN);
        waitInputPinDelay();
        palEnableLineEvent(HS_BT_DEF_PIN, PAL_EVENT_MODE_BOTH_EDGES);
    }
#endif

#ifdef HS_2G4_DEF_PIN
    if (is_keyboard_master()) {
        gpio_set_pin_input_high(HS_2G4_DEF_PIN);
        waitInputPinDelay();
        palEnableLineEvent(HS_2G4_DEF_PIN, PAL_EVENT_MODE_BOTH_EDGES);
    }
#endif

    if (wls_sleep_ordered()) {
#if DIODE_DIRECTION == ROW2COL
        /* Drive both halves' columns high so a keypress on either half pulls
         * its row low and can wake the board. */
        for (uint8_t i = 0; i < ARRAY_SIZE(col_pins_left); i++) {
            if (col_pins_left[i] != NO_PIN) {
                gpio_set_pin_output(col_pins_left[i]);
                gpio_write_pin_high(col_pins_left[i]);
            }
        }

        for (uint8_t i = 0; i < ARRAY_SIZE(col_pins_right); i++) {
            if (col_pins_right[i] != NO_PIN) {
                gpio_set_pin_output(col_pins_right[i]);
                gpio_write_pin_high(col_pins_right[i]);
            }
        }
#endif
    }
    gpio_set_pin_input(HS_BAT_CABLE_PIN);
    waitInputPinDelay();
    palEnableLineEvent(HS_BAT_CABLE_PIN, PAL_EVENT_MODE_RISING_EDGE);
}

void palcallback_cb(uint8_t line) {
    switch (line) {
        case PAL_PAD(HS_BAT_CABLE_PIN): {
            lpwr_set_sleep_wakeupcd(LPWR_WAKEUP_CABLE);
        } break;
#ifdef HS_BT_DEF_PIN
        case PAL_PAD(HS_BT_DEF_PIN): {
            lpwr_set_sleep_wakeupcd(LPWR_WAKEUP_SWITCH);
        } break;
#endif

#ifdef HS_2G4_DEF_PIN
        case PAL_PAD(HS_2G4_DEF_PIN): {
            lpwr_set_sleep_wakeupcd(LPWR_WAKEUP_SWITCH);
        } break;
#endif
        default: {
        } break;
    }
}

void lpwr_stop_hook_pre(void) {
    gpio_write_pin_low(LED_POWER_EN_PIN);

    if (wls_sleep_ordered()) {
        md_send_devctrl(MD_SND_CMD_DEVCTRL_USB);
        wait_ms(200);
    }
}

bool lpwr_stop_is_allowed(void) {
    /* This board's sleep policy: only the master may stop, and only when it has
     * been ordered to (`wls_sleep_ordered()`), which is the low-battery path. A plain idle
     * timeout reaches STOP unordered, and on that path the wake is not
     * interpreted — the board's lpwr_stop_hook_post() is gated on the same flag,
     * and a wake code of LPWR_WAKEUP_UART returns the machine to LPWR_STOP
     * without ever running lpwr_wakeup_cb(). The half then re-enters STOP with
     * no rail re-raise and no matrix re-init: dark and unresponsive.
     *
     * NOT DEMONSTRATED. On 2026-10-14 both halves went dark on Bluetooth with no
     * host, and an earlier version of this comment claimed this path had caused
     * it. That claim was refuted by analysis: the module reported
     * `md state = 3 (DISCONNECTED)`, and `hsm_link_watch()` returns
     * HSM_LINK_ACT_SLEEP only in HSM_LINK_CONNECTED -- and that action does not
     * call `wls_order_sleep()` anyway, so it cannot make this predicate true. The
     * low-battery setter (`wls_indicator.c:177-182`) needs `bat_level == 0`, but
     * `bat_level` is `*md_getp_bat()` = `md_info.bat`, initialised to 100 and
     * written only by a module reply (`module.c:75,224`). So this predicate was
     * unreachable in that session, and the dark board had another cause -- the
     * LED rail is a pure function of the RGB value (`wls_led_rail_apply()`), so
     * it can drop with no stop at all. See docs/HARDWARE.md "Both halves went dark
     * on Bluetooth" for the corrected analysis, and docs/METHOD.md
     * Part II §0 for why the first version of this note was wrong: a mechanism was
     * written before its gates were checked.
     *
     * This is a policy, not a statement that no wake source is armed — on both
     * paths the master arms its own rows, the mode-switch pins and the cable
     * pin. What this flag actually gates is the cross-half column drive, the
     * module-sleep command, and the post-stop wake interpretation.
     *
     * Refuse any unordered stop. */
    return lpwr_stop_is_allowed_decide(is_keyboard_master(), wls_sleep_ordered());
}

uint32_t lpwr_wakeup_armed_mask(void) {
    /* The codes this board arms, derived from the pad->code attribution below
     * rather than hand-written, and asserted sound by `lpwr_excluded_code_is_safe`.
     *
     * UART is deliberately NOT armed: this board's `lpwr_exti_init()` never
     * registers UART_RX_PIN (the module's own traffic would defeat deep sleep).
     * That exclusion is only SOUND because the pad UART is reported on is also
     * armed under another code -- the WB32 EXTI reports by pad number alone
     * (`PAL_PAD(line) = line & 0x0F`), so UART_RX_PIN (C11, pad 11) and the
     * matrix column B11 (pad 11) are indistinguishable, and an edge there is
     * still reachable as LPWR_WAKEUP_MATRIX. If UART's pad were NOT otherwise
     * armed, excluding UART would discard every real edge on it.
     *
     * `lpwr_excluded_code_is_safe(excluded, covered)` is that rule, applied at
     * the point the mask is built: UART is excluded AND its pad is covered by
     * MATRIX, so the exclusion holds. Flipping either classification below makes
     * the check fail and the code armed instead -- failing toward waking rather
     * than toward a silent phantom-discard. */
    const bool uart_is_armed                 = false; /* not registered: its traffic would defeat deep sleep */
    const bool uart_pad_is_covered_by_matrix = true;  /* pad 11 is also matrix column B11 */

    const bool uart_may_be_excluded = lpwr_excluded_code_is_safe(!uart_is_armed, uart_pad_is_covered_by_matrix);

    /* Arm the code unless it is both unarmed-by-design and safe to exclude. An
     * exclusion that is not safe falls through to arming the code, so a real
     * wake is never discarded on account of a wrong attribution. */
    const bool uart_effective = uart_is_armed || !uart_may_be_excluded;

    return (uart_effective ? LPWR_WAKEUP_UART : 0) | LPWR_WAKEUP_MATRIX | LPWR_WAKEUP_CABLE | LPWR_WAKEUP_SWITCH | LPWR_WAKEUP_USB;
}

void lpwr_stop_hook_post(void) {
    /* The state transition itself is now owned by lpwr_stop_cb(), which
     * interprets the wake as a set against lpwr_wakeup_armed_mask(). All this
     * hook needs to do is drop the board's ordered-sleep flag on a real wake,
     * so the next stop starts unordered. The switch that used to live here
     * predates the wake set and would misread a multi-bit code. */
    if (wls_sleep_ordered() && lpwr_get_state() == LPWR_WAKEUP) {
        wls_order_sleep(false);
    }
}

/* The percentage itself is owned upstream: `battery_driver_sample_percent()`
 * (wls_battery_driver.c) feeds quantum/battery's cached `battery_get_percent()`
 * (see `kb_battery_snapshot()` below). */

uint8_t kb_battery_charge(void) {
    if (!charging_state) {
        return KB_BATTERY_CHARGE_DISCHARGING;
    }
    return bat_full_flag ? KB_BATTERY_CHARGE_FULL : KB_BATTERY_CHARGE_CHARGING;
}

uint8_t kb_battery_transport(void) {
    uint8_t devs = wireless_get_current_devs();

    if (devs == DEVS_USB) {
        return KB_BATTERY_TRANSPORT_USB;
    }
    if (devs == DEVS_2G4) {
        return KB_BATTERY_TRANSPORT_2G4;
    }
    return KB_BATTERY_TRANSPORT_BT;
}

bool kb_battery_changed(const kb_battery_snapshot_t *snap) {
    static uint8_t last_percent = 0xff, last_charge = 0xff, last_transport = 0xff;

    if (snap->percent == last_percent && snap->charge == last_charge && snap->transport == last_transport) {
        return false;
    }
    last_percent   = snap->percent;
    last_charge    = snap->charge;
    last_transport = snap->transport;
    return true;
}

void kb_battery_snapshot(kb_battery_snapshot_t *out) {
    /* Sample live sources exactly once, then derive every field from that
     * single sample so callers cannot observe a torn state. The level comes
     * from upstream's cached `battery_get_percent()` so the whole firmware
     * reports one consistent value. */
    out->transport = kb_battery_transport();
    out->percent   = battery_get_percent();
    out->charge    = kb_battery_charge();
}