// Copyright 2026
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Pure codec and blink schedule for the *boot-decision* post-mortem.
 *
 * The main diagnostic seam (`wls/diag_logic`) rides the 0xA4 raw-HID reply, so it
 * needs the USB data line to be up. The worst fault this board has had — the boot
 * self-detach — is exactly the case where the line is *not* up: the shell decides
 * the transport, drives `wls_usb_connect(false)`, and the line dies before a host
 * can ask for anything. The instrument was unreachable precisely when it was
 * needed (see docs/FINDINGS.md, "the boot switch read is un-settled").
 *
 * This is the second exporter: a small record of the boot transport decision,
 * published on a channel that does not depend on the USB line —
 *   1. a blink schedule over the LED rail (the rail is powered before the switch
 *      read, so this works even when the line is down), and
 *   2. a retained copy the next successful 0xA4 reply can be compared against.
 *
 * The *rule* (what the record contains, how it encodes, how it blinks) is pure and
 * lives here; the shell gathers the values and drives the LEDs. */

/* The boot transport decision, as the shell made it. `switch_pos` and `host` are
 * the raw vocabularies used everywhere else on this board; `settled` says whether
 * the switch read was readable at all. */
typedef struct {
    uint8_t switch_pos; /* hsm_switch_pos_t: 0=UNKNOWN,1=USB,2=BT,3=2.4G */
    uint8_t host;       /* hsm_host_of_devs()/connection host: 3=USB,4=BT,5=2.4G */
    bool    settled;    /* false = the position was unreadable (nothing applied) */
} wls_boot_report_t;

/* The fixed encoded size of a boot report. */
#define WLS_BOOT_REPORT_ENC_SIZE 3

/* Encode `r` into `out` (at least WLS_BOOT_REPORT_ENC_SIZE bytes). Pure. */
uint8_t wls_boot_report_encode(const wls_boot_report_t *r, uint8_t *out);

/* Decode into `out`. Pure; the exact inverse of the encoder. False on NULL. */
bool wls_boot_report_decode(const uint8_t *in, wls_boot_report_t *out);

/* One step of the LED blink schedule that publishes the report with no host.
 *
 * The report is a byte sequence; the schedule shows it as a series of pulses,
 * one pulse per *field*, so a human can read it off the LEDs: each step returns
 * how many blinks to show (`count`) and what colour, for the field at `index`.
 * A pure function of (`index`) — the shell just walks `index` 0..`steps-1` on a
 * timer. Keeping it pure means the schedule (the mapping from a decision to a
 * visible signal) is pinned by a test, not by counting flashes by eye. */
typedef struct {
    uint8_t count; /* number of blinks to show for this step (0 = skip) */
    uint8_t r, g, b;
} wls_boot_blink_step_t;

/* How many steps a full report takes. */
#define WLS_BOOT_REPORT_BLINK_STEPS 3

/* The blink schedule for `r` at step `index` (0-based, < steps). Pure: the
 * colour encodes the *kind* of field and the count encodes its *value* (plus one,
 * so a zero value is still a visible pulse). Callers must pass `index` within
 * range; an out-of-range index returns a zero (skip) step. */
wls_boot_blink_step_t wls_boot_report_blink_step(const wls_boot_report_t *r, uint8_t index);
