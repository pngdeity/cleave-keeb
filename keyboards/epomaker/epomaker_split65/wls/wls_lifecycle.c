// Copyright 2026
// SPDX-License-Identifier: GPL-2.0-or-later

#include "wls_lifecycle.h"

#include "wls/wls.h"
#include "wls/wls_eeprom.h"
#include "wls/wls_split.h"
#include "wls/split_logic.h"
#include "wls/reset_logic.h"
#include "wls/wls_indicator.h"
#include "wls/transport_logic.h"
#include "wls/wls_transport.h"
#include "wls/wls_module.h"
#include "wls/wls_usb.h"
#include "quantum.h"
#include "usb_util.h"

#ifdef WIRELESS_ENABLE
#    include "wireless.h"
#    include "usb_main.h"
#    include "lowpower.h"
#    include "module.h"
#endif

bool charging_state = false;
bool bat_full_flag  = false;

#ifdef WIRELESS_ENABLE
static uint32_t post_init_timer = 0x00;
#endif

void wls_lifecycle_boot(void) {
#ifdef LED_POWER_EN_PIN
    gpio_set_pin_output(LED_POWER_EN_PIN);
#endif

#ifdef LED_POWER_EN2_PIN
    gpio_set_pin_output(LED_POWER_EN2_PIN);
#endif

    /* Rail starts from its single owner (defect 5). */
    wls_led_rail_apply();

#ifdef HS_BT_DEF_PIN
    gpio_set_pin_input_high(HS_BT_DEF_PIN);
#endif

#ifdef HS_2G4_DEF_PIN
    gpio_set_pin_input_high(HS_2G4_DEF_PIN);
#endif

#ifdef USB_POWER_EN_PIN
    gpio_write_pin_low(USB_POWER_EN_PIN);
    gpio_set_pin_output(USB_POWER_EN_PIN);
#endif

#ifdef HS_BAT_CABLE_PIN
    gpio_set_pin_input(HS_BAT_CABLE_PIN);
#endif

#ifdef BAT_FULL_PIN
    gpio_set_pin_input_high(BAT_FULL_PIN);
#endif

#ifdef WIRELESS_ENABLE
    wireless_init();
    /* The module exists now (`wireless_init` -> `md_init` -> `smsg_init`).
     * Tell both components that own an effect reaching it: the transport owner
     * (its commit path) and the module gate (every `wireless_devs_change` on
     * this board). Before this point either would reach `smsg_push()` on an
     * uninitialized ring and hard-fault -- which is what `protocol_setup()`'s
     * USB notification did on the 2026-10-13 build, resetting the MCU before
     * the main loop. See `wls_transport_ready` and `wls/wls_module.h`. */
    wls_transport_ready();
    wls_module_ready();
#    if (!(defined(HS_BT_DEF_PIN) && defined(HS_2G4_DEF_PIN)))
    wls_module_devs_change(!confinfo.devs, confinfo.devs, false);
#    endif

    /* Apply the physical switch LEVEL to the connection host here, in post-init
     * — after upstream's `connection_init()` has read EEPROM but before the
     * first `keyboard_task()`/`host_task()` in the main loop. Without this the
     * first `host_task()` resolves `config.desired_host` from stale EEPROM
     * before the switch is read, and (with the vendor `set_transport()` swap
     * gone, item 14d) the board would route a report through a driver whose
     * module is not initialized. This is the same switch-authority seed as the
     * 100 ms pass in `wireless_post_task()`, moved earlier so no report can
     * precede it; that pass remains for boards/branches we do not reach here. */
#    if defined(HS_BT_DEF_PIN) && defined(HS_2G4_DEF_PIN)
    {
        /* The pull-ups above were enabled in this same pass, and `palSetLineMode`
         * takes effect asynchronously: a read taken now can see the old floating
         * level and yield HSM_SWITCH_UNKNOWN. Wait for the pin to settle before
         * the first read (the sleep path already uses this idiom). */
        waitInputPinDelay();

        /* Seed the transport from the switch before any report can be sent: the
         * shared applier sets BOTH selectors (host + device index) from one
         * switch read, so no report can precede the switch's authority. */
        /* Drive the USB data line from the host we *applied*, not from a re-read
         * of `connection_get_host()`: `connection_set_host_noeeprom` early-returns
         * when EEPROM already holds this host, so a re-read can be stale and drop
         * the line. One read, one decision, applied consistently. */
        hsm_switch_pos_t pos = hs_mode_switch_pos();
        /* Commit the transport through its single owner: the owner decides from
         * the switch, the settled `B9` cable read and the persisted index only,
         * and -- on a real change -- drives the USB line itself from the
         * committed value. The caller only classifies the input.
         *
         * Boot is `HINT`: the host cannot have confirmed yet (no CONFIGURED
         * event has arrived), so this is the hint decision -- exactly right,
         * because a PSU must not be mistaken for a host. As a hint it may only
         * PROMOTE the line: an unconfirmed decision that tore the line down
         * would destroy the very event that would outrank it (the 2026-10-13
         * build -- the switch read 2.4 GHz via the C14/C15 bridge, boot
         * committed to it, and the board cut the line it was about to enumerate
         * on). The demotion belongs to the switch edge and the host event hook,
         * both confirmed. */
        wls_transport_decide(WLS_TRANSPORT_INPUT_HINT);
        wls_transport_t applied = wls_transport_current();

        /* Retain the decision for the diagnostic seam. The 0xA4 diag block is
         * sampled live and needs the USB line; when a bad boot tears that line
         * down, this retained copy is the only record of what the boot decided.
         *
         * `settled` reports the SWITCH READ, not the apply result: the
         * cable-aware rule always settles, so deriving it from `applied.kind`
         * would always be true and the diagnostic would lose the unreadable
         * case it exists to show. `switch_pos` already carries UNKNOWN directly,
         * so the field is derived from it. */
        wls_note_boot_decision((wls_boot_report_t){.switch_pos = (uint8_t)pos, .host = applied.host, .settled = (pos != HSM_SWITCH_UNKNOWN), .master = is_keyboard_master()});
        /* The readout of that decision is NOT started here. This runs at step 33
         * of `keyboard_init()`, before the RGB frame pass exists, so a schedule
         * armed here would advance its phase before the first paint -- and a
         * boot that died before the loop started would have "shown" nothing.
         * `wls_indicator_boot_code_start()` is called from the paint pass
         * instead, where the frame is live (the ordering fix; see
         * `wls/boot_code_logic.h`). */

        /* The USB line was driven by the owner inside `wls_transport_decide()`,
         * as a hint (promote-only). Nothing to do here. */
    }
#    endif
    post_init_timer = timer_read32();
#endif
}

