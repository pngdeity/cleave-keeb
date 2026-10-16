// Copyright 2026
// SPDX-License-Identifier: GPL-2.0-or-later

#include "indicator_logic.h"

#include <stddef.h>

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

/* --- LED-index ownership ------------------------------------------------------
 *
 * Each painter's index set, over the LED index space, and the precedence the
 * frame pass runs them in. The values are the `config.h` constants the painters
 * themselves use (BT1/2/3 = 17/18/19, 2G4 = 20, HS_MATRIX_BLINK_INDEX_BAT = 63,
 * HS_MATRIX_BAT_SOFT_INDEX(_2) = 64/65), plus the two literals the shell writes:
 * BT4/BT5 on 41/42 (`wls_indicate_devs`) and the `confinfo.filp` marker on 32
 * (`epomaker_split65.c`). Nothing here is inferred; each was read in the file.
 *
 * A painter's set is what it *can* write. `wls_led_painter_of` returns the
 * highest-priority painter whose set contains the index -- the winner -- and
 * `wls_led_painters_overlap` answers "do these two sets share any index", which
 * is the relation the precedence is doing work on. */

/* Does `painter`'s declared set contain `index`? Sets are stated as the
 * predicates the shell's own code uses, so a set and its writer cannot drift. */
static bool painter_owns_index(wls_led_painter_t painter, uint8_t index) {
    switch (painter) {
        case WLS_LED_PAINTER_BOOT_CODE:
            /* rgb_matrix_set_color_all: every index. Also the index the sentinel
             * stands for, so the all-LEDs painter owns the sentinel itself. */
            return true;
        case WLS_LED_PAINTER_LINK_STATE:
            /* The REJECT cue: BT1/2/3 only (wls_indicator_link_state). */
            return index == 17 || index == 18 || index == 19;
        case WLS_LED_PAINTER_CHANNEL_FLASH:
            /* The transition flash: BT1/2/3 (17/18/19), 2.4G (20), and the
             * BT4/BT5 literals (41/42) (wls_indicate_devs). */
            return index == 17 || index == 18 || index == 19 || index == 20 || index == 41 || index == 42;
        case WLS_LED_PAINTER_CHANNEL_CUE:
            /* The at-rest channel cue: BT1/2/3 and 2.4G (wls_indicator_channel_state). */
            return index == 17 || index == 18 || index == 19 || index == 20;
        case WLS_LED_PAINTER_BATTERY:
            /* 63/64/65 (wls_indicator_battery / _blink). */
            return index == 63 || index == 64 || index == 65;
        case WLS_LED_PAINTER_SET_FLASH:
            /* The set-flash pass writes exactly the index it was armed with (or
             * all, via the sentinel). Which index is runtime state, so this
             * table cannot name one: SET_FLASH is therefore the wildcard that
             * owns only the sentinel, never a named index. */
            return index == WLS_LED_INDEX_ALL;
        case WLS_LED_PAINTER_MARKER:
            /* The confinfo.filp marker: index 32. */
            return index == 32;
        case WLS_LED_PAINTER_NONE:
        default:
            return false;
    }
}

wls_led_painter_t wls_led_painter_of(uint8_t index, bool boot_code_claim_live) {
    /* The boot code owns every LED *only while its claim is live* -- the same
     * fact the frame arbiter takes (`wls_indicator_boot_code_owns_matrix()`).
     * Taking it here keeps the LED rule conditioned on the same fact as the
     * frame rule, so the two cannot disagree: the boot code wins an index
     * exactly when it owns the frame.
     *
     * Otherwise the precedence runs highest first -- the order the frame pass
     * runs the painters, restated so the winner is a rule and not a line's
     * position. The sentinel `WLS_LED_INDEX_ALL` is owned by whichever all-LEDs
     * painter is live (the boot code here, else the set-flash sentinel path). */
    if (boot_code_claim_live) {
        return WLS_LED_PAINTER_BOOT_CODE;
    }
    static const wls_led_painter_t order[] = {
        WLS_LED_PAINTER_LINK_STATE,
        WLS_LED_PAINTER_CHANNEL_FLASH,
        WLS_LED_PAINTER_CHANNEL_CUE,
        WLS_LED_PAINTER_BATTERY,
        WLS_LED_PAINTER_SET_FLASH,
        WLS_LED_PAINTER_MARKER,
    };
    for (size_t i = 0; i < sizeof(order) / sizeof(order[0]); i++) {
        if (painter_owns_index(order[i], index)) {
            return order[i];
        }
    }
    return WLS_LED_PAINTER_NONE;
}

bool wls_led_painter_is_exclusive(wls_led_painter_t painter) {
    switch (painter) {
        case WLS_LED_PAINTER_BOOT_CODE:
            return true;
        case WLS_LED_PAINTER_LINK_STATE:
        case WLS_LED_PAINTER_CHANNEL_FLASH:
        case WLS_LED_PAINTER_CHANNEL_CUE:
        case WLS_LED_PAINTER_BATTERY:
        case WLS_LED_PAINTER_SET_FLASH:
        case WLS_LED_PAINTER_MARKER:
        case WLS_LED_PAINTER_NONE:
        default:
            return false;
    }
}

bool wls_led_painters_overlap(wls_led_painter_t a, wls_led_painter_t b) {
    /* Derived from the same sets, so "do these two overlap" cannot drift from
     * "who wins". Two painters overlap when some LED index is in both sets.
     *
     * The set-flash pass is the honest exception: it is *armed* on an index at
     * runtime, so it can write any index -- which the winner table cannot name
     * (a named index belongs to its named owner, not to the wildcard). Here the
     * question is "can these two ever touch one LED", so the set-flash pass
     * overlaps any painter that owns at least one LED. Pure. */
    if (a == WLS_LED_PAINTER_NONE || b == WLS_LED_PAINTER_NONE) {
        return false;
    }
    if (a == WLS_LED_PAINTER_SET_FLASH || b == WLS_LED_PAINTER_SET_FLASH) {
        return true;
    }
    for (uint8_t i = 0; i <= 65; i++) {
        if (painter_owns_index(a, i) && painter_owns_index(b, i)) {
            return true;
        }
    }
    if (painter_owns_index(a, WLS_LED_INDEX_ALL) && painter_owns_index(b, WLS_LED_INDEX_ALL)) {
        return true;
    }
    return false;
}

bool wls_led_can_paint(uint8_t index, bool boot_code_claim_live, bool reject_live) {
    /* The cue is the LOWEST-priority painter of the channel LEDs, so the rule is
     * simply "it owns the index, and no higher-priority live claim does". The
     * two live claims are the exclusive boot code and the link-state cue. */
    if (!painter_owns_index(WLS_LED_PAINTER_CHANNEL_CUE, index)) {
        return false;
    }
    if (boot_code_claim_live) {
        return false;
    }
    if (reject_live && painter_owns_index(WLS_LED_PAINTER_LINK_STATE, index)) {
        return false;
    }
    return true;
}
