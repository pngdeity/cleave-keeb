// Copyright 2026
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Pure encoder/decoder for the firmware's own state snapshot.
 *
 * The shell had no way to be observed: there is no host serial console on this
 * board (the module UART owns SD3), USB raw HID is one of the channels we
 * diagnose, and the radio push is gated on a live link. So when a fault lived in
 * the imperative shell (every defect this project has fixed: 7, 8, the C.6
 * rewrites, 9) the only instrument was a flash-and-watch cycle -- which is why
 * the USB self-detach could not be root-caused. This is the missing seam: a
 * small, tested, transport-independent view of the shell's state.
 *
 * The *encoding* is a pure rule and lives here; *gathering* the values (reading
 * the pins, the device index, the host) is the shell's job, and *sending* it is
 * whichever channel happens to be up (the 0xA4 raw-HID reply over USB, the radio
 * push buffer over the air). Keeping the codec pure means the wire format is
 * pinned by tests rather than by a host parser's assumptions. */

/* Everything the shell can report about how it currently sees the world. Kept
 * deliberately small and scalar so it maps to a fixed byte layout. */
typedef struct {
    uint8_t switch_pos; /* hsm_switch_pos_t: 0=UNKNOWN,1=USB,2=BT,3=2.4G */
    uint8_t devs;       /* wireless device index: 0=USB,1..5=BT1..5,6=2.4G */
    uint8_t host;       /* connection_host_t raw value (upstream's desired_host) */
    uint8_t boot_count; /* increments once per MCU boot (reset-loop detector) */
    uint8_t usb_line;   /* 1 if the USB data line is currently connected */
    uint8_t link_state; /* module MD_STATE_* value */
} wls_diag_t;

/* The fixed on-the-wire size of an encoded snapshot. */
#define WLS_DIAG_ENC_SIZE 6

/* Encode `d` into `out` (at least WLS_DIAG_ENC_SIZE bytes). Pure: same input,
 * same bytes. Returns the number of bytes written. */
uint8_t wls_diag_encode(const wls_diag_t *d, uint8_t *out);

/* Decode WLS_DIAG_ENC_SIZE bytes into `out`. Pure, and the exact inverse of
 * `wls_diag_encode` (a round-trip test proves the pair). Returns false if `in`
 * is NULL. */
bool wls_diag_decode(const uint8_t *in, wls_diag_t *out);
