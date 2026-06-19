#ifndef MIDI_MAP_H
#define MIDI_MAP_H

/**
 * @file midi_map.h
 * @brief Board-wide MIDI CC assignments (ported from the "In The Water"
 *        platform's MidiMap.h so the two pedals stay MIDI-compatible).
 *
 * Decade-organized so each block has room to grow:
 *   20-29  : physical IO surface (the 6 pots, 2 footswitches, expression, on/off)
 *   110-119: global settings (channel select lives at the top)
 *   120-127: MIDI-spec reserved channel-mode messages (panic)
 *
 * On MIDI-OD the 6 pots drive the 5 SSI2160 VCAs + the bias DPOT, so CC 20-25
 * map straight onto POT1..POT6's targets (see main.c). Values 0-127 scale to
 * 0.0-1.0 and arbitrate against the physical knob (last-mover-wins hysteresis).
 */

#include <stdint.h>

/* -- Slot controls (20-29) : the physical IO surface -- */
#define MIDI_CC_POT1          20u   /* POT1 -> VC_HPF1    (low-cut)            */
#define MIDI_CC_POT2          21u   /* POT2 -> VCA_LPF1   (high-cut)           */
#define MIDI_CC_POT3          22u   /* POT3 -> VCA_LPF2   (high-cut)           */
#define MIDI_CC_POT4          23u   /* POT4 -> VCA_VOLUME (master out)         */
#define MIDI_CC_POT5          24u   /* POT5 -> VCA_GAIN   (drive level in)     */
#define MIDI_CC_POT6          25u   /* POT6 -> bias DPOT  (gating)             */
#define MIDI_CC_FS1           26u   /* SW_1 sim: value>0 = press (toggles bypass) */
#define MIDI_CC_FS2           27u   /* SW_2 sim: value>0 = press (toggles LED2)   */
#define MIDI_CC_EXPRESSION    28u   /* expression -> assigned target (future)  */
#define MIDI_CC_EFFECT_ONOFF  29u   /* direct bypass: 0 = bypassed, 127 = on   */

/* -- Global settings (110-119) -- */
#define MIDI_CC_CHANNEL      119u   /* set accept channel: 0 = omni, 1-16 = ch */

/* -- Channel-mode (120-127, MIDI-spec reserved) -- */
#define MIDI_CC_ALL_SOUND_OFF 120u  /* panic: force bypass                     */
#define MIDI_CC_ALL_NOTES_OFF 123u  /* panic: force bypass                     */

#endif /* MIDI_MAP_H */
