// Copyright 2026
// SPDX-License-Identifier: GPL-2.0-or-later

#include "boot_code_logic.h"

/* A group's duration: `n` pulses of `PULSE_MS` lit + `PULSE_MS` dark. */
static uint32_t group_ms(uint8_t n) {
    return (uint32_t)n * 2u * WLS_BOOT_CODE_PULSE_MS;
}

static uint8_t clamp_group(uint8_t v) {
    if (v < 1) {
        return 1;
    }
    if (v > WLS_BOOT_CODE_MAX_GROUP) {
        return WLS_BOOT_CODE_MAX_GROUP;
    }
    return v;
}

wls_boot_code_counts_t wls_boot_code_counts(wls_boot_code_t code) {
    wls_boot_code_counts_t c;
    /* The healthy case has no amber group, and its white group still reports
     * the chosen transport and role (see the plan's fault table: "A=0 (all
     * white) = healthy, and here is what I chose"). So stage 0 is the one case
     * where amber may be zero; every stage > 0 speaks at least one amber. */
    c.amber = (code.stage == WLS_BOOT_STAGE_OK) ? 0 : clamp_group(code.stage);
    c.white = clamp_group((uint8_t)(code.code + 1)); /* code 0 still speaks one white */
    return c;
}

uint32_t wls_boot_code_frame_ms(wls_boot_code_counts_t counts) {
    return group_ms(counts.amber) + WLS_BOOT_CODE_GROUP_GAP_MS + group_ms(counts.white) + WLS_BOOT_CODE_FRAME_GAP_MS;
}

uint32_t wls_boot_code_show_ms(wls_boot_code_counts_t counts) {
    /* Exactly ONE frame. A frame already IS the self-delimiting unit -- amber
     * group, group gap, white group, frame gap -- and the trailing pause is what
     * tells the reader the code has ended.
     *
     *   - SHORTER than a frame (the defect this replaces: a hand-chosen 10 000 ms
     *     against an 11 700 ms frame) ends mid-group, so only the leading pulses
     *     are shown and the count is short. On the board that read as five amber
     *     instead of seven.
     *   - LONGER than a frame lets the next frame's amber group into the window,
     *     and both groups being amber the reader counts them together (twelve,
     *     not seven) -- measured with the harness, not assumed.
     *
     * Derived, so it can never disagree with the frame. Pure. */
    return wls_boot_code_frame_ms(counts);
}

/* A pulse train starting at `start_ms`, `n` pulses of PULSE lit then PULSE
 * dark. Returns true and sets *lit when `t` is inside the train's lit half. */
static bool pulse_train(uint32_t t, uint32_t start_ms, uint8_t n, bool *lit) {
    uint32_t span = group_ms(n);
    if (t < start_ms || t >= start_ms + span) {
        return false;
    }
    uint32_t into = t - start_ms;
    *lit          = ((into / WLS_BOOT_CODE_PULSE_MS) % 2u) == 0u;
    return true;
}

wls_boot_code_out_t wls_boot_code_at(wls_boot_code_counts_t counts, uint32_t origin_ms, uint32_t now_ms) {
    wls_boot_code_out_t out = {WLS_BOOT_CODE_PHASE_DARK, 0, 0, 0};

    uint32_t frame = wls_boot_code_frame_ms(counts);
    if (frame == 0) {
        return out; /* unreachable: every frame has at least one pulse + gaps */
    }
    uint32_t t = (now_ms - origin_ms) % frame;

    bool lit = false;

    /* Amber group. */
    uint32_t amber_start = 0;
    if (pulse_train(t, amber_start, counts.amber, &lit)) {
        if (lit) {
            out.phase = WLS_BOOT_CODE_PHASE_LIT;
            out.r     = WLS_BOOT_CODE_AMBER_R;
            out.g     = WLS_BOOT_CODE_AMBER_G;
            out.b     = WLS_BOOT_CODE_AMBER_B;
        }
        return out;
    }

    /* White group, after the group gap. */
    uint32_t white_start = group_ms(counts.amber) + WLS_BOOT_CODE_GROUP_GAP_MS;
    if (pulse_train(t, white_start, counts.white, &lit)) {
        if (lit) {
            out.phase = WLS_BOOT_CODE_PHASE_LIT;
            out.r     = WLS_BOOT_CODE_WHITE_R;
            out.g     = WLS_BOOT_CODE_WHITE_G;
            out.b     = WLS_BOOT_CODE_WHITE_B;
        }
        return out;
    }

    /* Either gap. Dark. */
    return out;
}

