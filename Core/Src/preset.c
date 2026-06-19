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
#define CATCH_FLASH_MS   200.0f    /* white re-align flash on the EDGE onto a value */

/* Cyan/white brightness shared with LED1 (the effect-on / gain LED). */
#define PRESET_LEVEL     0.60f     /* solid recall + breathe PEAK  */
#define BREATHE_MIN      0.10f     /* breathe TROUGH (darker low)  */

#define BREATHE_PERIOD_MS 1500.0f  /* 1.5 s triangle, matches the "waiting" cue */

/* Re-seed knob alignment from where the knobs physically sit right now, so a
 * knob already on its spot doesn't spuriously flash on its first move. */
static void seed_alignment(Preset* p)
{
    for (int i = 0; i < PRESET_NUM_POTS; ++i)
        p->aligned[i] = fabsf(p->live[i] - p->preset[i]) <= CATCH_TOL;
}

/* Triangle phase for the breathe: 0 at the bright peak, 1 at the dark trough. */
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
        /* Flash white on the EDGE where the knob crosses onto its saved value
         * (not continuously). Informational only — the host applies the move. */
        const bool on_spot = fabsf(value - p->preset[idx]) <= CATCH_TOL;
        if (on_spot && !p->aligned[idx]) p->catch_flash_ms = CATCH_FLASH_MS;
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
        p->breathe_ms = 0.0f;               /* start the breath at the bright peak */
    }
}

void preset_tick(Preset* p, float dt_ms)
{
    p->breathe_ms += dt_ms;                 /* free-running; read only while armed */
    if (p->catch_flash_ms > 0.0f) p->catch_flash_ms -= dt_ms;   /* purple flash decay */
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
        /* White breathe (peak == PRESET_LEVEL, trough == BREATHE_MIN) so "save
         * mode" is unmistakable and clearly a mode, not a recalled preset. */
        const float v = PRESET_LEVEL - breathe_phase(p) * (PRESET_LEVEL - BREATHE_MIN);
        PresetColor c = { v, v, v };
        return c;
    }
    if (p->catch_flash_ms > 0.0f) {
        PresetColor c = { PRESET_LEVEL, PRESET_LEVEL, PRESET_LEVEL };  /* re-aligned = white */
        return c;
    }
    PresetColor c = { PRESET_LEVEL, 0.0f, 0.0f };            /* recall = red */
    return c;
}
