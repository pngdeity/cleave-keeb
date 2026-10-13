// Copyright 2026
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

/* The settings reset, as a pure ordered plan.
 *
 * `hs_reset_settings()` was one function that did five different things (tell
 * the other half, suppress the battery indicator, wipe config and RGB and
 * keymap, flash an acknowledgement, re-run the boot pass) and was reached by a
 * direct call from two unrelated places -- the `EE_CLR` keycode's hold timer,
 * and the split slave handler. That second caller made the split module depend
 * on the lifecycle module just to call it back: a dependency cycle, exactly
 * what the board-layer dependency rule forbids. It also kept the policy ("what
 * a reset consists of, and in what order") implicit inside a straight-line
 * function where no test could see it.
 *
 * This core states the policy once. A trigger says only *why* the reset
 * happened; the plan says what must therefore be done, in order; the shell does
 * it. The one thing the trigger changes is whether we notify the peer: a reset
 * ordered by the other half must not be echoed back to the other half, which is
 * already performing its own reset. That rule is what the test pins. */

typedef enum {
    WLS_RESET_CAUSE_LOCAL = 0, /* the local user held the reset key */
    WLS_RESET_CAUSE_REMOTE,    /* the other half told us to reset */
} wls_reset_cause_t;

/* One consequence, named so the shell's executor reads as the plan. */
typedef enum {
    WLS_RESET_STEP_NOTIFY_PEER = 0, /* tell the other half to reset too */
    WLS_RESET_STEP_WIPE_CONFIG,     /* eeconfig + rgb + rgblight + keymap back to defaults */
    WLS_RESET_STEP_FLASH,           /* the acknowledgement flash */
    WLS_RESET_STEP_RERUN_BOOT,      /* re-run the boot pass */
} wls_reset_step_t;

/* The plan is at most these many steps; the array is fixed so the struct needs
 * no allocator and the whole thing stays a value type. */
#define WLS_RESET_MAX_STEPS 4

typedef struct {
    wls_reset_step_t steps[WLS_RESET_MAX_STEPS];
    uint8_t          count;
} wls_reset_plan_t;

/* The plan for a reset. Pure: no I/O, no firmware state, no clock. */
wls_reset_plan_t wls_reset_plan_of(wls_reset_cause_t cause);
