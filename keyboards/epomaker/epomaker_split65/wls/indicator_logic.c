// Copyright 2026
// SPDX-License-Identifier: GPL-2.0-or-later

#include "indicator_logic.h"

wls_blink_out_t wls_blink_tick(wls_blink_t *b, uint32_t now_ms) {
    wls_blink_out_t out = {WLS_BLINK_PAINT, false};

    if (b->interval_ms == 0) {
        /* Idle: nothing to paint, stay idle. */
        return out;
    }

    if ((uint32_t)(now_ms - b->started_ms) >= b->interval_ms) {
        b->started_ms = now_ms;

        if (b->remaining) {
            b->remaining--;
        }

        if (b->remaining == 0) {
            b->interval_ms = 0;
            out.kind       = WLS_BLINK_DONE;
            out.on         = false;
            return out;
        }
    }

    /* Show the colour on the odd half-period, blank on the even one. */
    out.on = (b->remaining % 2) != 0;
    return out;
}

void wls_blink_arm(wls_blink_t *b, uint32_t interval_ms, uint8_t blinks, uint32_t now_ms) {
    b->started_ms  = now_ms;
    b->interval_ms = interval_ms;
    b->remaining   = (uint32_t)blinks * 2;
}

wls_bat_colour_t wls_bat_colour_of(bool charging, bool full, uint8_t level) {
    /* Order matters and mirrors the original: charge state first, then level.
     * The low and very-low levels are the same colour (red) -- kept as one
     * branch because the display makes no distinction. */
    if (charging && full) {
        return WLS_BAT_GREEN;
    }
    if (charging) {
        return WLS_BAT_BLUE;
    }
    if (level >= 50) {
        return WLS_BAT_GREEN;
    }
    if (level >= 30) {
        return WLS_BAT_ORANGE;
    }
    return WLS_BAT_RED;
}