void wls_boot_code_encode(wls_boot_code_counts_t counts, uint8_t *out) {
    out[0] = counts.amber;
    out[1] = counts.white;
}

wls_boot_code_t wls_boot_code_bootcount(uint8_t boot_count) {
    wls_boot_code_t c;
    c.stage = WLS_BOOT_STAGE_BOOTCOUNT;
    /* `wls_boot_code_counts()` reports `code + 1` as the white group, so the
     * count `n` is carried by `code = n - 1`. A zero boot count (never happens;
     * `wls_note_boot()` runs before the first paint) still speaks one white,
     * which reads as "boot 1". */
    c.code = (boot_count > 0) ? (uint8_t)(boot_count - 1) : 0;
    return c;
}

wls_boot_code_t wls_boot_code_rstcause(uint32_t rststat_bits) {
    wls_boot_code_t c;
    uint8_t         white;

    c.stage = WLS_BOOT_STAGE_RSTCAUSE;

    /* The six named causes, bits 0..5, in the order the CMSIS header defines
     * them. `white` is the bit position + 1, so white 1 = low-power reset,
     * 2 = window watchdog, 3 = independent watchdog, 4 = software, 5 = power-on,
     * 6 = external pin. The first set bit wins (a single reset normally sets
     * exactly one). */
    white = 0;
    for (uint8_t bit = 0; bit < 6; bit++) {
        if (rststat_bits & (1u << bit)) {
            white = (uint8_t)(bit + 1);
            break;
        }
    }
    /* Nothing latched (or only bits above the six we name): a distinct, non-zero
     * code. White 9 is reserved for this -- it cannot collide with the six causes
     * above and is not a valid "cause", so reading it means "the latch held
     * something we do not name", which is itself news. `wls_boot_code_counts()`
     * reports `code + 1`, so `code = 8` speaks white 9. */
    if (white == 0) {
        white = WLS_BOOT_CODE_MAX_GROUP;
    }
    c.code = (uint8_t)(white - 1);
    return c;
}

wls_boot_code_t wls_boot_facts(wls_boot_inputs_t in, uint32_t reset_cause, uint32_t boot_count) {
    wls_boot_code_t c = {WLS_BOOT_STAGE_OK, 0};

    /* 1. The chip's own reason for the last reset outranks everything. Reported
     * UNCONDITIONALLY when a cause bit is latched: the latch is hardware and
     * survives the reset it describes, whereas gating on the RAM boot counter
     * would disable the reading on exactly the boots it exists to measure. */
    if (reset_cause != 0) {
        return wls_boot_code_rstcause(reset_cause);
    }
    /* 2. A RAM boot counter is the right instrument for "has the app booted more
     * than once": that is the stage's whole question. */
    if (boot_count > 1) {
        return wls_boot_code_bootcount((uint8_t)boot_count);
    }
    /* 3. This half is not the master; its own keys do not type. */
    if (!in.master) {
        c.stage = WLS_BOOT_STAGE_ROLE;
        c.code  = 1;
        return c;
    }
    /* 4. The boot switch read was unreadable and nothing was applied. */
    if (!in.settled) {
        c.stage = WLS_BOOT_STAGE_BOOT_PASS;
        c.code  = 1;
        return c;
    }
    /* 5. Settled and master: name the transport that was chosen, so a working
     * board is also readable (no amber, white = 1/2/3 for USB/BT/2.4G). */
    if (in.host == WLS_BOOT_HOST_USB || in.host == WLS_BOOT_HOST_BLUETOOTH || in.host == WLS_BOOT_HOST_2P4GHZ) {
        c.stage = WLS_BOOT_STAGE_OK;
        c.code  = (in.host == WLS_BOOT_HOST_USB) ? 1 : (in.host == WLS_BOOT_HOST_BLUETOOTH) ? 2 : 3;
        return c;
    }
    /* 6. Settled and master, but no transport was chosen at all. */
    c.stage = WLS_BOOT_STAGE_TRANSPORT;
    c.code  = 3;
    return c;
}
