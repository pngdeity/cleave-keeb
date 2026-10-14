// Copyright 2026
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The boot diagnostic code: a two-group `(amber, white)` count, played on the
 * LED rail with no host at all.
 *
 * [Why: docs/FINDINGS.md, "the boot switch read is un-settled"; the design and
 *  the fault table are in TODO.md and docs/BOARD-LAYER.md.]
 *
 * The previous instrument (`wls/boot_report_logic` + `wls/boot_blink_logic`)
 * played a *sequence of differently-keyed fields*: step 0 = switch position,
 * step 1 = applied host, step 2 = settled, step 3 = role, each with a different
 * pulse count and duration. It was unreadable in practice, for four reasons:
 *
 *   1. It was not exclusive: `wls_indicator_set_flash()` ran after it in the
 *      same frame pass and could repaint the whole matrix (index 0xFF), so the
 *      frames the eye saw depended on unrelated state.
 *   2. Per-step durations differed (400 ms *per half-period*, so a step lasted
 *      `n * 400 ms`), so counting required the spec open.
 *   3. No frame delimiter and no repetition: it played once, so a missed start
 *      was unrecoverable.
 *   4. Ordering: it was started at boot step 33 but only painted once the RGB
 *      frame pass runs, so its phase had advanced before the first paint.
 *
 * This file is the replacement *specification*, pure and testable. It follows
 * the convention the user pointed at, Dell's Latitude E-series diagnostic LED:
 * a code is reported as `A` flashes of one colour then `W` flashes of another,
 * "the first number represents the number of amber-colored flashes, the second
 * number the number of white-colored flashes" (Dell KB 000131044). The count
 * carries the value; the colour only partitions the code into two groups.
 *
 * The played frame is
 *
 *     [amber x A] --1.5 s-- [white x W] --3 s-- (repeat, until dismissed)
 *
 * with a uniform pulse period (on and off equal), so the three time scales are
 * self-delimiting and distinguishable: the pulse period, the 1.5 s intra-code
 * gap, and the 3 s inter-frame pause. It loops, so a late first paint is
 * correct rather than mid-frame, and a missed start is recoverable.
 *
 * This file holds the *rule* only: the codec from boot facts to (A, W), and the
 * pure sequencer from time to a phase. It owns no LED and no timer. The shell
 * asks it what to paint and paints it. */

/* --- timing (ms) --------------------------------------------------------- */

/* One pulse: lit for this long, then dark for the same, so the period is
 * `2 * WLS_BOOT_CODE_PULSE_MS`. Measured reachable: the RGB frame is gated at
 * 16 ms (`RGB_MATRIX_LED_FLUSH_LIMIT`, `docs/HARDWARE.md` "RGB matrix frame
 * timing"), so a 300 ms pulse spans ~19 frames and survives unless the main
 * loop stalls beyond ~150 ms. */
#define WLS_BOOT_CODE_PULSE_MS 300

/* The gap between the amber group and the white group (Dell: 1.5 s). */
#define WLS_BOOT_CODE_GROUP_GAP_MS 1500

/* The pause after the white group, before the frame repeats (Dell: 3 s). */
#define WLS_BOOT_CODE_FRAME_GAP_MS 3000

/* The two group colours. Amber and white are the Dell convention; there is no
 * meaning in the specific values beyond "one warm, one neutral", so a reader
 * distinguishes the groups by hue, not by brightness. */
#define WLS_BOOT_CODE_AMBER_R 0xFF
#define WLS_BOOT_CODE_AMBER_G 0x60
#define WLS_BOOT_CODE_AMBER_B 0x00
#define WLS_BOOT_CODE_WHITE_R 0xFF
#define WLS_BOOT_CODE_WHITE_G 0xFF
#define WLS_BOOT_CODE_WHITE_B 0xFF

/* --- the boot facts the code is derived from ----------------------------- */

/* What the shell observed, reduced to exactly the facts the code needs. Kept
 * as plain values (no pointers, no board types) so the codec stays pure and
 * testable and the vocabulary is one place.
 *
 * `stage` is the coarse category (the amber count). The shell sets it from the
 * furthest stage it proved it reached; see the fault table in the plan record
 * and `docs/BOARD-LAYER.md`. `code` is the specific fault within the stage (the
 * white count). `A == 0` is the healthy case: no amber, and the white group
 * reports the chosen transport and role, so a *working* board is also readable.
 *
 * The vocabulary is deliberately revisitable: adding a stage or a code must not
 * require touching the sequencer. */
typedef struct {
    uint8_t stage; /* 0 = healthy; 1.. = the boot stage that failed (amber count) */
    uint8_t code;  /* the specific fault within the stage (white count) */
} wls_boot_code_t;

/* The coarse stages. `WLS_BOOT_STAGE_OK` (0) is the healthy case; the others
 * localise the fault to a region of the boot sequence. These are the amber
 * groups. */
