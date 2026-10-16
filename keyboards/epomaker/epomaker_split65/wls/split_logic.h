// Copyright 2026
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Pure split-link protocol decisions, extracted from the board's split glue.
 *
 * The master half and the slave half exchange a fixed command byte plus a body
 * over upstream's `USER_SYNC_MMS` RPC (`transaction_rpc_exec`). The *wire* is
 * upstream's; what this file owns is the *rule*: what each command byte means,
 * and therefore what the slave should do with it. Keeping that decision pure
 * (cmd + body -> an action descriptor) means the split protocol is pinned by a
 * test rather than by matching two switch statements by hand across the two
 * halves. */

/* Command bytes the master sends (`master_to_slave_t.cmd`). */
typedef enum {
    WLS_SPLIT_CMD_NONE           = 0,
    WLS_SPLIT_CMD_SYNC_MULTIMODE = 0x55, /* sync the vendor device index to the slave */
    WLS_SPLIT_CMD_SLEEP          = 0xAA, /* order the slave to stop */
    WLS_SPLIT_CMD_POWER_DOWN     = 0xBB, /* master is suspending */
    WLS_SPLIT_CMD_WAKEUP         = 0xCC, /* master woke: re-assert the LED rail */
    WLS_SPLIT_CMD_RESET_SETTINGS = 0xDD, /* master reset its settings */
} wls_split_cmd_t;

/* The device-index range the wire may name: `DEVS_USB` (0) through `DEVS_2G4`
 * (6). The split body is untrusted input -- a corrupted byte must not become
 * board state, because the index is what the module is told to select and what
 * `hsm_host_of_devs()` maps to a transport (0xFF would read as BLUETOOTH). The
 * decision clamps to this range so the *rule* is testable and the shell cannot
 * be handed an out-of-range index at all. Mirrors `DEVS_USB`..`DEVS_2G4`. */
#define WLS_SPLIT_DEVS_MIN 0
#define WLS_SPLIT_DEVS_MAX 6

/* What the slave should do on receiving a command. Exactly one action. */
typedef enum {
    WLS_SPLIT_ACT_NONE = 0,
    WLS_SPLIT_ACT_SYNC_DEVICE_INDEX, /* apply body[0]=last, body[1]=now */
    WLS_SPLIT_ACT_ORDER_SLEEP,       /* body[0] is the ordered-sleep flag */
    WLS_SPLIT_ACT_REAPPLY_RAIL,      /* re-derive the LED rail from the RGB value */
    WLS_SPLIT_ACT_RESET_SETTINGS,    /* run the reset-settings path */
} wls_split_act_kind_t;

/* The slave's decoded command. `body` is only meaningful for actions that carry
 * arguments (see the enum above). */
typedef struct {
    wls_split_act_kind_t kind;
    uint8_t              last_devs;  /* SYNC_DEVICE_INDEX: body[0] */
    uint8_t              now_devs;   /* SYNC_DEVICE_INDEX: body[1] */
    bool                 sleep_flag; /* ORDER_SLEEP: body[0] != 0 */
} wls_split_action_t;

/* Decide the slave's action from a received command byte and its 4-byte body.
 * Pure: no I/O, no firmware state. An unknown command decodes to NONE (the slave
 * must stay silent rather than act on a byte it does not understand). */
wls_split_action_t wls_split_decide_slave(uint8_t cmd, const uint8_t body[4]);

/* ---------------------------------------------------------------------------
 * May the master order the slave to sleep, given where the master is now and
 * which transport it will wake on?
 *
 * This is the cross-half contract, and it is a SINGLE decision rather than two
 * (the master's presleep send, `wls_lifecycle_lpwr_allow_presleep()`, and the
 * slave's own wake suppression). It exists because the pair is asymmetric and
 * the asymmetry is not visible from either half alone:
 *
 *   - `WLS_SPLIT_CMD_SLEEP` (0xAA) makes the slave order ITSELF to stop and arm
 *     that stop (`wls_split.c`: `wls_order_sleep()` + `lpwr_set_timeout_manual`).
 *   - `WLS_SPLIT_CMD_WAKEUP` (0xCC) only re-applies the LED rail; it does NOT
 *     clear the slave's order. The only cancellation is the slave's own
 *     `lpwr_stop_hook_post()`, which needs the slave's OWN `LPWR_WAKEUP`.
 *   - `0xCC` has exactly ONE call site, and it returns early while the master is
 *     on USB. So on USB the master sends the slave no wake at all.
 *
 * A master that orders the slave to sleep on a path that can return to USB
 * therefore strands the slave permanently -- dark and silent, recoverable only
 * by power-cycling both halves. That was the 30 s dark board's right half.
 *
 * `sends_wake_on_this_transport` is the fact that decides it: whether the
 * master's wake command (0xCC) is actually sent on the transport in question.
 * The rule is "only order the sleep if the wake is reachable on the transport
 * the half will be woken on". Pure: no I/O, no firmware state. */
