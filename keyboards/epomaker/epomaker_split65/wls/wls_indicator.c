// Copyright 2026
// SPDX-License-Identifier: GPL-2.0-or-later

#include "wls_indicator.h"

#include "wls/wls.h"
#include "wls/wls_eeprom.h"
#include "wls/indicator_logic.h"
#include "wls/sleep_order_logic.h"
#include "wls/transport_logic.h"
#include "wls/boot_code_logic.h"
#include "quantum.h"

#ifdef WIRELESS_ENABLE
#    include "wireless.h"
#    include "lowpower.h"
#    include "module.h"
#endif

#ifdef RGB_MATRIX_ENABLE

typedef struct {
    bool        active;
    wls_blink_t blink; /* the shared blink timing (wls/indicator_logic) */
    uint8_t     index;
    RGB         rgb;
    void (*blink_cb)(uint8_t);
} wls_rgb_indicator_t;

static wls_rgb_indicator_t hs_rgb_indicators[HS_RGB_INDICATOR_COUNT];
static wls_rgb_indicator_t hs_rgb_bat[HS_RGB_BAT_COUNT];

/* The battery indicator's suppression deadline (0 = not suppressed). Set to
 * "now + WLS_BAT_SUPPRESS_MS" by an EEPROM reset; the indicator is hidden while
 * `timer_read32() < bat_suppress_until`. */
static uint32_t bat_suppress_until = 0;

/* The channel-indicator flash, as one piece of state. The blink timing is the
 * shared `wls_blink_t` (wls/indicator_logic); the index and colour are this
 * concern's own. Bundling them keeps the state's scope honest instead of five
 * loose file-scope globals that any function could reach (Power of Ten Rule 6,
 * wiring review Phase C.5). */
static struct {
    wls_blink_t blink; /* the shared blink timing; interval_ms 0 = idle */
    uint8_t     index; /* LED to flash */
    RGB         rgb;   /* colour to flash */
} wls_indicator;

static void wls_indicator_set(uint8_t index, RGB rgb, uint32_t interval, uint8_t times) {
    wls_blink_arm(&wls_indicator.blink, interval, times, timer_read32());
    wls_indicator.index = index;
    wls_indicator.rgb   = rgb;
}

void wls_indicator_wls_set(uint8_t index, RGB rgb, uint32_t interval, uint8_t times) {
    wls_indicator_set(index, rgb, interval, times);
}

/* Terminal predicate for the channel flash: it decides whether a completed
 * flash stays lit and NEVER re-arms. The flash is owed to a transition and is
 * armed exactly once, at the transition site (`wls_indicate_devs`); when it
 * completes it is done. Returning true here means "re-arm" — and the vendor
 * code returned true while the module was not connected, which re-armed the
 * flash forever: a half sitting on a non-connected channel blinked on a ~500 ms
 * cadence for as long as it stayed disconnected, on both halves (the indicator
 * is not master-gated). A time-driven repeat is not a channel change, so it is
 * not this function's to make; any "still trying to connect" motion must be an
 * explicit, bounded policy owned elsewhere, not an unbounded self-retrigger. */
static bool rgb_matrix_wls_indicator_cb(void) {
    if (*md_getp_state() == MD_STATE_CONNECTED) {
        led_wakeup();
    }

    return false;
}

void wls_indicator_channel_flash(void) {
    /* Idle: the flash owns nothing, so it paints nothing (the LED is left as
     * some other writer set it). */
    if (wls_indicator.blink.interval_ms == 0) {
        return;
    }

    wls_blink_out_t out = wls_blink_tick(&wls_indicator.blink, timer_read32());

    if (out.kind == WLS_BLINK_DONE) {
        /* The flash is owed to a transition and is armed exactly once, at the
         * transition site; on completion it is done (the terminal predicate
         * decides whether the LED stays lit). */
        if (rgb_matrix_wls_indicator_cb() != true) {
            return;
        }
    }

    if (out.on) {
        rgb_matrix_set_color(wls_indicator.index, wls_indicator.rgb.r, wls_indicator.rgb.g, wls_indicator.rgb.b);
    } else {
        rgb_matrix_set_color(wls_indicator.index, 0x00, 0x00, 0x00);
    }
}

static void rgb_matrix_hs_bat_set(uint8_t index, RGB rgb, uint32_t interval, uint8_t times) {
    for (int i = 0; i < HS_RGB_BAT_COUNT; i++) {
        if (!hs_rgb_bat[i].active) {
            hs_rgb_bat[i].active = true;
            wls_blink_arm(&hs_rgb_bat[i].blink, interval, times, timer_read32());
            hs_rgb_bat[i].index = index;
            hs_rgb_bat[i].rgb   = rgb;
            break;
        }
    }
}