enum {
    WLS_BOOT_STAGE_OK         = 0, /* ran, and chose a transport it can type on    */
    WLS_BOOT_STAGE_PREINIT    = 1, /* faulted before our boot pass (steps 1-32)     */
    WLS_BOOT_STAGE_BOOT_PASS  = 2, /* our boot pass (step 33) could not decide      */
    WLS_BOOT_STAGE_TRANSPORT  = 3, /* ran, but routed keystrokes to no listener     */
    WLS_BOOT_STAGE_ROLE       = 4, /* this half is not the master                   */
    WLS_BOOT_STAGE_INCOMPLETE = 5, /* reached the loop but the transport is unset   */
    WLS_BOOT_STAGE_BOOTCOUNT  = 6, /* reporting the MCU boot count, not a fault     */
    WLS_BOOT_STAGE_RSTCAUSE   = 7  /* reporting the latched MCU reset cause         */
};

/* The stage partition above is the *instrument's own contract*: it decides what
 * every reading means, so it is the last decision that must not live in a shell
 * where no test can reach it. This is that rule, pure and host-testable.
 *
 * Inputs are exactly the values the shell already holds, passed as plain values
 * (no pointers, no board types) so the file stays include-free:
 *   `master`       -- this half's own `is_keyboard_master()` answer at boot
 *   `settled`      -- the boot switch read was readable (something was applied)
 *   `host`         -- the retained transport (3=USB,4=BT,5=2.4G; matches HSM_HOST_*)
 *   `reset_cause`  -- the latched `RCC->RSTSTAT` bits (0 = nothing latched)
 *   `boot_count`   -- the RAM boot counter (1 = first boot this power session)
 *
 * Precedence, highest first:
 *   1. `reset_cause != 0` -> `RSTCAUSE` (white = bit+1). UNGATED on purpose: the
 *      cause latch is hardware and survives the reset it describes, whereas the
 *      RAM boot counter is wiped by every reset -- gating on `boot_count > 1`
 *      would disable the reading on exactly the boots it exists to measure.
 *   2. `boot_count > 1`   -> `BOOTCOUNT` (white = the count). Here a RAM counter
 *      is the right instrument: the stage's whole question is "has the app booted
 *      more than once".
 *   3. `!master`          -> `ROLE`, code 1 (this half is the slave).
 *   4. `!settled`         -> `BOOT_PASS`, code 1 (switch unreadable, nothing applied).
 *   5. `host` is a real transport -> `OK`, code 1/2/3 for USB/BT/2.4G.
 *   6. otherwise          -> `TRANSPORT`, code 3 (no transport chosen at all).
 *
 * The per-stage codes are revisitable; only the stage partition is fixed. Pure. */
typedef struct {
    bool    master;
    bool    settled;
    uint8_t host; /* 3=USB,4=BT,5=2.4G; anything else = none */
} wls_boot_inputs_t;

/* The transport vocabulary, copied here for the same reason `HSM_DEVS_*` is
 * copied into `transport_logic` -- an include-free core is host-testable. The
 * values match `HSM_HOST_*`; a board-side static assert pins them (F7). */
#define WLS_BOOT_HOST_USB 3
#define WLS_BOOT_HOST_BLUETOOTH 4
#define WLS_BOOT_HOST_2P4GHZ 5

wls_boot_code_t wls_boot_facts(wls_boot_inputs_t in, uint32_t reset_cause, uint32_t boot_count);

/* The fault-table-free code that reports the MCU boot counter.
 *
 * A reset loop cannot be proved by any channel that needs the running app -- the
 * reset destroys the channel. The LED survives it (the rail is powered before the
 * switch is read), so the counter rides here: six amber, then `n` white for boot
 * number `n`. Replug and watch the white group climb; if it changes between
 * replugs the MCU is resetting, if it holds the app is stable.
 *
 * The reader's rule is unchanged -- amber is the category, white the value -- so
 * this is a *stage*, not a third group, and nothing has to be re-learned. Stage 6
 * is free. The count is clamped by `wls_boot_code_counts()` like any other white
 * group (1..`WLS_BOOT_CODE_MAX_GROUP`); a runaway loop pins at the maximum, which
 * is itself the reading. `wls_boot_count` also stays in the host DIAG block for a
 * finer count when the USB path is alive. */
wls_boot_code_t wls_boot_code_bootcount(uint8_t boot_count);

