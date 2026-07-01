/**
 * @file preset.c
 * @brief Preset store + live-editing state machine. See preset.h for the model.
 *
 * C port of "Envelope Reverb" PresetManager.h. The host owns the apply path; here
 * we only track state, the knob-match cue, and the LED2 color. The recall value
 * is held by the host's per-pot movement gate (it reads preset_value()), so this
 * module has no apply buffer — entering/leaving a mode is enough.
 */

#include "preset.h"
#include <math.h>   /* fabsf, fmodf */

/* "On the spot" tolerance — ~3% of travel, a hair above the ADC's ~0.003
 * post-hysteresis step so the catch is easy to land by hand. */
#define CATCH_TOL        0.03f
#define CATCH_FLASH_MS   750.0f    /* white re-align flash; cancels early if you leave the band */

/* InTheWater house palette + breathe. Both boards share the SAME sp1513 die
 * balance (kPreGamma R0.80/G0.60/B0.79) + gamma 2.2, so these perceptual values
 * render identically here. LED2's max_brightness is 1.0 (set in main.c), like ITW. */
#define WHITE_R           0.70f
#define WHITE_G           0.90f
#define WHITE_B           0.90f     /* ITW kWhite — R-trimmed neutral white       */
#define RED_LEVEL         1.00f     /* solid recall red; run bright like ITW      */
#define BREATHE_FLOOR     0.40f     /* breathe TROUGH = 40% of kWhite (ITW)       */

#define BREATHE_PERIOD_MS 1500.0f   /* 1.5 s triangle, matches ITW's "waiting" cue */

/* Re-seed knob alignment from where the knobs physically sit right now, so a
 * knob already on its spot doesn't spuriously flash on its first move. */
static void seed_alignment(Preset* p)
{
    for (int i = 0; i < PRESET_NUM_POTS; ++i)
        p->aligned[i] = fabsf(p->live[i] - p->preset[i]) <= CATCH_TOL;
}

/* Triangle phase for the breathe: 0 at the dark trough, 1 at the bright peak. */
static float breathe_phase(const Preset* p)
{
    const float t = fmodf(p->breathe_ms, BREATHE_PERIOD_MS);
    const float half = BREATHE_PERIOD_MS * 0.5f;
    return (t < half) ? (t / half) : (2.0f - t / half);
}

void preset_init(Preset* p)
{
    for (int i = 0; i < PRESET_NUM_POTS; ++i) {
        p->live[i]    = 0.0f;
        p->preset[i]  = 0.0f;
        p->aligned[i] = true;
    }
    p->mode           = PRESET_LIVE;
    p->prev_mode      = PRESET_LIVE;
    p->has_preset     = false;
    p->catch_flash_ms = 0.0f;
    p->breathe_ms     = 0.0f;
}

void preset_on_pot_move(Preset* p, int idx, float value)
{
    if (idx < 0 || idx >= PRESET_NUM_POTS) return;
    p->live[idx] = value;

    if (p->mode == PRESET_PRESET) {
        /* White cue, latched-with-cancel: start the flash on the EDGE where the
         * knob crosses ONTO its saved value, let it run up to CATCH_FLASH_MS, but
         * KILL it the instant the knob leaves the band. Informational only — the
         * host applies the move. */
        const bool on_spot = fabsf(value - p->preset[idx]) <= CATCH_TOL;
        if (on_spot && !p->aligned[idx])      p->catch_flash_ms = CATCH_FLASH_MS;
        else if (!on_spot && p->aligned[idx]) p->catch_flash_ms = 0.0f;
        p->aligned[idx] = on_spot;
    }
}

void preset_recall_toggle(Preset* p)
{
    switch (p->mode) {
        case PRESET_PRESET:                 /* un-recall -> live: knobs take over */
            p->mode = PRESET_LIVE;
            break;

        case PRESET_SAVE_ARMED:             /* CANCEL -> restore the pre-arm mode */
            p->mode = p->prev_mode;
            if (p->prev_mode == PRESET_PRESET) seed_alignment(p);
            break;

        case PRESET_LIVE:                   /* recall (if anything is saved)      */
            if (p->has_preset) {
                p->mode = PRESET_PRESET;
                seed_alignment(p);
            }
            break;
    }
}

void preset_hold_fired(Preset* p)
{
    if (p->mode == PRESET_SAVE_ARMED) {
        /* Second hold -> COMMIT: snapshot the live knobs and drop into the preset
         * (LED2 settles solid). The host already holds these exact values. */
        for (int i = 0; i < PRESET_NUM_POTS; ++i) {
            p->preset[i]  = p->live[i];
            p->aligned[i] = true;
        }
        p->has_preset = true;
        p->mode       = PRESET_PRESET;
    } else {
        /* First hold (from LIVE or PRESET) -> ARM. Remember where we came from so
         * a cancel can return there. Controls stay live while armed. */
        p->prev_mode  = p->mode;
        p->mode       = PRESET_SAVE_ARMED;
        p->breathe_ms = 0.0f;               /* start the breath at the dark trough */
    }
}

void preset_commit(Preset* p, const float values[PRESET_NUM_POTS])
{
    /* Store the host's gated snapshot (preset value for untouched pots, live knob
     * for moved ones) and settle into PRESET. Unlike preset_hold_fired's legacy
     * SAVE_ARMED branch, this does NOT read p->live[] — the host decided which
     * values to keep, so a save from a recalled preset preserves un-tweaked params. */
    for (int i = 0; i < PRESET_NUM_POTS; ++i) {
        p->preset[i]  = values[i];
        p->aligned[i] = true;
    }
    p->has_preset = true;
    p->mode       = PRESET_PRESET;
}

void preset_tick(Preset* p, float dt_ms)
{
    p->breathe_ms += dt_ms;                 /* free-running; read only while armed */
    if (p->catch_flash_ms > 0.0f) p->catch_flash_ms -= dt_ms;   /* purple flash decay */
}

void preset_load_snapshot(Preset* p, const float values[PRESET_NUM_POTS])
{
    for (int i = 0; i < PRESET_NUM_POTS; ++i) {
        p->preset[i]  = values[i];
        p->aligned[i] = true;
    }
    p->has_preset = true;
    /* mode stays as-is (host boots LIVE); first SW_2 tap recalls this snapshot. */
}

float preset_value(const Preset* p, int idx)
{
    return (idx >= 0 && idx < PRESET_NUM_POTS) ? p->preset[idx] : 0.0f;
}

bool preset_led_on(const Preset* p)
{
    return p->mode != PRESET_LIVE;
}

PresetColor preset_led_color(const Preset* p)
{
    if (p->mode == PRESET_SAVE_ARMED) {
        /* White breathe, exactly like ITW's scale(kWhite, breathe(ph)): the white
         * is scaled from BREATHE_FLOOR up to full and back (triangle). breathe_phase
         * is 0 at the trough, 1 at the peak. */
        const float k = BREATHE_FLOOR + (1.0f - BREATHE_FLOOR) * breathe_phase(p);
        PresetColor c = { WHITE_R * k, WHITE_G * k, WHITE_B * k };
        return c;
    }
    if (p->catch_flash_ms > 0.0f) {
        PresetColor c = { WHITE_R, WHITE_G, WHITE_B };    /* re-aligned = full white */
        return c;
    }
    PresetColor c = { RED_LEVEL, 0.0f, 0.0f };            /* recall = red */
    return c;
}
