// Copyright 2026
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Pure transport-selection decisions, extracted from the board's switch scan
 * (`wls/wls.c`) so the transport state machine can be tested without the MCU.
 *
 * The transport is a *decision over several inputs*, not the switch alone. The
 * physical switch is the user's primary affordance and the only input that
 * expresses intent, so where it is readable it is authoritative: it is read
 * through pins that can lie, but nothing else on the board says "wireless" or
 * "USB" as an intention. Two facts can override it, and both are facts rather
 * than intentions: a *confirmed* host (the USB peripheral's own enumeration
 * result, `USB_ACTIVE` -- a real host is there) and an unreadable switch read,
 * which carries no intent at all and falls back to the persisted index. See
 * `hsm_input_rule`.
 *
 * A cable is NOT one of these inputs. A cable-detect pin says "power is
 * available", not "route HID here": a charger, a power bank, a phone and a PC
 * all raise it identically, so treating it as a transport would force USB for a
 * power-only supply -- which is the requirement-2 failure (a cable with no host
 * leaves the board on a transport there is nothing to type to). The cable is
 * therefore a *power* fact and reaches no transport rule. Software persistence
 * may only model what the switch cannot express -- which of BT1..5, once the
 * switch merely says "BT" -- and serves as the fallback when the switch read is
 * unreadable. The original defect this file encodes against is a *persisted*
 * transport surviving a reboot with the switch elsewhere ("enumerated but no
 * keys"; TODO.md defect 4). */

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

/* The one place that knows what "is a BT device index" means: a value in
 * DEVS_BT1..DEVS_BT5. Callers that must clamp a possibly-corrupt index into the
 * BT range use `hsm_btdev_or`, which keeps the fallback explicit per site. */
bool    hsm_devs_is_bt(uint8_t devs);
uint8_t hsm_btdev_or(uint8_t devs, uint8_t fallback);

/* Whether a persisted confinfo block must be re-defaulted: blank, or written by
 * firmware whose layout differs (a version bump). The decision is pure; the shell
 * that owns the store performs the write. */
bool hsm_confinfo_needs_default(uint32_t raw, uint8_t version, uint8_t current_version);

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
 * `connection_host_t` (AUTO, NONE, USB, BLUETOOTH, 2P4GHZ -- so USB = 2,
 * BLUETOOTH = 3, 2P4GHZ = 4). Exposed so callers and tests share the vocabulary;
 * callers cast the returned `uint8_t` to `connection_host_t`, so these MUST stay
 * equal to the upstream enum: an off-by-one here casts straight into the wrong
 * host at `wls_transport.c` `connection_set_host_noeeprom()`. */
#define HSM_HOST_USB 2
#define HSM_HOST_BLUETOOTH 3
#define HSM_HOST_2P4GHZ 4

/* The connection host a device index maps onto: USB -> USB, 2.4 GHz -> 2P4GHZ,
 * any BT index (and anything else) -> BLUETOOTH. Values match the firmware's
 * `connection_host_t` so the shell needs no translation (see the `HSM_HOST_*`
 * block above). This is the single `devs -> host` mapping the board uses; it
 * existed as three inline copies before (defect 7). */
uint8_t hsm_host_of_devs(uint8_t devs);

/* Whether the switch position could actually be read. A pin read taken before
 * the pull-up has settled (or with the switch absent or mid-travel) yields
 * HSM_SWITCH_UNKNOWN, and an unknown position is NOT a transport decision: it
 * must not be applied. Making indeterminacy a *kind* the caller must name (see
 * `hsm_switch_apply_t` below) is what makes the boot self-detach unrepresentable
 * -- see docs/FINDINGS.md, "the boot switch read is un-settled". */
typedef enum {
    HSM_SWITCH_SETTLED = 0, /* the position was readable; `devs`/`host` are meaningful */
    HSM_SWITCH_UNREADABLE,  /* no decision: apply NOTHING and leave both selectors */
} hsm_switch_read_t;

/* The transport the switch position selects, as a variant: the `devs`/`host`
 * pair exists ONLY in the settled case, so a caller cannot construct -- or be
 * handed -- a pair that was never actually read. `devs` is the vendor device
 * index (as `hsm_boot_devs`); `host` is the matching connection host. The caller
 * decides whether the pair persists (a user keycode: yes) or not (the switch:
 * no, the switch is re-read every boot and must not overwrite stored intent).
 *
 * The shape is deliberately the same tagged-kind form as `wls_blink_out_t` and
 * `filp_action_t`: kind first, payload meaningful only for one kind. An earlier
 * design mapped unknown -> the persisted BT index -> "Bluetooth", which silently
 * tore down the USB data line at boot on the USB switch position. */
typedef struct {
    hsm_switch_read_t kind; /* HSM_SWITCH_SETTLED -> read devs/host; UNREADABLE -> neither */
    uint8_t           devs; /* DEVS_USB / DEVS_BT1..5 / DEVS_2G4 (settled only) */
    uint8_t           host; /* CONNECTION_HOST_* (settled only) */
} hsm_switch_apply_t;

hsm_switch_apply_t hsm_switch_apply(hsm_switch_pos_t pos, uint8_t last_bt);

/* The input rule: the switch is the transport, the cable is only power.
 *
 * The switch position IS the user's transport intent, and it is the only signal
 * that expresses intent at all. A cable does not: a cable to a wall charger, a
 * power bank, a phone or a PC are electrically indistinguishable at the moment
 * the decision is made, and all of them raise the cable-detect pin. Treating a
 * cable as a transport input therefore *cannot* honour "charge from a power-only
 * supply while staying on the radio" -- it can only force USB, which is exactly
 * the requirement-2 failure (a power-only cable leaving the keyboard silent on a
 * transport there is nothing to type to).
 *
 * Precedence:
 *   1. settled `pos`   -> the switch's transport. Authoritative.
 *   2. unreadable `pos` -> `DEVS_USB` when a host is confirmed (`host_confirmed`
 *      is upstream's own enumeration result, so a real host is a fact); otherwise
 *      the persisted `last_bt`. An unconfirmed unreadable read prefers USB because
 *      a boot-time pin read is the least trustworthy input and USB is the
 *      recoverable answer -- there is always a host on the other end of a real
 *      cable, and an unreadable read with no host is not a transport decision
 *      worth inventing a wireless route for.
 *
 * A confirmed host still outranks a lying switch through `host_confirmed` here,
 * which is why this rule subsumes the previous two-stage composition rather than
 * needing it: there is no separate "cable" stage to get wrong.
 *
 * Always returns HSM_SWITCH_SETTLED: this rule always produces a transport,
 * because it has a fallback for every input. Callers that must distinguish
 * "nothing was applied" use `hsm_switch_apply` instead. */
hsm_switch_apply_t hsm_input_rule(hsm_switch_pos_t pos, uint8_t last_bt, bool host_confirmed);

/* Whether committing `decision` over `committed` is a CHANGE.
 *
 * The transport owner applies a transport only on a change, so the line is
 * driven once per real transition rather than once per observation of it. This
 * is the rule that makes a self-re-arming trigger impossible to write by
 * accident, so it is stated once, purely, and tested: a caller that re-runs the
 * decision (a switch scan, a host event) gets `false` and does nothing when the
 * transport is unchanged.
 *
 * `committed_host == WLS_TRANSPORT_UNKNOWN` (0xFF) is the un-decided start, so
 * the first commit is always a change -- the owner never silently agrees with a
 * transport it did not choose. */
bool hsm_transport_changed(uint8_t decision_host, uint8_t decision_devs, uint8_t committed_host, uint8_t committed_devs);

/* The value the transport owner holds before any decision (not an `HSM_HOST_*`).
 * Named here, beside the change rule, so the rule and its sentinel cannot drift
 * apart. */
enum { WLS_TRANSPORT_UNKNOWN = 0xFF };

/* Whether a USB lifecycle notification means the host is still there.
 *
 * The notification carries upstream's `usb_configure_state_t` (NO_INIT=0,
 * INIT=1, CONFIGURED=2, SUSPEND=3), and only two of those four are a host fact:
 * `CONFIGURED` is a host talking to us, and `SUSPEND` is a host merely idling --
 * which must NOT be read as a loss, or the transport re-decides on the switch
 * hint and tears the data line down the moment the host stops polling (a
 * self-detach at idle, on any board whose switch reads wireless while cabled).
 * This returns true for `CONFIGURED` and for `SUSPEND` alike: in both, the host
 * is present and the switch stays a hint. Only a reset or a genuine unconfigure
 * (NO_INIT/INIT) reports the host gone.
 *
 * Takes the raw state as an int so the core stays a plain value over a value
 * upstream owns; the caller passes `configure_state` unmodified. */
bool hsm_host_present_on_notify(int configure_state);

/* Whether the transport owner may perform an effect yet.
 *
 * The owner's effect reaches the vendor wireless module's ring buffer
 * (`wireless_devs_change` -> `md_send_devctrl` -> `smsg_push`), which is only
 * initialized when the board calls `wireless_init()` (`md_init` -> `smsg_init`).
 * The owner can be driven before that: `protocol_setup()` calls
 * `usb_device_state_init()`, which fires the board's USB-lifecycle hook, and the
 * hook feeds the owner. Committing then dereferences the uninitialized ring's
 * NULL buffer and hard-faults the MCU -- the 2026-10-13 build reset before the
 * main loop and never enumerated (see `docs/BOOT-SEQUENCE.md`).
 *
 * This is the rule stated purely, so the guarantee is testable: an owner that is
 * not ready performs no effect, whatever it is asked. */
bool hsm_transport_may_act(bool ready);

/* Whether a caller may command the vendor wireless module.
 *
 * The same fact as `hsm_transport_may_act`, stated where the module's own owner
 * applies it: `wireless_devs_change()` reaches the module's ring buffer, which
 * exists only after `wireless_init()`. Gating the *module* rather than the
 * transport owner means every present and future caller is safe, not just the
 * one path that happened to fault. See `wls/wls_module.h`.
 *
 * This is deliberately a second name for the same predicate: the transport
 * owner's readiness and the module's readiness are the same event, but they are
 * different components, and a shared guard on one actuator is not a guard on the
 * class (the 2026-10-13 lesson). The module owns its own gate. */
bool hsm_module_may_act(bool ready);

/* Whether an UNCONFIRMED transport decision may drive the USB line.
 *
 * The USB line has two directions and they are not symmetric. Turning it ON is
 * idempotent and harmless (`wls_usb_connect(true)` early-returns when the driver
 * is already up). Turning it OFF is destructive and, without a host, irreversible
 * by the board itself.
 *
 * A boot-time (or just-after-boot) decision is made from the *hints* alone --
 * the switch, the cable, the persisted index -- because no host can have
 * confirmed yet. If that unconfirmed decision is allowed to drive the line DOWN,
 * it destroys the very thing that would confirm it: the line falls, USB never
 * enumerates, the host-lifecycle event that promotes a confirmed host over a
 * lying switch never fires, and the board sits silent on a transport it chose
 * from a pin that lies. That was the 2026-10-13 build: the switch read 2.4 GHz
 * (the C14/C15 bridge), the boot committed to it, and the board tore down the
 * line it was about to enumerate on.
 *
 * So an unconfirmed decision may only ever PROMOTE. The demotion is the business
 * of a *confirmed* event: the switch edge (a real user action) and the host
 * event hook (a real host loss). This is the monotonic-actuator principle
 * applied to the site that had escaped it.
 *
 * `decision_is_usb` is whether the unconfirmed decision chose USB. */
bool hsm_transport_may_drive_line(bool decision_is_usb);

/* Whether a transport input is PURE -- whether it belongs to the alphabet the
 * transport decision may read.
 *
 * The ADR's invariant has two halves, and this is the second: "`wls_usb_connect`
 * may only be called as the consequence of a decision change, **and nothing in
 * the line's own state may cause a decision change**." The first half (one call
 * site, on a transition only) is the actuator rule. This is the input rule, and
 * it is the one that CAUSED the livelock: `USB_ACTIVE` was an input to the
 * decision, and driving the line (a consequence of the decision) restarts the
 * driver, which clears `USB_ACTIVE`. The decision perturbed its own input, so a
 * board whose switch read disagreed with the resolved host re-decided forever at
 * the enumeration cadence.
 *
 * The rule is therefore: the transport decision reads INTENT (the switch
 * position, the persisted index) and CONFIRMED HOST FACTS DELIVERED AS EVENTS
 * (the `CONFIGURED`/`UNCONFIGURED` notification). It never reads the live state
 * of the USB line, because that is a value the decision's own effect modifies.
 *
 * `reads_line_state` says whether an input reads a live USB-line signal
 * (`usb_connected_state()`, `USB_DRIVER.state`, `USB_ACTIVE`). Returns false for
 * it: a line-state read is exactly what makes the cycle writable, so an input
 * that performs one is not a pure input no matter what else it reads. Stated
 * purely so the alphabet is a checkable set rather than a convention. */
bool hsm_transport_input_is_pure(bool reads_line_state);

/* Whether the *switch really moved* between two pin reads.
 *
 * `wls.c`'s detector runs every 50 ms and used to ask "is the resolved
 * transport different from the pins?" -- which is a different question. Under
 * the transport rule the resolved transport is *supposed* to differ from the
 * pins (a confirmed host outranks a lying switch), so that predicate stayed
 * true forever and re-drove the actuator every scan; with `wls_usb_connect` on
 * the other end, the board tore its own data line down and re-enumerated
 * roughly once a second. This is a true edge: the position now, against the
 * position last scan.
 *
 * `forced` is the detector's own "re-read now" request (its `update`
 * argument), which must report an edge regardless of history -- the caller
 * uses the return to run the switch's side effects. */
bool hsm_switch_moved(hsm_switch_pos_t now_pos, hsm_switch_pos_t last_pos, bool forced);

/* Whether a keycode may act, given the current switch position and target.
 * A BT key may only act in the BT position (or an unknown/forgiving one); a
 * 2.4 GHz key only in the 2.4 GHz position. This keeps a keycode from crossing
 * modes the switch does not allow. */
bool hsm_keycode_allowed(hsm_switch_pos_t pos, uint8_t target_devs);

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

/* ---------------------------------------------------------------------------
 * Link-state rendering: every carried state gets a visible cue, as a pure
 * decision.
 *
 * The module reports five link states and this board carries all five (see
 * `hs_link_state`), but nothing rendered them: `REJECT` -- "the module refused
 * this connection" -- was folded into the same actionless arm as the start-up
 * states and painted identically to an ordinary non-connection. That violates
 * `docs/UX-REQUIREMENTS.md` X-2: "rejected" and "not connected" need OPPOSITE
 * user actions (re-pair the host / fix its pairing settings vs simply wait or
 * re-pair), so a person cannot tell which to do. X-30 states the general rule
 * this file encodes against: a state the firmware carries is never silently
 * flattened.
 *
 * This is a *render* decision, not an action: the watch (above) decides what to
 * DO this tick, this decides what the problem is SHOWN as. Reject deliberately
 * keeps no action -- poking a module that just refused would be the livelock
 * shape ("does this effect feed back into its own trigger?"), and inventing a
 * recovery is outside "stop flattening".
 *
 * `wls_blink_t` is not reused here as a type; the cue is a static style the
 * shell paints continuously, so there is nothing to tick. Constrained by
 * X-9 (never colour alone) and X-11 (do not reuse an existing pattern with a
 * new meaning): every state below carries a *pattern* -- steady, no-paint, or
 * blink -- and where two states must be told apart they differ in more than
 * hue. */
typedef enum {
    /* Nothing painted: no link is being attempted (no link machine active, or
     * the module refused -- see the state map below). */
    HSM_LINK_CUE_NONE = 0,
    /* A bounded presence blink: a link is in progress and will resolve. */
    HSM_LINK_CUE_BLINK,
    /* A steady indicator: the state is settled, nothing is owed. */
    HSM_LINK_CUE_STEADY,
} hsm_link_cue_t;

/* The cue a link state is shown as. Pure: the shell paints what this returns
 * and holds no policy.
 *
 * The state map, stated so each is deliberate:
 *   NONE         -> NONE    no link machine is active; nothing is in progress.
 *   PAIRING      -> BLINK   an attempt is running and will resolve.
 *   CONNECTED    -> STEADY  settled.
 *   DISCONNECTED -> BLINK   the watch is already re-poking; the blink says so.
 *   REJECT       -> NONE    terminal until the person acts, and deliberately
 *                           NOT the DISCONNECTED blink: the whole point of
 *                           X-30 is that these two are never the same sight.
 *
 * The `NONE` cue is "nothing painted", which is a *reduction*, so it would still
 * be indistinguishable from a board whose indicator pass never ran. X-10's
 * "off/unavailable states are shown dim/dark-in-pattern, never absent" is
 * therefore owed as well: the shell paints REJECT/NONE as the *dim* form (a
 * low-intensity steady frame), not as nothing at all, so "deliberately quiescent"
 * and "not painted" are not the same picture. That is the shell's job -- the
 * core can only say which of the three shapes applies. */
hsm_link_cue_t hsm_link_cue_of(hsm_link_state_t state);

/* The module's own link-state value (its `MD_STATE_*`, 0..4) mapped onto this
 * core's enum. Takes the raw numeric value so this header stays free of the
 * firmware include graph. This is the ONE map: the watch's shell and the
 * indicator both resolve through it, so a state cannot be mapped two ways.
 *
 * Declared here because the two callers are different translation units
 * (`wls.c` supplies it to the watch; `wls_indicator.c` supplies it to the
 * render) -- the shared rule is the point, and duplicating it is the defect
 * class (defect 7: one rule written N times). */
hsm_link_state_t hsm_link_state_of_md(uint8_t md_state);

/* ---------------------------------------------------------------------------
 * Which channel is live *right now* — the at-rest channel cue (X-4).
 *
 * The channel was shown only at its *transition*: `wls_indicate_devs()` fires
 * from the mode-switch edge, the Fn+Q/W/E keycode path and the re-pair
 * long-press, then the LEDs return to the fill. So at rest the board says
 * nothing about which channel is selected -- while X-4 requires the active mode
 * be indicated *"while the switch is in that position"*, not merely when it
 * changed. This is the same shape as the link's REJECT flattening (X-30): a
 * carried state with no sight. The cue below is the missing sight.
 *
 * It is a *separate* vocabulary from `hsm_link_cue_t` on purpose. That cue is
 * about the LINK (is a connection being attempted, settled, refused); this one
 * is about the CHANNEL (which of USB / BT1..5 / 2.4 GHz is selected). A board
 * can be CONNECTED on channel BT2, or REJECT on BT3, and the two facts must not
 * be conflated into one indicator.
 *
 * The channel is deterministic at every instant -- `wireless_get_current_devs()`
 * -- so unlike the link cue this is a pure function of one input with no state
 * to tick. */
typedef enum {
    /* USB: the cable is the mode. Deliberately NOT given a channel LED: USB has
     * no channel indicator on this board (the four LEDs 17..20 are the wireless
     * channels only), and inventing one would be a new sight for a state the
     * switch's own position already shows. Reported so the shell can *clear*
     * any stale wireless cue rather than leaving the last channel lit. */
    HSM_CHAN_CUE_NONE = 0,
    /* BT1: LED 17 lit. */
    HSM_CHAN_CUE_BT1,
    /* BT2: LED 18 lit. */
    HSM_CHAN_CUE_BT2,
    /* BT3: LED 19 lit. */
    HSM_CHAN_CUE_BT3,
    /* BT4/BT5: the channels exist in the module but the board does not surface
     * them as keycodes (A9/B14); mapped to the same "none" as USB so the shell
     * has one clear path, and so a corrupt index cannot light a channel the
     * person never selected. */
    HSM_CHAN_CUE_BT4,
    HSM_CHAN_CUE_BT5,
    /* 2.4 GHz: LED 20 lit. */
    HSM_CHAN_CUE_2G4,
} hsm_chan_cue_t;

/* The at-rest cue for a device index. One arm per `DEVS_*`, no `default:` --
 * the same class guard as `hsm_link_cue_of`: a channel added to the module's
 * enum fails to compile here rather than silently sharing another channel's
 * sight. Values are the module's own (`DEVS_USB` = 0 .. `DEVS_2G4` = 6), taken
 * as a plain number so this header stays include-free. Pure. */
hsm_chan_cue_t hsm_chan_cue_of(uint8_t devs);

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
 * not in. Pure: returns the deadlines to store back, so a caller cannot forget
 * to persist them and the shell holds no policy. */
hsm_link_timers_t hsm_link_restart(hsm_link_state_t state, uint32_t now, uint32_t reconnect_timeout, uint32_t sleep_timeout);

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
