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