bool wls_split_can_order_sleep(bool sends_wake_on_this_transport);

/* ---------------------------------------------------------------------------
 * The order executor: a handle that cannot exist unless a release is reachable.
 *
 * `wls_split_can_order_sleep()` above *refuses* an unsafe order, but nothing
 * stops a caller from forgetting to ask, or from asking and then sending anyway:
 * the send (`wls_split_send(WLS_SPLIT_CMD_SLEEP, ...)`) is a plain call that any
 * code can make. The refusal is a convention, not a precondition.
 *
 * This type makes the right a PRECONDITION of constructing the order. The only
 * way to obtain an executor is `wls_sleep_order_open()`, which returns one ONLY
 * when the release is reachable on the transport the half is on; when it is not,
 * there is no executor, and the order cannot be built at all. "An order exists
 * without a reachable release" is therefore not a state the shell can write --
 * it is not representable. (This is the S2 rung-3 form; the property that S1's
 * rung 3 gave the low-power state, given to the cross-half order.)
 *
 * The shell still performs the send; what it may not do is decide whether it is
 * allowed. It asks the handle, and the handle is the answer: a zero handle is a
 * refusal, and `wls_sleep_order_permitted()` says so in words. */
typedef struct {
    bool    permitted;  /* false => no order may be sent (the refusal) */
    uint8_t sleep_flag; /* the body[0] the slave reads as its ordered-sleep flag */
} wls_sleep_order_t;

/* Open an order executor for the transport the master is on NOW.
 * `sends_wake_on_this_transport` is the same fact `wls_split_can_order_sleep()`
 * takes -- whether the 0xCC release is actually sent on this transport.
 * `sleep_flag` is the flag the slave will read (`wls_sleep_ordered()` for the
 * master's own stop). Returns a handle whose `permitted` is exactly the contract
 * above, so a caller can neither forget the check nor defeat it. Pure. */
wls_sleep_order_t wls_sleep_order_open(bool sends_wake_on_this_transport, uint8_t sleep_flag);

/* Whether the handle permits the order. Reads only what the open decided: the
 * answer cannot drift from the transport fact the handle was built from. */
bool wls_sleep_order_permitted(const wls_sleep_order_t *order);

/* ---------------------------------------------------------------------------
 * The order's LIVENESS: a permitted order must be released in bounded time.
 *
 * The executor above gives the slave's sleep order a SAFETY home -- an order
 * whose release is unreachable cannot be constructed. It says nothing about the
 * other half of a liveness property: an order that IS permitted may still be
 * sent and never released, and nothing above notices. A half that sent 0xAA is
 * dark until the 0xCC release arrives; if the transport it was ordered under
 * changes before that, the release is skipped (0xCC returns early on USB) and
 * the slave is stranded exactly as in the safety case, one step later.
 *
 * This is the liveness counterpart, and it must be stated differently, because
 * liveness is a property of TIME, not of a construction. The fact the master
 * holds is: the moment it last sent an order, and the release the order
 * depended on. The fault is an order outstanding past the point where the
 * release it waited for can still arrive. `wls_sleep_order_released()` is that
 * fact as a pure decision over (sent-at, now, the release the order depended
 * on, whether that release is now taken).
 *
 * The deadline exists because the master CANNOT see the slave (the RPC is
 * request/response; `slave_to_master_t` carries no persistent slave state), so
 * "the slave is asleep" is unobservable from here -- only "we ordered it and
 * have not released it for longer than the transport allows" is observable.
 * ------------------------------------------------------------------------- */

/* How long an order may stay outstanding before it is a fault. An order and its
 * release happen within one low-power cycle; this is the ceiling on that cycle,
 * so an order still outstanding after it means the release was skipped, not
 * merely late. */
#define WLS_SLEEP_ORDER_RELEASE_MS 120000

/* Whether an order outstanding since `sent_at_ms` should by now have been
 * released, given the release facts. `now_ms` is the current time.
 * `release_transport_still_carries` is whether the transport the order was sent
 * under still sends 0xCC (the release reachable at open). `released` is whether
 * the release has since been observed.
 *
 * True means: the order is a LIVENESS FAULT -- it has outlived its release and
 * the release cannot still be coming on that transport. False means the order is
 * either already released, or still within the window, or on a transport whose
 * release may yet arrive. Pure: no I/O, no firmware state. */
bool wls_sleep_order_outlived_release(uint32_t sent_at_ms, uint32_t now_ms, bool release_transport_still_carries, bool released);
