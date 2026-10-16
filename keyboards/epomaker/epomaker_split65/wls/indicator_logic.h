// Copyright 2026
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Pure decisions for the LED indicators.
 *
 * The board layer carried three near-identical blink loops (the channel flash,
 * the "set"-driven flashes, the battery blink), each re-deriving "has the
 * interval elapsed? how many flashes remain? what colour is this LED right
 * now?" inline with its own counters. That is one rule written three times --
 * the shape this project keeps finding (defect 7's class). The rule is:
 *
 *   given the flash's state and the current time, either paint a colour this
 *   tick or paint nothing, and advance the counter.
 *
 * which is a pure function of (state, now). Likewise the battery indicator's
 * colour is a pure function of (charging, full, level), so the shell can hold
 * no policy. The shell owns only the LED I/O and the timers. */

/* A single flash's timing state. `remaining` counts half-periods: the colour is
 * shown while it is odd, blank while it is even (so a flash of N "blinks" is 2N
 * half-periods). `interval_ms` of 0 means "not running". */
typedef struct {
    uint32_t started_ms;  /* when the current half-period began */
    uint32_t interval_ms; /* half-period length; 0 = idle */
    uint32_t remaining;   /* half-periods left; 0 = done */
} wls_blink_t;

/* An idle flash: nothing painted, nothing owed. Zero-initialise to this. */
#define WLS_BLINK_IDLE ((wls_blink_t){0, 0, 0})

/* The outcome of one tick of a flash. */
typedef enum {
    WLS_BLINK_PAINT = 0, /* `on` says whether to show the colour this tick */
    WLS_BLINK_DONE,      /* the flash has just finished this tick */
} wls_blink_tick_t;

typedef struct {
    wls_blink_tick_t kind;
    bool             on; /* valid for WLS_BLINK_PAINT: true = colour, false = blank */
} wls_blink_out_t;

/* Advance a flash by one tick at time `now_ms`. Mutates `b` in place and
 * returns what to paint (or that the flash just completed). Pure: depends only
 * on (`*b`, `now_ms`). An idle flash returns PAINT/off and stays idle. */
wls_blink_out_t wls_blink_tick(wls_blink_t *b, uint32_t now_ms);

/* Arm a flash: `blinks` full on/off cycles (so `remaining` = 2*blinks), starting
 * now. Pure. */
void wls_blink_arm(wls_blink_t *b, uint32_t interval_ms, uint8_t blinks, uint32_t now_ms);

/* The colour the battery indicator should show, given the charge state and
 * level. Pure: the shell paints the returned value and holds no policy. */
typedef enum {
    WLS_BAT_NONE = 0, /* nothing to show */
    WLS_BAT_GREEN,
    WLS_BAT_BLUE,
    WLS_BAT_ORANGE,
    WLS_BAT_RED,
} wls_bat_colour_t;

/* `charging`/`full` come from the charge pins; `level` is 0..100. */
wls_bat_colour_t wls_bat_colour_of(bool charging, bool full, uint8_t level);

/* The RGB a battery colour is painted with, as plain components.
 *
 * The colour→RGB map is *policy* (which bytes mean "green"), so it belongs with
 * the colour vocabulary in the core, not in the shell: previously the shell held
 * a five-arm switch re-deriving these bytes from the enum, which is exactly the
 * "shell holds no policy" violation this file exists to remove. The shell now
 * paints `r`/`g`/`b` through unchanged. `WLS_BAT_NONE` maps to all-zero (paint
 * black), which is the same as painting nothing on this rail. */
typedef struct {
    uint8_t r, g, b;
} wls_bat_rgb_t;

wls_bat_rgb_t wls_bat_rgb_of(wls_bat_colour_t colour);

/* What the charging LED should *do* this tick, given the charge state and
 * level. This is a decision, so by V3/V4 it belongs in the core: the shell
 * previously held a four-arm branch (paint red / paint green / blink red /
 * nothing) re-deriving it inline, which is the "shell holds no policy" shape.
 * The shell now applies the returned action and holds no branch. Pure.
 *
 * The three red/green paint cases come from the charge pins; `blink` is the
 * low-level warning, which is the same blink the sleep order rides on but its
 * own visible effect. */
typedef enum {
    WLS_BAT_ACT_NONE = 0,    /* leave the charging LED alone */
    WLS_BAT_ACT_PAINT_RED,   /* full-request: solid red */
    WLS_BAT_ACT_PAINT_GREEN, /* charge-request: solid green */
    WLS_BAT_ACT_BLINK_RED,   /* low level: blink red (the warning) */
} wls_bat_action_t;

/* `charging`/`full` are the charge pins, `level` is 0..100, `request` is the
 * host's/IM's charge-request flag, and `low` is the level at or below which the
 * warning blink applies. Returns the action the shell should apply. Pure. */
wls_bat_action_t wls_bat_action_of(bool charging, bool full, uint8_t level, bool request, uint8_t low);

/* --- frame ownership: which painter owns a frame, and what ends the claim ----
 *
 * Every indicator painter writes into the SAME matrix buffer in the SAME frame
 * pass, so "who is showing" is not a property of any painter -- it is a property
 * of the ORDER they run in. That order was the frame pass itself
 * (`rgb_matrix_indicators_advanced_kb`), with ownership expressed positionally:
 * the boot code claims the frame and the pass `return true`s early, so every
 * painter written after that line is silently suppressed and every painter
 * written before it silently overwrites the code. Nothing failed when either
 * happened, which is the whole defect class (docs/FINDINGS.md): a relation
 * between decisions, owned by no file.
 *
 * The rule, stated once and computed here:
 *
 *   1. **Exclusive owners suppress everything.** The boot code is an exclusive
 *      owner: while it plays, no other painter may write, because it is the one
 *      instrument that must be readable with the USB line down.
 *   2. **An exclusive claim ends by a FACT, not by a painter.** The claim is
 *      over when its deadline passes or a key dismisses it -- both readable
 *      without any paint function running. A claim whose only exit is a call the
 *      claim itself suppresses can never end (the §7 latent hazard, `TODO.md` §7).
 *   3. **Non-exclusive painters compose.** They paint in the pass's order and
 *      each covers its own LEDs; none of them owns the frame.
 *
 * This type is the claim; `wls_frame_owner_of()` is the arbitration. The shell
 * paints `owner` only when the arbiter permits, and the ordering above stops
 * being a positional accident in the pass. */

/* The painters, in precedence order, highest first. The ORDER IS THE POLICY: it
 * is the same order the frame pass ran them in, restated here so it exists once
 * and a reader does not have to reconstruct arbitration from call order.
 * `WLS_FRAME_OWNER_NONE` means no painter owns this frame. */
typedef enum {
    WLS_FRAME_OWNER_NONE = 0,
    WLS_FRAME_OWNER_BOOT_CODE, /* exclusive: the boot diagnostic code */
    /* Non-exclusive painters, in pass order, follow here as they are given
     * claims. Until then they are covered by the shell's own order. */
} wls_frame_owner_t;

/* Whether a painter owns a frame exclusively (suppresses all others). */
bool wls_frame_owner_is_exclusive(wls_frame_owner_t owner);

/* The arbitration: given that the exclusive owner's claim is live or not, which
 * owner paints this frame.
 *
 * `exclusive_claim_live` is a FACT the caller can evaluate without painting --
 * for the boot code it is `wls_indicator_boot_code_owns_matrix()`, which the §7
 * fix made compute its own deadline. Passing the fact rather than the painter
 * is the point: the claim's liveness is decided before any paint runs, so a
 * suppressed pass cannot keep a claim alive that only painting would end.
 *
 * Returns `WLS_FRAME_OWNER_BOOT_CODE` while the claim is live (the caller paints
 * the code and nothing else), else `WLS_FRAME_OWNER_NONE` (the caller runs the
 * rest of the pass). Pure. */
wls_frame_owner_t wls_frame_owner_of(bool exclusive_claim_live);

/* --- LED-index ownership: which painter owns which index, and who wins an -----
 *     overlap
 *
 * `wls_frame_owner_of()` above arbitrates the FRAME: a single exclusive owner
 * suppresses the whole pass. It deliberately says nothing about the LEDs the
 * non-exclusive painters write, and that is D3: two non-exclusive painters that
 * write the same index still resolved by *call order*, so the winner was a line
 * number and moving a paint call across another changed which indicator the
 * board showed, with nothing failing.
 *
 * The gap has a shape. Every painter writes a KNOWN, FIXED set of indices (all
 * of them are `config.h` constants or literals), except the set-flash pass,
 * whose armed entry may carry the `0xFF` "all LEDs" sentinel
 * (`wls_indicator_set_flash`, `wls_indicator.c`). So "who owns index N" is a
 * pure function of N once each painter's set and the overlap precedence are
 * stated once. That statement is this:
 *
 *   1. The painters are enumerated in the same order the frame pass runs them,
 *      highest priority first. The ORDER IS THE POLICY, restated here so a
 *      reader does not reconstruct arbitration from call order.
 *   2. A painter owns an index when the index is in its declared set; the
 *      all-LEDs painter owns every index.
 *   3. On an overlap the earlier painter (higher priority) wins. The two real
 *      overlaps today are link-state vs channel cue (17/18/19) and channel flash
 *      vs channel cue (the same LEDs); the first is already arbitrated in the
 *      shell by an early return, and this makes that arbitration a rule of the
 *      core rather than a property of the return's position.
 *
 * `WLS_LED_PAINTER_NONE` means no listed painter owns the index -- it belongs to
 * the open-ended painters (the keymap's `rgb_matrix_indicators_advanced_user`,
 * rgblight), which this core cannot enumerate and therefore does not claim. */
typedef enum {
    WLS_LED_PAINTER_NONE = 0,
    WLS_LED_PAINTER_BOOT_CODE,    /* exclusive: every index (rgb_matrix_set_color_all) */
    WLS_LED_PAINTER_LINK_STATE,   /* BT1/2/3 (17/18/19): the REJECT cue */
    WLS_LED_PAINTER_CHANNEL_FLASH,/* the transition flash (17/18/19/20, 41/42) */
    WLS_LED_PAINTER_CHANNEL_CUE,  /* the at-rest channel cue (17/18/19/20) */
    WLS_LED_PAINTER_BATTERY,      /* 63/64/65: the battery indicator */
    WLS_LED_PAINTER_SET_FLASH,    /* its armed index, or all (0xFF) */
    WLS_LED_PAINTER_MARKER,       /* the confinfo.filp marker (32) */
} wls_led_painter_t;

/* The `0xFF` entry index: an armed set-flash that repaints every LED. This is a
 * sentinel, not a real LED, so it must never be compared as an index. */
#define WLS_LED_INDEX_ALL 0xFFu

/* Which painter wins LED `index`, by the precedence above. `index` may be any
 * uint8_t and `WLS_LED_INDEX_ALL` is treated as the wildcard it is.
 *
 * `boot_code_claim_live` is the same FACT the frame arbiter takes
 * (`wls_indicator_boot_code_owns_matrix()`), because the boot code owns every
 * LED only while its claim is live. Taking it here -- rather than assuming the
 * boot code always wins -- keeps the LED rule conditioned on the same fact as
 * the frame rule, so the two cannot disagree. Pure. */
wls_led_painter_t wls_led_painter_of(uint8_t index, bool boot_code_claim_live);

/* Whether a painter owns every LED, so it composes with no other by index. The
 * boot code is the only exclusive painter: it writes every LED through
 * `rgb_matrix_set_color_all`. The set-flash pass *can* write every LED (when its
 * armed entry carries `WLS_LED_INDEX_ALL`), but it is not exclusive -- its
 * ordinary case writes one index, so it composes. Kept as a switch over the enum
 * so a new painter's exclusivity is decided here, with the tests that pin it --
 * not a silent property of a new enum value. This is D4's home. Pure. */
bool wls_led_painter_is_exclusive(wls_led_painter_t painter);

/* Whether the two painters can ever write the same LED, so the precedence above
 * is doing real work between them. Derived from the same sets `wls_led_painter_of`
 * uses, with one honest exception: the set-flash pass is armed on its index at
 * runtime, so it *can* touch any LED and overlaps anyone. Pure. */
bool wls_led_painters_overlap(wls_led_painter_t a, wls_led_painter_t b);

/* Whether the at-rest channel cue (the lowest-priority painter that writes a
 * channel LED) may paint `index` this frame.
 *
 * The cue shares 17/18/19/20 with two higher-priority painters: the exclusive
 * boot code (while its claim is live) and the link-state cue (while REJECT is
 * live). Asking rather than testing positionally means the shell's suppression
 * is the core's precedence, not the line a `return` sits on. The two liveness
 * facts are passed in because they are runtime state the core cannot read.
 * Returns true when the cue owns the index and no higher live claim does. Pure. */
bool wls_led_can_paint(uint8_t index, bool boot_code_claim_live, bool reject_live);
