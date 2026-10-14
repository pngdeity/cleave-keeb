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
    /* The RAW levels behind `switch_pos`, so the decode can be audited against
     * the physical part instead of trusted. `switch_pos` is `hsm_switch_pos_t`
     * derived from these two pins; the pins are measured to LIE -- a settled read
     * has returned 2.4 GHz with the switch on USB (the fault `TODO.md` calls the
     * pin lie, and the one `docs/HARDWARE.md` "the cabled-but-wireless fault"
     * records), and the lie has not even been stable across boots. Reporting the
     * levels is what turns "the pins lie" into a specific, repeatable
     * measurement, and it is the fact any transport-rule change must be based
     * on. `pin_b9` is the cable-detect read: a real host cable present should
     * read high, and whether it does decides whether `B9` can be used as a cable
     * hint at all. */
    uint8_t pin_c14; /* raw HS_BT_DEF_PIN level at gather time */
    uint8_t pin_c15; /* raw HS_2G4_DEF_PIN level at gather time */
    uint8_t pin_b9;  /* raw SPLIT_HAND_PIN (cable detect) level at gather time */
    /* The MCU's latched reset cause (`RCC->RSTSTAT`), consumed once at boot. The
     * chip names why it last reset and holds it; the LED speaks the same value in
     * its own stage, so this byte is the host's copy of one fact, not a second
     * reading. Zero means nothing was latched. Letting both readers carry the same
     * consumed value is the point -- re-reading the latch after a clear would
     * disagree with the LED. */
    uint8_t reset_cause;
} wls_diag_t;

/* The fixed on-the-wire size of an encoded snapshot. */
#define WLS_DIAG_ENC_SIZE 10

/* Encode `d` into `out` (at least WLS_DIAG_ENC_SIZE bytes). Pure: same input,
 * same bytes. Returns the number of bytes written. */
uint8_t wls_diag_encode(const wls_diag_t *d, uint8_t *out);

/* Decode WLS_DIAG_ENC_SIZE bytes into `out`. Pure, and the exact inverse of
 * `wls_diag_encode` (a round-trip test proves the pair). Returns false if `in`
 * is NULL. */
bool wls_diag_decode(const uint8_t *in, wls_diag_t *out);