void wls_indicator_battery_blink(void) {
    for (int i = 0; i < HS_RGB_BAT_COUNT; i++) {
        if (!hs_rgb_bat[i].active) {
            continue;
        }

        wls_blink_out_t out = wls_blink_tick(&hs_rgb_bat[i].blink, timer_read32());

        if (out.kind == WLS_BLINK_DONE) {
            hs_rgb_bat[i].active = false;
        }

        if (out.on) {
            rgb_matrix_set_color(hs_rgb_bat[i].index, hs_rgb_bat[i].rgb.r, hs_rgb_bat[i].rgb.g, hs_rgb_bat[i].rgb.b);
        } else {
            rgb_matrix_set_color(hs_rgb_bat[i].index, 0x00, 0x00, 0x00);
        }
    }
}

void wls_indicator_battery(void) {
    static wls_sleep_order_t sleep_order = WLS_SLEEP_ORDER_IDLE;
    uint8_t                  bat_level   = *md_getp_bat();

    if (!is_keyboard_master()) {
        return;
    }

    /* Always-on soft battery indicator across HS_MATRIX_BAT_SOFT_INDEX and
     * HS_MATRIX_BAT_SOFT_INDEX2 (two adjacent right-half bottom-row LEDs), at
     * full channel intensity so the charge state is legible at a glance rather
     * than a single dim LED. */
    if (rgb_matrix_get_val() != 0) {
        wls_bat_rgb_t rgb = wls_bat_rgb_of(wls_bat_colour_of(charging_state, bat_full_flag, bat_level));
        rgb_matrix_set_color(HS_MATRIX_BAT_SOFT_INDEX, rgb.r, rgb.g, rgb.b);
        rgb_matrix_set_color(HS_MATRIX_BAT_SOFT_INDEX2, rgb.r, rgb.g, rgb.b);
    }

    /* The *what to do* is the pure `wls_bat_action_of()`; this is only the
     * effect. The sleep order below is the one thing that is not the action's
     * concern: the low-level warning rides on the same sight as the sleep
     * arming, so the tick runs whatever the action says. */
    bool             critical = bat_level <= BATTERY_CAPACITY_STOP;
    wls_bat_action_t action   = wls_bat_action_of(charging_state, bat_full_flag, bat_level, im_bat_req_charging_flag, BATTERY_CAPACITY_LOW);

    switch (action) {
        case WLS_BAT_ACT_PAINT_RED:
            rgb_matrix_set_color(HS_MATRIX_BLINK_INDEX_BAT, 0xFF, 0x00, 0x00);
            break;
        case WLS_BAT_ACT_PAINT_GREEN:
            rgb_matrix_set_color(HS_MATRIX_BLINK_INDEX_BAT, 0x00, 0xFF, 0x00);
            break;
        case WLS_BAT_ACT_BLINK_RED:
            rgb_matrix_hs_bat_set(HS_MATRIX_BLINK_INDEX_BAT, (RGB){0xFF, 0x00, 0x00}, 250, 1);
            break;
        case WLS_BAT_ACT_NONE:
        default:
            break;
    }

    /* The timing (arm on first critical sight, fire once the window elapses) is
     * the pure `wls_sleep_order_tick()`; the *whether* is the pure
     * `hsm_should_order_sleep()`. */
    if (critical) {
        if (wls_sleep_order_tick(&sleep_order, true, timer_read32()) == WLS_SLEEP_ORDER_NOW) {
            if (hsm_should_order_sleep(bat_level, charging_state, BATTERY_CAPACITY_STOP)) {
                wls_order_sleep(true);
                lpwr_set_timeout_manual(true);
            }
        }
    } else {
        (void)wls_sleep_order_tick(&sleep_order, false, timer_read32());
    }
}

void rgb_blink_dir(void) {
    rgb_matrix_hs_indicator_set(54, (RGB){0, 0, 0}, 250, 1);
    rgb_matrix_hs_indicator_set(66, (RGB){0, 0, 0}, 250, 1);
    rgb_matrix_hs_indicator_set(67, (RGB){0, 0, 0}, 250, 1);
    rgb_matrix_hs_indicator_set(65, (RGB){0, 0, 0}, 250, 1);
}

bool hs_reset_settings_user(void) {
    rgb_matrix_hs_indicator_set(0xFF, (RGB){0x10, 0x10, 0x10}, 250, 3);

    return true;
}

static void nkr_indicators_hook(uint8_t index) {
    if ((hs_rgb_indicators[index].rgb.r == 0x6E) && (hs_rgb_indicators[index].rgb.g == 0x00) && (hs_rgb_indicators[index].rgb.b == 0x00)) {
        rgb_matrix_hs_indicator_set(0xFF, (RGB){0x6E, 0x00, 0x00}, 250, 1);

    } else if ((hs_rgb_indicators[index].rgb.r == 0x00) && (hs_rgb_indicators[index].rgb.g == 0x6E) && (hs_rgb_indicators[index].rgb.b == 0x00)) {
        rgb_matrix_hs_indicator_set(0xFF, (RGB){0x00, 0x00, 0x6F}, 250, 1);
    }
}

