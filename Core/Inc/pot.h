#ifndef POT_H
#define POT_H

/**
 * @file pot.h
 * @brief Potentiometer wrapper — one ADC channel + end-of-travel correction +
 *        hysteresis. Ported from the "In The Water" Pot/readKnob design.
 *
 * Reads are blocking single conversions on a shared ADC. With 6 knobs at a
 * 100 Hz poll that's ~0.1% CPU, so no DMA/scan setup is needed — the driver
 * just points ADC1 at the channel it wants for each read.
 *
 * Pipeline: raw 12-bit ADC -> 0..1 -> rail rescale (POT_RAIL_LO..HI -> 0..1,
 * clamped) so the knob saturates cleanly at the mechanical stops.
 */

#include "stm32f1xx_hal.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* End-of-travel correction. Knobs rarely reach the rails, so rescale the live
 * span to 0..1 and clamp. Defaults are conservative (small dead zone); watch
 * Pot.raw at the mechanical stops on THIS board and tighten these. */
#define POT_RAIL_LO     0.01f   /* fraction at/below which -> 0.0 */
#define POT_RAIL_HI     0.99f   /* fraction at/above which -> 1.0 */

/* Change threshold (fraction of full scale) below which poll() reports "no
 * change" — kills ADC dither so a still knob doesn't spam parameter updates. */
#define POT_HYSTERESIS  0.003f

typedef struct {
    ADC_HandleTypeDef* hadc;       /* shared ADC (e.g. &hadc1)               */
    uint32_t           channel;    /* ADC_CHANNEL_0 / _10 / ...              */
    uint32_t           sampletime; /* ADC_SAMPLETIME_* (long for pots)       */
    uint16_t           raw;        /* last raw conversion 0..4095 (for tuning)*/
    float              value;      /* last corrected value 0..1; -1 = unread  */
} Pot;

/** Bind a pot to an ADC channel. Does not read yet. Call HAL_ADCEx_Calibration_
 *  Start(hadc) ONCE before the first read (the ADC, not each pot). */
void pot_init(Pot* p, ADC_HandleTypeDef* hadc, uint32_t channel);

/** Blocking read: select channel, convert, apply rail correction. Updates
 *  ->raw and ->value and returns the corrected value 0..1. */
float pot_read(Pot* p);

/** Read and hysteresis-check. @return true if the value moved more than
 *  POT_HYSTERESIS since the last poll (always true on the first poll). */
bool pot_poll(Pot* p);

/** Most-recent corrected value 0..1 (-1.0 before the first read). */
static inline float pot_value(const Pot* p) { return p->value; }

#ifdef __cplusplus
}
#endif

#endif /* POT_H */
