// Copyright 2026
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>

#include "transport_logic.h"

/* The single owner of the transport decision.
 *
 * Before this owner the transport was chosen in five places, each writing
 * `desired_host` and each triggering `connection_host_changed_kb()`, whose
 * `wls_usb_connect()` restarts the USB driver -- which clears `USB_ACTIVE` --
 * which was itself an input to the decision. That cycle is a livelock: the board
 * enumerated, self-detached and re-enumerated about once a second, forever (see
 * `docs/ADR-transport-authority.md`).
 *
 * The owner removes the cycle by construction:
 *
 *   - it decides from INPUTS ONLY -- the switch position, the settled `B9` cable
 *     read, and the persisted host. `USB_ACTIVE` is never an input.
 *   - host lifecycle arrives as an EVENT (`wls_transport_host_event`), pushed
 *     from the USB event queue, never polled.
 *   - it returns whether the committed transport CHANGED. The caller drives the
 *     line on that change and only on that change, so the actuator is a pure
 *     consequence and can never re-enter the decision.
 *
 * The invariant: `wls_usb_connect` may only be called as the consequence of a
 * decision change, and nothing in the line's own state may cause a decision
 * change. */

/* Host lifecycle, as an event rather than a sampled signal. */
typedef enum {
    WLS_HOST_EVENT_CONFIGURED = 0, /* the host enumerated and is talking to us */
    WLS_HOST_EVENT_UNCONFIGURED,   /* the host went away (unplug, reset, error)  */
} wls_host_event_t;

/* The committed transport. `host`/`devs` are the `HSM_*` vocabulary, so a
 * caller applies them straight through the existing selectors. `host` may be
 * `WLS_TRANSPORT_UNKNOWN` (defined in `transport_logic.h`, beside the change
 * rule) before the first decision, which matches no `HSM_HOST_*` value and so
 * can never be mistaken for a decided transport. */
typedef struct {
    uint8_t host;
    uint8_t devs;
} wls_transport_t;

/* What kind of input drove a decision, i.e. whether it may command a DEMOTION of
 * the USB line. It is a property of the *input*, not of the decision, which is
 * why it is a parameter rather than something the owner can infer.
 *
 *   HINT       -- an unconfirmed input: the switch position, the cable, the
 *                 persisted index, taken at boot or shortly after. May only ever
 *                 PROMOTE the line (see `hsm_transport_may_drive_line`), because
 *                 a demotion here destroys the enumeration that would confirm
 *                 the decision it is acting on.
 *   CONFIRMED  -- a real external event: the user moved the switch, or the host
 *                 lifecycle changed. Owns the demotion. */
typedef enum {
    WLS_TRANSPORT_INPUT_HINT = 0,
    WLS_TRANSPORT_INPUT_CONFIRMED,
} wls_transport_input_t;

/* Decide once from the inputs. This is the boot seed and the switch-edge path:
 * it reads the switch and the cable, applies the pure rule, and commits.
 *
 * It DRIVES THE USB LINE ITSELF on a real change. The owner owns the line, so no
 * caller re-derives `if (changed) wls_usb_connect(...)` and no caller can get the
 * direction rule wrong: the drive is a consequence of the transition, emitted in
 * one place. The caller passes what it is -- see `wls_transport_input_t` -- and
 * the owner applies `hsm_transport_may_drive_line` for the unconfirmed cases.
 *
 * Use this for the *hint* inputs (boot, the +100 ms re-seed, a switch move).
 * Returns true if the committed transport changed. */
bool wls_transport_decide(wls_transport_input_t input);

/* Tell the owner the host lifecycle changed. On CONFIGURED the host is a fact
 * and outranks every hint; on UNCONFIGURED the hints decide again. Returns true
 * if the committed transport changed. */
bool wls_transport_host_event(wls_host_event_t event);

/* The committed transport, for the diagnostic seam. */
wls_transport_t wls_transport_current(void);

/* Declare the owner ready to act.
 *
 * Until this is called the owner performs NO effect: `wls_transport_decide()`
 * and `wls_transport_host_event()` return false without committing anything.
 *
 * This is not defensive padding; it is required, and the reason is concrete.
 * Committing the transport calls `wireless_devs_change()`, which reaches
 * `md_send_devctrl()` -> `smsg_push()`. The smsg ring buffer is initialized only
 * by `md_init()` (via `wireless_init()`), which the board calls in
 * `wls_lifecycle_boot()` -- step 33 of `keyboard_init()`. But the owner can be
 * driven EARLIER: `protocol_setup()` (step 3, before the USB driver even exists)
 * calls `usb_device_state_init()`, which fires `notify_usb_device_state_change_kb()`
 * -- and that hook feeds this owner. `smsg_push()` on an uninitialized ring
 * dereferences a NULL buffer and hard-faults the MCU, so the board reset before
 * it ever reached the main loop (the 2026-10-13 dead-board fault).
 *
 * The board therefore calls this once its wireless module is up. Gating the
 * owner rather than the hook keeps the guarantee at the component that owns the
 * effect, so any future caller is safe by construction. */
void wls_transport_ready(void);
