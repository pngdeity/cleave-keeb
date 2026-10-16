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

wls_bat_rgb_t wls_bat_rgb_of(wls_bat_colour_t colour) {
    /* Moved here from the indicator shell (the FC/IS audit, F3): the shell held
     * this five-arm switch, which is a policy decision, not an effect. Values
     * are the ones the board painted before -- unchanged, so the visible
     * behaviour is identical. */
    switch (colour) {
        case WLS_BAT_GREEN:
            return (wls_bat_rgb_t){0x00, 0xFF, 0x00};
        case WLS_BAT_BLUE:
            return (wls_bat_rgb_t){0x00, 0x40, 0xFF};
        case WLS_BAT_ORANGE:
            return (wls_bat_rgb_t){0xFF, 0x80, 0x00};
        case WLS_BAT_RED:
            return (wls_bat_rgb_t){0xFF, 0x00, 0x00};
        case WLS_BAT_NONE:
        default:
            return (wls_bat_rgb_t){0x00, 0x00, 0x00};
    }
}

wls_bat_action_t wls_bat_action_of(bool charging, bool full, uint8_t level, bool request, uint8_t low) {
    /* Order mirrors the shell branch this replaced exactly (F10): full-charge
     * request first, then charging, then the low-level warning, then nothing.
     * Only the two charging cases gate on `request`; the low-level blink does
     * not, which is why `request` is ignored on the blink path -- the same
     * asymmetry the board had. */
    if (charging && full) {
        return request ? WLS_BAT_ACT_PAINT_RED : WLS_BAT_ACT_NONE;
    }
    if (charging) {
        return request ? WLS_BAT_ACT_PAINT_GREEN : WLS_BAT_ACT_NONE;
    }
    if (level <= low) {
        return WLS_BAT_ACT_BLINK_RED;
    }
    return WLS_BAT_ACT_NONE;
}

bool wls_frame_owner_is_exclusive(wls_frame_owner_t owner) {
    /* Today only the boot code is exclusive. Kept as a switch over the enum
     * rather than `owner == BOOT_CODE` so adding a second exclusive owner is a
     * decision made here, in the core, with the tests that pin it -- not a
     * silent property of a new enum value. */
    switch (owner) {
        case WLS_FRAME_OWNER_BOOT_CODE:
            return true;
        case WLS_FRAME_OWNER_NONE:
        default:
            return false;
    }
}

wls_frame_owner_t wls_frame_owner_of(bool exclusive_claim_live) {
    /* The arbiter sees only the exclusive claim, because an exclusive owner is
     * exactly what suppresses the rest. A non-exclusive painter never needs to
     * be arbitrated: it composes with its siblings by painting its own LEDs. */
    return exclusive_claim_live ? WLS_FRAME_OWNER_BOOT_CODE : WLS_FRAME_OWNER_NONE;
}
