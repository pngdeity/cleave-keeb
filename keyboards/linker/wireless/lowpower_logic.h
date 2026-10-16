// Copyright 2024 Su (@isuua)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Pure decisions extracted from the low-power state machine (`lowpower.c`).
 *
 * These are the parts of a stop that depend only on their arguments: whether a
 * stop is permitted, and whether the codes left by the wake hardware count as a
 * real wake. They carry no ChibiOS, no registers and no globals, so the unit
 * tests compile them directly — the imperative shell that touches the MCU stays
 * in `lowpower.c`.
 *
 * The wake codes are a *set* of bit flags (`lpwr_wakeupcd_t`). Comparing a set
 * against a mask, rather than switching on the last code seen, is what makes a
 * pad aliased to another function (the WB32 EXTI reports by pad number only) a
 * phantom rather than a phantom-triggered wake. */

/* Whether a stop may be taken, given the two facts the policy depends on. */
bool lpwr_stop_is_allowed_decide(bool is_master, bool lower_sleep);

/* Whether any code in the fired set belongs to the set this board arms. */
bool lpwr_wakeup_is_real(uint32_t wake_set, uint32_t armed_mask);

/* ---------------------------------------------------------------------------
 * Pad-aliasing soundness, as a pure decision.
 *
 * The WB32 EXTI reports by PAD NUMBER ALONE: `PAL_PAD(line) = line & 0x0F`
 * (chibios-contrib WB32 GPIOv1 `hal_pal_lld.h`), and `palcallback()` receives
 * that pad, not the port. So two pins on different ports with the same pad
 * number are INDISTINGUISHABLE to the wake classifier, which stamps the code of
 * whichever pin it registered under that pad.
 *
 * That is why a board may exclude a code from its armed mask (this board
 * excludes UART: the module's own traffic would defeat deep sleep) only when the
 * excluded code's reporting pad is ALSO covered by a code the board does arm --
 * otherwise a genuine edge on that pad is stamped with the excluded code and
 * silently discarded. The rule is a property of the pad->code attribution, not
 * of the mask alone: it relates the two, which is what no single file stated.
 *
 * `excluded_code_pad_is_also_armed` is that fact pre-decided: whether the pad
 * the excluded code is reported on is one the board arms under some OTHER code.
 * The decision is `!excluded || covered`: excluding a code whose pad is not
 * otherwise armed would drop a real wake, so it must be refused.
 *
 * `excluded`  -- the code is being left out of the armed mask.
 * `covered`   -- the pad the excluded code is reported on is armed under another
 *                code that the board keeps (so an edge there still wakes).
 *
 * Returns true when the mask is sound: every excluded code's pad is still
 * reachable through an armed code. Pure: no registers, no globals. */
bool lpwr_excluded_code_is_safe(bool excluded, bool covered);

/* The board's pad -> code attribution, as data.
 *
 * This is the relation `lpwr_excluded_code_is_safe` reasons about, made
 * checkable. It was prose: a table in `docs/STATE-MACHINE-pad-attribution.md` §2
 * plus a `switch` in the board's classifier, and a pin change would update the
 * code and not the table with nothing failing (D6 of `TODO.md` "Determination
 * residue").
 *
 * The rule the classifier obeys -- and the reason MATRIX is a *default* and not a
 * pin -- is: a pad reports the code of the pin registered under it, and every pad
 * no other pin claims reports MATRIX. So the attribution is exactly the set of
 * pads some pin registered under, each with the code it stamps; the list below is
 * that set, and `lpwr_code_of_pad` is the classifier.
 *
 * The pads are the ones THIS board's pins land on (the EXTI reports by pad
 * number, so a pad, not a pin, is the key): the UART RX pad, the USB detect pad
 * (`A12`), the cable pad, and the two mode-switch pads. The mode-switch pads are
 * registered by the MASTER only (see `wls.c` `lpwr_exti_init_hook`), which is why
 * `is_master` is part of the lookup.
 *
 * `default_code` is the code every unclaimed pad reports -- MATRIX on this board.
 * It is a field rather than a constant so this pure core names no firmware
 * macro: the core carries no `lowpower.h`, by its own contract. */