void rgb_matrix_hs_indicator_set(uint8_t index, RGB rgb, uint32_t interval, uint8_t times) {
    for (int i = 0; i < HS_RGB_INDICATOR_COUNT; i++) {
        if (!hs_rgb_indicators[i].active) {
            hs_rgb_indicators[i].active = true;
            wls_blink_arm(&hs_rgb_indicators[i].blink, interval, times, timer_read32());
            hs_rgb_indicators[i].index    = index;
            hs_rgb_indicators[i].rgb      = rgb;
            hs_rgb_indicators[i].blink_cb = (index != 0xFF) ? NULL : nkr_indicators_hook;
            break;
        }
    }
}

/* Cancel the running set-flash on `index`, if any. (The vendor API this
 * replaces took a `remain_time` it never read and only ever cleared the entry;
 * every caller passed 0. Stating the real effect -- cancel-by-index -- removes
 * the lie, and the dead parameter with it.) */
void rgb_matrix_hs_cancel_index(uint8_t index) {
    for (int i = 0; i < HS_RGB_INDICATOR_COUNT; i++) {
        if (hs_rgb_indicators[i].index == index) {
            hs_rgb_indicators[i].blink.remaining   = 0;
            hs_rgb_indicators[i].blink.interval_ms = 0;
            hs_rgb_indicators[i].active            = false;
            break;
        }
    }
}

void wls_indicator_set_flash(void) {
    for (int i = 0; i < HS_RGB_INDICATOR_COUNT; i++) {
        if (!hs_rgb_indicators[i].active) {
            continue;
        }

        wls_blink_out_t out = wls_blink_tick(&hs_rgb_indicators[i].blink, timer_read32());

        if (out.kind == WLS_BLINK_DONE) {
            hs_rgb_indicators[i].active = false;
            if (hs_rgb_indicators[i].blink_cb != NULL) {
                hs_rgb_indicators[i].blink_cb(i);
            }
            continue;
        }

        uint8_t r = out.on ? hs_rgb_indicators[i].rgb.r : 0x00;
        uint8_t g = out.on ? hs_rgb_indicators[i].rgb.g : 0x00;
        uint8_t b = out.on ? hs_rgb_indicators[i].rgb.b : 0x00;
        if (hs_rgb_indicators[i].index == 0xFF) {
            rgb_matrix_set_color_all(r, g, b);
        } else {
            rgb_matrix_set_color(hs_rgb_indicators[i].index, r, g, b);
        }
    }
}

/* The user-visible transport confirmation: flash the selected channel's LED
 * once. Called ONLY from genuine transition sites (the mode-switch edge, the
 * Fn+Q/W/E keycode path, a re-pair long-press) — never from a re-assert, so the
 * indicator can only mean "the channel just changed". `reset` selects the
 * re-pair (fast) vs select (slow) cadence. This is the same separation applied
 * to the LED rail: the effect has one owner and is impossible to trigger by a
 * no-op. */
void wls_indicate_devs(uint8_t new_devs, bool reset) {
    switch (new_devs) {
        case DEVS_BT1: {
            if (reset) {
                wls_indicator_set(HS_RGB_BLINK_INDEX_BT1, (RGB){HS_LBACK_COLOR_BT1}, 200, 1);
            } else {
                wls_indicator_set(HS_RGB_BLINK_INDEX_BT1, (RGB){HS_PAIR_COLOR_BT1}, 500, 1);
            }
        } break;
        case DEVS_BT2: {
            if (reset) {
                wls_indicator_set(HS_RGB_BLINK_INDEX_BT2, (RGB){HS_LBACK_COLOR_BT2}, 200, 1);
            } else {
                wls_indicator_set(HS_RGB_BLINK_INDEX_BT2, (RGB){HS_PAIR_COLOR_BT2}, 500, 1);
            }
        } break;
        case DEVS_BT3: {
            if (reset) {
                wls_indicator_set(HS_RGB_BLINK_INDEX_BT3, (RGB){HS_LBACK_COLOR_BT3}, 200, 1);
            } else {
                wls_indicator_set(HS_RGB_BLINK_INDEX_BT3, (RGB){HS_PAIR_COLOR_BT3}, 500, 1);
            }
        } break;
        case DEVS_BT4: {
            if (reset) {
                wls_indicator_set(41, (RGB){RGB_BLUE}, 200, 1);
            } else {
                wls_indicator_set(41, (RGB){RGB_BLUE}, 500, 1);
            }
        } break;
        case DEVS_BT5: {
            if (reset) {
                wls_indicator_set(42, (RGB){RGB_BLUE}, 200, 1);
            } else {
                wls_indicator_set(42, (RGB){RGB_BLUE}, 500, 1);
            }
        } break;
        case DEVS_2G4: {
            if (reset) {
                wls_indicator_set(HS_RGB_BLINK_INDEX_2G4, (RGB){HS_LBACK_COLOR_2G4}, 200, 1);
            } else {
                wls_indicator_set(HS_RGB_BLINK_INDEX_2G4, (RGB){HS_LBACK_COLOR_2G4}, 500, 1);
            }
        } break;
        default:
            break;
    }
}

