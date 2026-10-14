// Copyright 2026
// SPDX-License-Identifier: GPL-2.0-or-later

#include "wls_transport.h"

#include "wls.h"
#include "wls_usb.h"
#include "wls_eeprom.h"
#include "wls_module.h"

#include "quantum.h"
#include "connection.h"
#include "usb_util.h"

/* The committed transport, and the only writer of it. Held as the `HSM_*` pair
 * so a transition is a plain value comparison.
 *
 * It starts UNKNOWN, not at a guess. The owner must not assume a transport it
 * has not decided: `connection_init()` has already loaded `config.desired_host`
 * from EEPROM (possibly `CONNECTION_HOST_AUTO`, which is not a transport at
 * all), and seeding `committed` with a real value would let a first decision
 * that happens to agree with the guess early-return without ever writing that
 * correction -- leaving upstream routing HID by a host the owner never chose.
 * Starting unknown guarantees the first commit writes both selectors. */
static wls_transport_t committed = {WLS_TRANSPORT_UNKNOWN, DEVS_USB};

/* Whether the host has been confirmed since the last decision. This is the
 * owner's *record* of the CONFIGURED/UNCONFIGURED events, not a sample of the
 * driver: the difference is the whole point. A sample would be perturbed by the
 * owner's own effect (driving the line restarts the driver); an event is not. */
static bool host_confirmed = false;

/* Whether the board has told us its wireless module is up. Until then the owner
 * performs no effect, because committing one reaches the module's ring buffer
 * before it exists (see `wls_transport_ready`). */
static bool ready = false;

static hsm_switch_apply_t hints(void) {
    return hsm_switch_apply_with_cable(hs_mode_switch_pos(), last_btdev_from_confinfo(), wls_usb_cable_present());
}

/* Commit `decision` and report whether it is a change. Applies both selectors on
 * change only, so `connection_host_changed_kb()` fires at most once per real
 * transition rather than once per observation of the line. */
static bool commit(hsm_switch_apply_t decision) {
    if (!hsm_transport_changed(decision.host, decision.devs, committed.host, committed.devs)) {
        return false;
    }
    committed.host = decision.host;
    committed.devs = decision.devs;
    connection_set_host_noeeprom((connection_host_t)decision.host);
    wls_module_devs_change(wls_module_devs(), decision.devs, false);
    return true;
}

/* Drive the USB line as the CONSEQUENCE of a committed transition.
 *
 * This is the one place the line is driven from a decision, so no caller
 * re-derives `if (changed) wls_usb_connect(...)` and none can get the direction
 * rule wrong. `input` says whether the transition came from an unconfirmed hint
 * or a confirmed event: an unconfirmed decision may only PROMOTE, because a
 * demotion from it destroys the enumeration that would confirm it (the
 * 2026-10-13 self-detach -- see `hsm_transport_may_drive_line`). A confirmed
 * event owns the demotion. */
static void drive_line_for(wls_transport_input_t input) {
    bool want_usb = (committed.host == HSM_HOST_USB);
    if (input == WLS_TRANSPORT_INPUT_HINT && !hsm_transport_may_drive_line(want_usb)) {
        return;
    }
    wls_usb_connect(want_usb);
}

bool wls_transport_decide(wls_transport_input_t input) {
    /* No effect before the module exists: committing reaches the vendor ring
     * buffer, which is uninitialized until `wireless_init()`. See
     * `wls_transport_ready`. */
    if (!hsm_transport_may_act(ready)) {
        return false;
    }
    /* Inputs only. `host_confirmed` is the owner's own state, set by events,
     * never a read of the driver the owner is about to restart. */
    hsm_switch_apply_t decision = hsm_transport_decide(hints(), host_confirmed);
    if (!commit(decision)) {
        return false;
    }
    drive_line_for(input);
    return true;
}

bool wls_transport_host_event(wls_host_event_t event) {
    /* Record the event even before ready: it is an input, not an effect, and
     * dropping it would lose the host state a later decision depends on. */
    host_confirmed = (event == WLS_HOST_EVENT_CONFIGURED);
    /* A host-lifecycle event is confirmed, so it owns the demotion. */
    return wls_transport_decide(WLS_TRANSPORT_INPUT_CONFIRMED);
}

void wls_transport_ready(void) {
    ready = true;
}

wls_transport_t wls_transport_current(void) {
    return committed;
}