typedef struct {
    uint8_t  uart_pad;   /* the pin UART_RX_PIN lands on */
    uint8_t  usb_pad;    /* A12, the USB-detect pin */
    uint8_t  cable_pad;  /* HS_BAT_CABLE_PIN */
    uint8_t  bt_pad;     /* HS_BT_DEF_PIN (master only) */
    uint8_t  g24_pad;    /* HS_2G4_DEF_PIN (master only) */
    uint32_t uart_code;  /* code stamped on uart_pad */
    uint32_t usb_code;   /* code stamped on usb_pad */
    uint32_t cable_code; /* code stamped on cable_pad */
    uint32_t bt_code;    /* code stamped on bt_pad */
    uint32_t g24_code;   /* code stamped on g24_pad */
    uint32_t default_code; /* code every unclaimed pad reports (MATRIX) */
} lpwr_attribution_t;

/* The code a fired pad is stamped with, from the attribution. MATRIX is the
 * default: every pad the board does not register a specific code for.
 *
 * The board's classifier must be this function's caller, not a second copy: a
 * `switch` that restated the pads would drift from the table the mask reasons
 * about, which is exactly the residue D6 records. */
uint32_t lpwr_code_of_pad(const lpwr_attribution_t *attr, uint8_t pad, bool is_master);

/* The pads a code is reported on (the inverse lookup), so the mask can ask
 * "which pads does this code claim" without a second table. Returns
 * `LPWR_PAD_NONE` when a code is not pad-classified (e.g. a locally-set code). */
#define LPWR_PAD_NONE 0xFFu
uint8_t lpwr_pad_of_code(const lpwr_attribution_t *attr, uint32_t code, bool is_master);

/* Whether the pads `a` and `b` would be reported as the SAME pad by the EXTI --
 * i.e. they collide. `PAL_PAD` is `line & 0x0F`, so two pins on different ports
 * collide exactly when their pad numbers are equal. Stated as a function so the
 * "which pads collide" question has one home rather than being re-derived. */
bool lpwr_pads_collide(uint8_t pad_a, uint8_t pad_b);

/* ---------------------------------------------------------------------------
 * Idle-timeout admissibility, as a pure decision.
 *
 * The machine may enter PRESLEEP on an idle timeout, or immediately when a
 * one-shot *manual* override is pending. The override is consumable: once a
 * timeout-induced stop is actually allowed, the override has done its job. The
 * decision depends only on four facts, so it is pure; whether to *consume* the
 * override is the caller's act, not a side effect of asking.
 *
 * `hook_allows`    — the board hook permits a timeout stop at all.
 * `usb_active`     — the active host is USB and the USB driver is up.
 * `manual_pending` — a manual override is waiting to be honored.
 * `idle_elapsed`   — the idle timer has reached the configured timeout.
 *
 * Returns true when the machine may proceed to PRESLEEP.
 */
bool lpwr_timeout_allowed_decide(bool hook_allows, bool usb_active, bool manual_pending, bool idle_elapsed);

/* ---------------------------------------------------------------------------
 * Whether the machine may enter PRESLEEP, as a pure decision.
 *
 * This is the sequencing invariant that the 2026-10-15 dark-board fault was
 * missing. PRESLEEP has side effects it cannot undo by itself -- it disables the
 * RGB matrix -- and the only thing that reverses them runs in LPWR_WAKEUP, a
 * state a REFUSED stop never reaches (the refusal returns to LPWR_NORMAL). So a
 * stop that would be refused at the point of commitment must be refused BEFORE
 * the first irreversible effect, not after it.
 *
 * `timeout_allowed` — the idle/manual condition permits the sleep sequence.
 * `stop_allowed`    — the board's stop policy would permit the stop itself.
 *
 * Returns true only when both hold. Entering PRESLEEP on `timeout_allowed`
 * alone is the fault: it takes the side effects and then declines the stop,
 * leaving the matrix off with no path back.
 */
bool lpwr_presleep_is_allowed_decide(bool timeout_allowed, bool stop_allowed);