uint32_t wls_bat_suppress_deadline(void) {
    return bat_suppress_until;
}

void wls_bat_suppress_arm(void) {
    bat_suppress_until = timer_read32() + WLS_BAT_SUPPRESS_MS;
}

/* The boot diagnostic code: the two-group `(amber, white)` count, played over
 * the whole matrix. This is the exporter that reads out when the USB line is
 * down -- exactly when the `0xA4` reply that carries the same record cannot be
 * read. The codec and the sequencer are the pure `wls/boot_code_logic`; this
 * pass only paints, and it *owns* the matrix while it plays.
 *
 * Ownership is the fix for the previous instrument's fatal defect: it painted
 * additively and `wls_indicator_set_flash()` (below) repainted the whole matrix
 * on any index-0xFF flash, so the frames the eye saw depended on unrelated
 * state. Here the code is the highest-priority owner -- while it plays, nothing
 * else paints. */
static bool                   boot_code_active  = false;
static bool                   boot_code_armed   = false;
static uint32_t               boot_code_started = 0;
static bool                   boot_code_done    = false;
static wls_boot_code_counts_t boot_code_counts;

bool wls_indicator_boot_code_owns_matrix(void) {
    return boot_code_active;
}

void wls_indicator_boot_code_dismiss(void) {
    boot_code_active = false;
    boot_code_done   = true;
}

/* Build the code from the boot facts and start playing. `stage`/`code` come from
 * the shell, which knows the boot outcome; the mapping to counts and every bit
 * of the timing is the pure core's.
 *
 * The shell holds NO policy here: the show duration is the core's
 * `wls_boot_code_show_ms()` (derived from the frame, so it can never be shorter
 * than one repetition) and the origin is the first paint, passed to the core as
 * a value. The only thing the shell remembers is "has this armed", which is a
 * latch, not a decision.
 *
 * ARMING IS DEFERRED BY ONE PASS, deliberately. The first call happens before
 * this frame's buffer is flushed to the panel, so an origin taken here would be
 * up to a frame (plus the RGB pipeline's lead-in) earlier than anything the eye
 * can see -- and the leading amber pulses would be counted short. Taking the
 * origin on the second pass makes it a real frame boundary. The core's contract
 * no longer depends on this (the show window guarantees a complete second
 * frame), but starting on a boundary keeps the first frame honest too. */
void wls_indicator_boot_code_start(void) {
    /* Idempotent: the paint pass calls this every frame, but the code must arm
     * exactly once. Re-arming would restart the window each frame and the code
     * would never end. */
    if (boot_code_active || boot_code_done) {
        return;
    }
    wls_boot_code_t c = wls_boot_code_now();
    boot_code_counts  = wls_boot_code_counts(c);
    /* Armed but not yet started: the paint is held black until an origin is
     * stamped on the next pass (a real frame boundary), so no pulse is ever
     * half-shown. "Not yet started" is its own latch (`boot_code_armed`)
     * rather than a sentinel in the origin -- `timer_read32()` legitimately
     * returns 0 at MCU start, so a 0 origin is a real time, not "unset", and
     * conflating the two would leave the matrix dark for the whole window. */
    boot_code_armed   = true;
    boot_code_active  = true;
    boot_code_done    = false;
}

void wls_indicator_boot_code(void) {
    if (!boot_code_active) {
        return;
    }
    if (boot_code_armed) {
        /* Origin not yet stamped: hold black and stamp it now. */
        boot_code_started = timer_read32();
        boot_code_armed   = false;
        rgb_matrix_set_color_all(0, 0, 0);
        return;
    }
    if (timer_elapsed32(boot_code_started) >= wls_boot_code_show_ms(boot_code_counts)) {
        boot_code_active = false;
        boot_code_done   = true;
        return;
    }

    wls_boot_code_out_t out = wls_boot_code_at(boot_code_counts, boot_code_started, timer_read32());
    if (out.phase == WLS_BOOT_CODE_PHASE_LIT) {
        rgb_matrix_set_color_all(out.r, out.g, out.b);
    } else {
        rgb_matrix_set_color_all(0, 0, 0);
    }
}

#endif /* RGB_MATRIX_ENABLE */