#ifdef WIRELESS_ENABLE

void wls_lifecycle_suspend_power_down_kb(void) {
    /* Rail from its single owner, not a raw pin write (defect 5). A suspend can
     * fire from the USB stack before our own boot pass has run, so this hook
     * must not assume the rail pins are configured here -- `wls_led_rail_apply`
     * derives them from the current RGB value and is safe at any point. */
    wls_led_rail_apply();
}

void wls_lifecycle_suspend_wakeup_init_kb(void) {
    /* Rail from its single owner (defect 5). */
    wls_led_rail_apply();

    /* Re-poke the link through the module gate: before the module is up this is
     * a no-op rather than a fault (this hook can fire from the USB stack during
     * `protocol_setup()`, before `wireless_init()`). */
    wls_module_devs_change(wls_module_devs(), wls_module_devs(), false);
}

void wls_lifecycle_suspend_wakeup_init_user(void) {
    usart_init();

    /* The 0xCC/0xBB pair relay LED-rail power to the slave, so they only make
     * sense on battery. While cabled the rail must stay up on both halves;
     * broadcasting it would extinguish the slave's backlight. */
    if (wireless_get_current_devs() == DEVS_USB) {
        return;
    }

    if (wls_split_send(WLS_SPLIT_CMD_WAKEUP, NULL)) {
        /* The release has been sent: the outstanding order is discharged. This
         * is the observable that answers the liveness question -- until it
         * arrives, an order sent earlier is still outstanding. */
        wls_note_sleep_order_released();
        dprintf("slave wakeup sent\n");
    } else {
        dprint("slave wakeup failed!\n");
    }
}

void wls_lifecycle_suspend_power_down_user(void) {
    if (wireless_get_current_devs() == DEVS_USB) {
        return;
    }

    if (wls_split_send(WLS_SPLIT_CMD_POWER_DOWN, NULL)) {
        dprintf("slave power-down sent\n");
    } else {
        dprint("slave power-down failed!\n");
    }
}

