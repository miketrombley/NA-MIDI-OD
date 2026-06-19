#ifndef CVOUT_H
#define CVOUT_H

/**
 * @file cvout.h
 * @brief PWM-as-DAC control-voltage output. A 0..1 control maps to a calibrated
 *        PWM duty range; an external RC low-pass turns it into a smooth CV.
 *
 * Unlike the LEDs this is NOT active-low: duty 0 => ~0 V, duty 1 => ~Vdd (3.3 V).
 *
 * First use: VC_HPF1 (PB3 / TIM2_CH2) -> 10k/220nF LPF -> SSI2160 VCA control,
 * sweeping a high-pass corner. The SSI2160 control law is exponential, so a
 * LINEAR duty ramp gives a musical (constant-octaves) frequency sweep.
 *
 *   *** CALIBRATION ***
 * duty_min / duty_max are the two ends of the sweep, found on the bench:
 *   - Set control = 0, trim duty_min until the corner sits at the low target.
 *   - Set control = 1, trim duty_max until the corner sits at the high target.
 * If the CV runs backwards (more volts = lower corner), just swap min/max
 * (duty_min > duty_max is allowed). Full range is duty_min=0, duty_max=1.
 */

#include "stm32f1xx_hal.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    TIM_HandleTypeDef* htim;
    uint32_t           channel;   /* TIM_CHANNEL_x */
    float              duty_min;   /* duty at control = 0.0 */
    float              duty_max;   /* duty at control = 1.0 */
} CvOut;

/** Bind to a PWM channel and set the calibrated endpoints. Starts at control 0.
 *  Start the channel separately with HAL_TIM_PWM_Start(). */
void cvout_init(CvOut* c, TIM_HandleTypeDef* htim, uint32_t channel,
                float duty_min, float duty_max);

/** Map control [0..1] -> duty [duty_min..duty_max] -> compare register. */
void cvout_set(CvOut* c, float control);

#ifdef __cplusplus
}
#endif

#endif /* CVOUT_H */
