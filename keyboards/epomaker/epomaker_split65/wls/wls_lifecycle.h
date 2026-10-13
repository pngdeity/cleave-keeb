// Copyright 2026
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

/* The board's lifecycle hooks, as delegates.
 *
 * QMK calls the board at fixed points: boot, suspend/resume, the low-power
 * machine's decision points, and the per-loop housekeeping pass. Each of those
 * was a body of shell code living in the board glue; they are gathered here so
 * the board file keeps only thin QMK-mandated wrappers and each piece of the
 * lifecycle has one home.
 *
 * The *policy* in each is a pure decision (see the pure cores); these functions
 * only assemble the live inputs and perform the effects. */

/* Boot: initialize every owned piece of the board in one ordered pass. Idempotent
 * apart from the diagnostic boot counter, so the settings-reset path may re-run
 * it. */
void wls_lifecycle_boot(void);

/* Suspend/resume halves. The *_kb halves own the board's own pins and rail; the
 * *_user halves relay the cross-half rail power over the split link. */
void wls_lifecycle_suspend_power_down_kb(void);
void wls_lifecycle_suspend_wakeup_init_kb(void);
void wls_lifecycle_suspend_wakeup_init_user(void);
void wls_lifecycle_suspend_power_down_user(void);

/* Low-power machine decision points. */
bool wls_lifecycle_lpwr_allow_timeout(void);
bool wls_lifecycle_lpwr_allow_presleep(void);
void wls_lifecycle_lpwr_wakeup(void);

/* The per-loop passes. */
void wls_lifecycle_wireless_post_task(void);
void wls_lifecycle_housekeeping(void);
