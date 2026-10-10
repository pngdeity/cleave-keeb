// Copyright 2026
// SPDX-License-Identifier: GPL-2.0-or-later

#include "md_frame.h"

/* Command bytes that begin a fixed 3-byte frame. Duplicated from module.h's
 * MD_REV_CMD_* so this file stays free of the firmware include graph (and thus
 * testable). `md_frame_is_known_cmd` is the single list the shell and the core
 * both consult, so they cannot disagree about what starts a frame. */
#define MD_FRAME_CMD_ACK_LEAD 0x61
#define MD_FRAME_CMD_RAW 0xAF
#define MD_FRAME_CMD_RAW_OUT 0x60
#define MD_FRAME_CMD_INDICATOR 0x5A
#define MD_FRAME_CMD_DEVCTRL 0x5B
#define MD_FRAME_CMD_BATVOL 0x5C
#define MD_FRAME_CMD_FW_VERSION 0x5D
#define MD_FRAME_CMD_HOST_STATE 0x60

bool md_frame_is_known_cmd(uint8_t byte) {
    switch (byte) {
        case MD_FRAME_CMD_RAW:
        case MD_FRAME_CMD_INDICATOR:
        case MD_FRAME_CMD_DEVCTRL:
        case MD_FRAME_CMD_BATVOL:
        case MD_FRAME_CMD_FW_VERSION:
        case MD_FRAME_CMD_HOST_STATE:
        case MD_FRAME_CMD_ACK_LEAD:
            return true;
        default:
            return false;
    }
}

md_frame_result_t md_frame_feed(md_frame_state_t *state, uint8_t byte) {
    md_frame_result_t r = {false, MD_FRAME_NONE, 0};

    switch (state->kind) {
        case MD_FRAME_NONE: {
            /* A frame starts only on a byte we recognise; anything else is
             * discarded and we stay idle (resync, one byte at a time). */
            if (md_frame_is_known_cmd(byte)) {
                state->kind   = MD_FRAME_CMD;
                state->cmd    = byte;
                state->count  = 1;
                state->expect = 3; /* [cmd][b1][checksum] by default */
            }
            return r;
        }

        case MD_FRAME_CMD: {
            state->count++;
            if (state->count == 2) {
                /* The second byte can promote a RAW pair (`AF 60`) to the
                 * variable-length grammar; otherwise this stays a 3-byte frame
                 * and the next byte is its checksum. `expect` stays 0 as the
                 * "length byte not yet seen" marker for the RAW grammar. */
                if (state->cmd == MD_FRAME_CMD_RAW && byte == MD_FRAME_CMD_RAW_OUT) {
                    state->kind   = MD_FRAME_RAW;
                    state->expect = 0;
                }
                return r;
            }
            /* Fixed 3-byte frame complete. An ACK-lead frame is classified ACK
             * so the caller can dispatch without re-reading the bytes; whether
             * its body really says ACK is the caller's check. */
            r.complete   = true;
            r.kind       = (state->cmd == MD_FRAME_CMD_ACK_LEAD) ? MD_FRAME_ACK : MD_FRAME_CMD;
            r.length     = state->count;
            state->kind  = MD_FRAME_NONE;
            state->count = 0;
            return r;
        }

        case MD_FRAME_RAW: {
            if (state->expect == 0) {
                /* The length byte: total = [AF][60][len] + len data + checksum. */
                state->expect = (uint16_t)(3 + byte + 1);
                state->count  = 3;
                return r;
            }
            state->count++;
            if (state->count >= state->expect) {
                r.complete   = true;
                r.kind       = MD_FRAME_RAW;
                r.length     = state->count;
                state->kind  = MD_FRAME_NONE;
                state->count = 0;
            }
            return r;
        }

        default: {
            /* Defensive: an unknown kind cannot be advanced; return to idle. */
            state->kind  = MD_FRAME_NONE;
            state->count = 0;
            return r;
        }
    }
}