bool wls_lifecycle_lpwr_allow_timeout(void) {
    /* A half may only fall into the timeout path when it is the master and is
     * not on USB. Whether the resulting stop is one this board may take is
     * decided once, at the point of commitment: the sleep-policy contract
     * (`lpwr_stop_is_allowed()`, enforced in the shared lowpower.c) refuses an
     * unordered stop there, so the rule lives in one place rather than at each
     * entry. */
    if (!is_keyboard_master()) {
        return false;
    }

    if (wireless_get_current_devs() == DEVS_USB) {
        return false;
    }

    return true;
}

bool wls_lifecycle_lpwr_allow_presleep(void) {
    if (is_keyboard_master()) {
        /* The cross-half contract, as a PRECONDITION rather than a check: the
         * order executor cannot be opened unless the release is reachable on the
         * transport the master is on now. `0xCC` is never sent while the master
         * is on USB, so no order can even be constructed there -- the one-way
         * order that stranded the slave's half (dark and silent until both halves
         * are power-cycled) is not representable. See
         * `docs/STATE-MACHINE-stop-wake-split.md` §6 rung 3. */
        bool              sends_wake = (wireless_get_current_devs() != DEVS_USB);
        wls_sleep_order_t order      = wls_sleep_order_open(sends_wake, wls_sleep_ordered());
        if (wls_sleep_order_permitted(&order)) {
            const uint8_t body[4] = {order.sleep_flag, 0, 0, 0};
            if (wls_split_send(WLS_SPLIT_CMD_SLEEP, body)) {
                /* Record the order so its LIVENESS is checkable: the send time,
                 * and whether a release is even reachable on this transport. A
                 * permitted order whose release is skipped (0xCC returns early
                 * on USB) strands the slave until both halves are power-cycled;
                 * without this record nothing could notice. See
                 * `wls_sleep_order_outlived_release()` (split_logic). */
                wls_note_sleep_order_sent(sends_wake);
                dprintf("slave sleep sent\n");
            } else {
                dprint("slave sleep failed!\n");
            }
        }

        if (wireless_get_current_devs() != DEVS_USB) {
            palSetLineMode(SERIAL_USART_RX_PIN, PAL_OUTPUT_TYPE_OPENDRAIN);
            palSetLineMode(SERIAL_USART_TX_PIN, PAL_OUTPUT_TYPE_OPENDRAIN);
        }
    }

    /* Drop the USB data line before sleeping, but ONLY when it is not an active
     * host connection. The old guard was `!charging_state`, which is unsound:
     * `HS_BAT_CABLE_PIN` is a shared "power present" bit (docs/HARDWARE.md,
     * "Split-link power"), not a charger detect, so on a host's own USB port it
     * can read true or false for reasons unrelated to whether the host is using
     * the line. With the switch read UNKNOWN the applied transport is nothing,
     * so `wireless_get_current_devs()` keeps a stale `DEVS_USB`; the old guard
     * then held for the whole session and this hook cut a line the host was
     * actively using, every time the low-power machine reached pre-sleep — the
     * host saw disconnect/re-enumerate and the board came back to the same
     * state (the periodic reset loop in docs/FINDINGS.md). A line in USB_ACTIVE
     * is enumerated and in use; tearing it down is never a low-power action.
     * The sibling `lpwr_is_allow_timeout()` (lowpower.c) states the same "on
     * USB" test, so this now agrees with it instead of contradicting it. */
    if ((wireless_get_current_devs() == DEVS_USB) && (USB_DRIVER.state != USB_ACTIVE)) {
        if (USB_DRIVER.state != USB_STOP) {
            usb_power_disconnect();
            usbDisconnectBus(&USBD1);
            usbStop(&USBD1);
        }
    }
    return true;
}

void wls_lifecycle_lpwr_wakeup(void) {
    hs_mode_scan(false);

    /* Rail from its single owner (defect 5). */
    wls_led_rail_apply();
}

void wls_lifecycle_wireless_post_task(void) {
    // auto switching devs
    if (post_init_timer && timer_elapsed32(post_init_timer) >= 100) {
        md_send_devctrl(MD_SND_CMD_DEVCTRL_FW_VERSION);   // get the module fw version.
        md_send_devctrl(MD_SND_CMD_DEVCTRL_SLEEP_BT_EN);  // timeout 30min to sleep in bt mode, enable
        md_send_devctrl(MD_SND_CMD_DEVCTRL_SLEEP_2G4_EN); // timeout 30min to sleep in 2.4g mode, enable
        /* The physical switch is authoritative at boot. Restoring the persisted
         * index blindly let a stale wireless selection survive a reboot with the
         * switch on USB — the board came up on a dead wireless link while USB
         * stayed enumerated ("enumerated but no keys"; defect 4). Routing the
         * re-seed through the transport OWNER (not a direct apply) keeps this
         * from being a second writer of the same state: the owner commits once
         * and -- on a real change -- drives the line itself. It is a
         * switch-driven change, so it does not persist (14c). Persistence still
         * supplies the BT1..5 sub-selection the switch cannot express.
         *
         * `HINT`, like the boot site: this runs ~100 ms after boot, before a
         * host can have confirmed, so it may only PROMOTE. A demotion here would
         * cut the line the confirmation is about to arrive on (the 2026-10-13
         * self-detach). */
        wls_transport_decide(WLS_TRANSPORT_INPUT_HINT);
        post_init_timer = 0x00;
    }
#    if defined(HS_BT_DEF_PIN) && defined(HS_2G4_DEF_PIN)
    hs_mode_scan(false);
#    endif
}

