// Copyright 2026
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Pure UART frame assembler for the CH582F module wire (keyboards/linker/
 * wireless). The module replies as framed messages; this is the *framing* half
 * only, as a byte-at-a-time state machine with no I/O and no globals, so the
 * rule "what ends a frame" is exhaustively testable apart from the bytes and the
 * meaning.
 *
 * It replaces two loose function-statics (`data_count`/`data_remain`) that once
 * lived inside the receive function and coupled buffering to interpretation:
 * position and remaining-length were implicit, reconstructed from `count`, and
 * impossible to reason about in isolation (the "one function, two jobs" smell).
 * Here the state *carries the decided grammar*: once the first byte selects a
 * frame kind, the kind and the total expected length are explicit.
 *
 * Grammar (matches `module.c`'s receive path and `module.h`'s MD_REV_CMD_*):
 *   - ACK: the 3 bytes `61 0D 0A`. `61` is a valid frame lead; whether it is
 *     really an ACK depends on its body, which the caller checks.
 *   - A known command byte (see `md_frame_is_known_cmd`): a fixed 3-byte frame,
 *     `[cmd][b1][checksum]`.
 *   - RAW (`AF 60`): `[AF][60][len][data*len][checksum]`, complete after
 *     `len + 1` bytes following the two-byte header.
 *   - Any other byte at a frame start is discarded (resync), one byte at a time.
 */

/* The grammar of the frame in progress (or of a completed one). */
typedef enum {
    MD_FRAME_NONE = 0, /* idle: no frame in progress */
    MD_FRAME_ACK,      /* 61 0D 0A */
    MD_FRAME_CMD,      /* known command, 3 bytes */
    MD_FRAME_RAW,      /* AF 60 <len> <data...> <sum> */
} md_frame_kind_t;

/* The assembler's whole state. Zero-initialised is the correct power-on state
 * ("no frame in progress"). Callers store it; they never inspect it. */
typedef struct {
    md_frame_kind_t kind;   /* grammar of the frame in progress (NONE = idle) */
    uint8_t         cmd;    /* the frame's first byte (valid while kind != NONE) */
    uint16_t        count;  /* bytes collected so far */
    uint16_t        expect; /* total bytes this frame will hold (0 = pending) */
} md_frame_state_t;

/* The outcome of feeding one byte. */
typedef struct {
    bool            complete; /* a whole frame is now held */
    md_frame_kind_t kind;     /* which grammar produced it (valid when complete) */
    uint16_t        length;   /* bytes in the completed frame (valid when complete) */
} md_frame_result_t;

/* Whether a byte is one of the command bytes the assembler frames as a 3-byte
 * frame. Exposed so the caller's buffer sizing and this grammar cannot disagree. */
bool md_frame_is_known_cmd(uint8_t byte);

/* Feed one byte. Mutates `state` in place and returns the outcome. Pure: the
 * result depends only on (`*state`, byte). The caller appends the byte to its
 * own buffer and, when `complete`, validates the checksum and dispatches. */
md_frame_result_t md_frame_feed(md_frame_state_t *state, uint8_t byte);
