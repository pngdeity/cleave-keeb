// Copyright 2026
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The boot-report blink sequencer: play a fixed schedule of (colour, count)
 * steps one after another on the LEDs, so the retained boot decision is
 * readable with NO host at all.
 *
 * [Why this exists: `docs/FINDINGS.md`, "the boot switch read is un-settled".]
 *
 * The primary diagnostic seam (`wls/diag_logic`) rides the `0xA4` raw-HID
 * reply, and `wls/boot_report_logic` retains the boot decision for the *next*
 * successful reply. Both need the USB data line up -- which is exactly what a
 * bad boot tears down. This is the third exporter: a blink that needs only the
 * LEDs, which are driven from the RGB frame pass long before any host talks.
 *
 * The schedule itself is pure and already lives in `wls_boot_report_blink_step`
 * (`wls/boot_report_logic`): step 0 = switch position, step 1 = applied host,
 * step 2 = settled (one white pulse, or three red for the failure case). What
 * was missing is the *sequencer*: the indicator arrays hold flashes
 * concurrently, so showing three fields in order needs a step machine. That is
 * this file -- and only that. It holds no LED and no timer: given the schedule
 * (each step's pulse count) and the current time, it says which step to show.
 *
 * Time-driven, not completion-driven: the caller paints the current step for
 * `WLS_BOOT_BLINK_STEP_MS` per pulse and asks this core when to move on. That
 * keeps the core pure (it needs no callback from the shell) and matches the
 * cadence arithmetic the shell already does for its other flashes. */

/* Per-pulse dwell: how long one pulse of a step is shown. A step with `n`
 * pulses is shown for `n * WLS_BOOT_BLINK_STEP_MS` before the next step. */
#define WLS_BOOT_BLINK_STEP_MS 400

/* The sequencer's state: which step is playing, and when the schedule began. */
typedef struct {
    uint8_t  step;       /* index into the schedule; 0 until started/finished */
    uint32_t started_ms; /* when the whole schedule began */
    bool     running;
} wls_boot_blink_t;

/* An idle sequencer: nothing playing. Zero-initialise to this. */
#define WLS_BOOT_BLINK_IDLE ((wls_boot_blink_t){0, 0, false})

/* What the shell should paint this frame. */
typedef enum {
    WLS_BOOT_BLINK_IDLE_STATE = 0, /* not running: paint nothing, do nothing */
    WLS_BOOT_BLINK_SHOW,           /* paint `step`; `pulse_on` says colour or dark */
    WLS_BOOT_BLINK_DONE,           /* the schedule just finished this call */
} wls_boot_blink_out_kind_t;

typedef struct {
    wls_boot_blink_out_kind_t kind;
    uint8_t                   step;     /* valid for SHOW */
    bool                      pulse_on; /* valid for SHOW: colour vs dark */
} wls_boot_blink_out_t;

/* Advance the sequence to time `now_ms`. `step_count` is the schedule length
 * (`WLS_BOOT_REPORT_BLINK_STEPS`); `steps[s]` is step `s`'s pulse count, from
 * the pure `wls_boot_report_blink_step()`. Mutates `s` in place.
 *
 * Pure: depends only on (`*s`, `step_count`, `steps`, `now_ms`). A fresh (idle)
 * sequencer stays idle until `wls_boot_blink_start()`; once started it walks the
 * schedule, showing the odd half-periods lit and the even ones dark, moving on
 * when a step's `count * WLS_BOOT_BLINK_STEP_MS` has elapsed, then reports DONE
 * once and goes idle (it never restarts itself -- a boot report is played once). */
wls_boot_blink_out_t wls_boot_blink_tick(wls_boot_blink_t *s, uint8_t step_count, const uint8_t *steps, uint32_t now_ms);

/* Start the schedule from the beginning at time `now_ms`. */
void wls_boot_blink_start(wls_boot_blink_t *s, uint32_t now_ms);