/* The fault-table-free code that reports the MCU's OWN reset cause.
 *
 * The chip latches why it last reset in `RCC->RSTSTAT`, six named bits
 * (low-power / window-watchdog / independent-watchdog / software / power-on /
 * external-pin), and holds it until `RCC->CLRRSTSTAT` is written. That is
 * strictly better than inferring a reset: it names the cause and it survives the
 * reset, so the *next* boot can report what killed the last one. Nothing else in
 * the tree reads or clears it, so the value is intact when this runs.
 *
 * The cause is a one-hot bitfield, so it is mapped onto a code in this stage:
 * seven amber, then `n` white, where `n` is the bit position + 1 (1..6) for a
 * known cause, or `WLS_BOOT_CODE_MAX_GROUP`-style reserved high value for more
 * than one bit set at once (which should not happen for a single reset). This is
 * a *stage*, so the reader rule is unchanged: amber is the category, white the
 * value.
 *
 * The caller passes the raw `RSTSTAT` word; this stays a plain value function so
 * it is testable with no hardware. `0` (nothing latched, e.g. after an explicit
 * clear with no reset since) maps to a single white -- a distinct, non-zero code,
 * because a spoken group is never zero-length. */
wls_boot_code_t wls_boot_code_rstcause(uint32_t rststat_bits);

/* The largest amber/white group the sequencer must play. The codec clamps to
 * this so a bad value cannot make the code unreadably long; callers read back
 * the clamped counts from `wls_boot_code_counts()`. */
#define WLS_BOOT_CODE_MAX_GROUP 9

/* The (amber, white) counts for a code. Pure. Both are clamped to
 * 1..WLS_BOOT_CODE_MAX_GROUP: a group is never zero-length *within* a spoken
 * code, because a zero group is indistinguishable from a missing delimiter --
 * a stage with code 0 therefore reports one white pulse, not none. (The healthy
 * case is `stage == 0`, which is a distinct code, not an empty amber group.) */
typedef struct {
    uint8_t amber;
    uint8_t white;
} wls_boot_code_counts_t;

wls_boot_code_counts_t wls_boot_code_counts(wls_boot_code_t code);

/* --- the sequencer ------------------------------------------------------- */

/* What the shell should paint this frame. */
typedef enum {
    WLS_BOOT_CODE_PHASE_LIT = 0, /* paint the current group's colour */
    WLS_BOOT_CODE_PHASE_DARK,    /* paint nothing (within a pulse's off half, or a gap) */
} wls_boot_code_phase_t;

typedef struct {
    wls_boot_code_phase_t phase;
    uint8_t               r, g, b; /* valid for LIT */
} wls_boot_code_out_t;

/* The whole played frame, in ms. A frame is `2*PULSE*A + GROUP_GAP +
 * 2*PULSE*W + FRAME_GAP`: the two pulse trains plus the two gaps. Pure. */
uint32_t wls_boot_code_frame_ms(wls_boot_code_counts_t counts);

/* How long the code must be shown, in ms: exactly ONE frame.
 *
 * A frame already IS the self-delimiting unit (amber group, group gap, white
 * group, and the trailing frame gap that tells the reader the code has ended),
 * so the window is the frame and nothing else. Both failure directions are real
 * and were measured: a window SHORTER than a frame ends mid-group and the count
 * comes out short (the 10 000 ms constant against an 11 700 ms frame read as
 * five amber instead of seven on the board), and a window LONGER than a frame
 * lets the next frame's amber group into the count (twelve amber, not seven).
 *
 * Derived from the frame, so the two can never disagree. Pure. */
uint32_t wls_boot_code_show_ms(wls_boot_code_counts_t counts);

/* The phase at `now_ms`, given the instant the code started (`origin_ms`).
 * Pure: the caller passes the two clock readings it genuinely has and need not
 * know the layout.
 *
 * The origin is a VALUE rather than a moment the shell remembers, so the
 * "how late may the first paint be" question is answered here, in a test,
 * instead of in a comment. `now_ms - origin_ms` is taken modulo the frame
 * length, so callers may pass ever-growing clock values.
 *
 * NOTE (the contract this replaces): the previous signature took only
 * `elapsed_ms` and its comment claimed a late first paint was "still correct".
 * It was not -- a late start shifted the whole frame, so the leading pulses
 * were consumed while invisible and the count came out short. The count is only
 * recovered because the show window (`wls_boot_code_show_ms`) now guarantees a
 * complete second frame; the origin itself is still free. */
wls_boot_code_out_t wls_boot_code_at(wls_boot_code_counts_t counts, uint32_t origin_ms, uint32_t now_ms);

/* --- the host-visible exporter ------------------------------------------- */

/* How many bytes `wls_boot_code_encode()` writes. Two counts, one byte each. */
#define WLS_BOOT_CODE_ENC_SIZE 2

/* Encode the counts into `out` (at least WLS_BOOT_CODE_ENC_SIZE bytes) as
 * `{amber, white}`, so the `0xA4` reply carries the SAME code the LED plays and
 * a host reader cannot drift from what the eye counts. Pure. */
void wls_boot_code_encode(wls_boot_code_counts_t counts, uint8_t *out);
