// Copyright 2026
// SPDX-License-Identifier: GPL-2.0-or-later

#include "boot_blink_logic.h"

void wls_boot_blink_start(wls_boot_blink_t *s, uint32_t now_ms) {
    s->step       = 0;
    s->started_ms = now_ms;
    s->running    = true;
}

/* The half-period index within the whole schedule at time `now_ms`, or -1 if the
 * schedule has not started. Each pulse is one half-period. */
static int32_t elapsed_pulses(const wls_boot_blink_t *s, uint32_t now_ms) {
    if (!s->running) {
        return -1;
    }
    return (int32_t)((now_ms - s->started_ms) / WLS_BOOT_BLINK_STEP_MS);
}

wls_boot_blink_out_t wls_boot_blink_tick(wls_boot_blink_t *s, uint8_t step_count, const uint8_t *steps, uint32_t now_ms) {
    wls_boot_blink_out_t out = {WLS_BOOT_BLINK_IDLE_STATE, 0, false};

    if (!s->running || step_count == 0 || steps == NULL) {
        return out;
    }

    int32_t elapsed = elapsed_pulses(s, now_ms);

    /* Walk the schedule to place `elapsed` in a step. Total pulses is the sum of
     * the step counts; past it the schedule is over. */
    uint32_t total = 0;
    for (uint8_t i = 0; i < step_count; i++) {
        uint32_t pulses = steps[i] ? steps[i] : 1; /* a zero count still shows one pulse */
        if (elapsed < (int32_t)(total + pulses)) {
            s->step      = i;
            out.kind     = WLS_BOOT_BLINK_SHOW;
            out.step     = i;
            out.pulse_on = ((elapsed - (int32_t)total) % 2) == 0;
            return out;
        }
        total += pulses;
    }

    /* Past the end: report DONE once, then go idle so later calls are quiet. */
    s->running = false;
    out.kind   = WLS_BOOT_BLINK_DONE;
    return out;
}
