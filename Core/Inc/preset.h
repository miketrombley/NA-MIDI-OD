#ifndef PRESET_H
#define PRESET_H

/**
 * @file preset.h
 * @brief Effect-agnostic preset store + live-editing state machine (recall,
 *        save, real-time edits, knob-match cue).
 *
 * C port of the "Envelope Reverb" PresetManager (Daisy/STM32H750). It knows
 * NOTHING about VCAs, bias, or what any pot does — only "there are six pots,
 * each currently at a value 0..1, and a saved snapshot of those six values."
 * The host (main.c) owns the meaning of each pot, the FS2/LED2 wiring, and the
 * apply path (writing cv_target[]/bias_target); this module owns the *behavior*.
 *
 * Three modes:
 *   PRESET_LIVE       — pots pass straight through; LED2 off.
 *   PRESET_PRESET     — recalled the snapshot; knob moves still edit live (the
 *                       host holds the recalled value only until a knob moves).
 *                       A knob landing back on its saved value flashes white
 *                       ("matched the preset"). LED2 solid red.
 *   PRESET_SAVE_ARMED — a save is pending; LED2 breathes white. Controls stay
 *                       live, so you can dial in the sound before committing.
 *
 * FS2 gestures (host decides tap vs hold from its footswitch wrapper, then calls):
 *   - short tap  -> preset_recall_toggle():
 *       LIVE: recall (if a preset exists) -> PRESET.
 *       PRESET: -> LIVE (physical knobs take back over immediately).
 *       SAVE_ARMED: CANCEL -> restore the pre-arm mode.
 *   - ~1 s hold  -> preset_hold_fired():
 *       LIVE/PRESET: arm -> SAVE_ARMED (breathe).
 *       SAVE_ARMED: commit the snapshot -> PRESET (LED2 solid).
 *
 * One preset for now; the snapshot lives in RAM (lost on power-cycle — the F105
 * has no EEPROM, flash persistence is a later add). Growing to N slots is just
 * making the arrays 2-D + a slot index.
 *
 * No serialization, no HAL/DSP deps. All timing is driven by dt_ms from the
 * host's tick(). The host reads preset_value(i) for the held recall value and
 * gates its own apply path on the mode (see main.c).
 */

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PRESET_NUM_POTS 6

typedef enum { PRESET_LIVE, PRESET_PRESET, PRESET_SAVE_ARMED } PresetMode;

typedef struct { float r, g, b; } PresetColor;   /* LED2 output */

typedef struct {
    float live[PRESET_NUM_POTS];    /* latest resolved (knob-or-MIDI) value      */
    float preset[PRESET_NUM_POTS];  /* the saved snapshot                        */
    bool  aligned[PRESET_NUM_POTS]; /* is each knob IN its match band? (hysteretic) */

    PresetMode mode;
    PresetMode prev_mode;           /* mode before arming (restored on cancel)   */
    bool  has_preset;               /* has anything been saved yet?              */

    float catch_flash_ms;           /* white match-cue hold timer (>0 = lit)     */
    float breathe_ms;               /* free-running breathe phase (ms)           */
} Preset;

void preset_init(Preset* p);

/* A pot's resolved value (0..1) this poll. In PRESET mode, the edge where a knob
 * crosses onto its saved value arms the white match flash. Always non-blocking. */
void preset_on_pot_move(Preset* p, int idx, float value);

/* FS2 gesture hooks (host classifies tap vs hold). */
void preset_recall_toggle(Preset* p);   /* short tap                     */
void preset_hold_fired(Preset* p);      /* ~1 s hold crossed: arm/commit */

/* Commit a host-supplied snapshot and drop into PRESET mode. The host passes the
 * GATED values (saved value for a pot untouched since recall, live knob for a pot
 * that moved), so a save from a recalled preset keeps the un-tweaked params and
 * overwrites only what changed — copy-the-preset-then-edit. Preferred over the
 * SAVE_ARMED branch of preset_hold_fired, which snapshots the raw live panel. */
void preset_commit(Preset* p, const float values[PRESET_NUM_POTS]);

/* Control-rate housekeeping; dt_ms since the last call. */
void preset_tick(Preset* p, float dt_ms);

/* Inject a saved snapshot (e.g. loaded from external flash at boot): copies the
 * six values into the preset slot, marks them aligned, and sets has_preset so a
 * tap can recall it. Does NOT change mode (the host stays LIVE at boot). Pure —
 * no HAL/flash deps; the persistence layer (preset_store) feeds it. */
void preset_load_snapshot(Preset* p, const float values[PRESET_NUM_POTS]);

/* Held recall value the host applies for an untouched knob while in PRESET. */
float preset_value(const Preset* p, int idx);

/* LED2 output. */
bool        preset_led_on(const Preset* p);
PresetColor preset_led_color(const Preset* p);

/* Queries. */
static inline PresetMode preset_mode(const Preset* p)        { return p->mode; }
static inline bool preset_in_preset_mode(const Preset* p)    { return p->mode == PRESET_PRESET; }
static inline bool preset_save_armed(const Preset* p)        { return p->mode == PRESET_SAVE_ARMED; }
static inline PresetMode preset_armed_from(const Preset* p)  { return p->prev_mode; }   /* pre-arm mode */
static inline bool preset_has_preset(const Preset* p)        { return p->has_preset; }

#ifdef __cplusplus
}
#endif

#endif /* PRESET_H */