#endif /* WIRELESS_ENABLE */

void wls_lifecycle_housekeeping(void) {
#ifdef HS_BAT_CABLE_PIN
    charging_state = gpio_read_pin(HS_BAT_CABLE_PIN);
#endif

#ifdef BAT_FULL_PIN
    bat_full_flag = gpio_read_pin(BAT_FULL_PIN);
#endif

    /* Single owner for the LED rail: re-derive A5/A8 from the current RGB value
     * on every half. See `wls_led_rail_apply()`. */
    wls_led_rail_apply();

    /* The transport's stage-2 trigger is NOT here. It arrives as a USB
     * lifecycle EVENT through `notify_usb_device_state_change_kb()` (see
     * `epomaker_split65.c`), which feeds `wls_transport_host_event()`. Polling
     * was the defect: `usb_connected_state()` is a signal our own effect clears
     * (driving the line restarts the driver), so a poll of it re-armed itself
     * and produced a livelock at the enumeration cadence. An event is not
     * perturbed by our effect, so it cannot recurse. See
     * `docs/ADR-transport-authority.md`. */

#ifdef WIRELESS_ENABLE
    uint8_t hs_now_mode;
    if (charging_state && (bat_full_flag)) {
        hs_now_mode = MD_SND_CMD_DEVCTRL_CHARGING_DONE;
    } else if (charging_state) {
        hs_now_mode = MD_SND_CMD_DEVCTRL_CHARGING;
    } else {
        hs_now_mode = MD_SND_CMD_DEVCTRL_CHARGING_STOP;
    }

    static uint32_t hs_current_time;
    if (!hs_current_time || timer_elapsed32(hs_current_time) > WLS_MODULE_POLL_MS) {
        hs_current_time = timer_read32();
        md_send_devctrl(hs_now_mode);
        md_send_devctrl(MD_SND_CMD_DEVCTRL_INQVOL);

        /* The cross-half order's liveness check rides the same poll. It is a
         * report, not an effect (see `hs_sleep_order_liveness_check`), so it
         * belongs on the tick that already samples the board's slow facts. */
        hs_sleep_order_liveness_check();
    }
#endif
}

/* Execute the reset plan. The *policy* (which consequences, in what order, and
 * the one thing the cause changes) is the pure `wls_reset_plan_of()`
 * (wls/reset_logic); this function only performs the steps it names. Two
 * unrelated triggers reach it -- the `EE_CLR` hold timer (LOCAL) and the split
 * slave handler (REMOTE); the cause is the whole reason that handler no longer
 * needs to call back into the lifecycle module. */
void wls_reset_settings(wls_reset_cause_t cause) {
    wls_reset_plan_t plan = wls_reset_plan_of(cause);

    for (uint8_t i = 0; i < plan.count; i++) {
        switch (plan.steps[i]) {
            case WLS_RESET_STEP_NOTIFY_PEER:
                if (is_keyboard_master()) {
                    if (wls_split_send(WLS_SPLIT_CMD_RESET_SETTINGS, NULL)) {
                        dprintf("slave reset-settings sent\n");
                    } else {
                        dprintf("slave reset-settings failed!\n");
                    }
                }
                break;
            case WLS_RESET_STEP_WIPE_CONFIG:
                wls_bat_suppress_arm();
                wls_eeprom_wipe_config();
                break;
            case WLS_RESET_STEP_FLASH:
                /* The acknowledgement flash; a refusal here ends the reset. */
                if (hs_reset_settings_user() != true) {
                    return;
                }
                break;
            case WLS_RESET_STEP_RERUN_BOOT:
                hs_link_activity();
                wls_lifecycle_boot();
                break;
        }
    }
}
