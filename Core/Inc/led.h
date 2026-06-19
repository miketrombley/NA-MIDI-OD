#ifndef LED_H
#define LED_H

/**
 * @file led.h
 * @brief Generic single-channel LED PWM driver (STM32 HAL backend).
 *
 * Handles polarity (active-high / active-low common-anode), a brightness curve
 * (gamma 2.2 for perceptual linearity), and a max-brightness cap. One instance
 * per PWM channel; led_rgb.c ties three of these into an RGB LED.
 *
 * Ported from "In The Water": the platform write that was Daisy's
 * PWMHandle::Channel::Set() is now __HAL_TIM_SET_COMPARE().
 */

#include "stm32f1xx_hal.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LED_POLARITY_ACTIVE_HIGH = 0,  /* duty 1.0 = full bright (common cathode)  */
    LED_POLARITY_ACTIVE_LOW  = 1   /* duty 0.0 = full bright (common anode)    */
} LEDPolarity;

typedef enum {
    LED_CURVE_LINEAR    = 0,
    LED_CURVE_GAMMA_2_2 = 1,       /* perceptually natural fade (recommended)  */
    LED_CURVE_EXPONENTIAL = 2
} LEDBrightnessCurve;

typedef struct {
    /* config */
    TIM_HandleTypeDef* htim;       /* timer handle (e.g. &htim3)               */
    uint32_t           channel;    /* TIM_CHANNEL_1 / _2 / _3 ...              */
    LEDPolarity        polarity;
    LEDBrightnessCurve curve;
    float              max_brightness; /* 0..1 dimming cap                     */
    /* state */
    float              current_brightness; /* last linear value set            */
    bool               enabled;
} LEDDriver;

/** Initialize a driver and push it to off. Does NOT start the timer — call
 *  HAL_TIM_PWM_Start() for the channel once after init. */
void led_init(LEDDriver* led, TIM_HandleTypeDef* htim, uint32_t channel,
              LEDPolarity polarity, LEDBrightnessCurve curve, float max_brightness);

/** Set linear brightness 0..1. Curve + cap + polarity applied automatically. */
void led_setBrightness(LEDDriver* led, float brightness);

/** Enable/disable (false forces the LED off, remembering the brightness). */
void led_setEnabled(LEDDriver* led, bool enable);

/** Last linear brightness that was set. */
float led_getBrightness(const LEDDriver* led);

#ifdef __cplusplus
}
#endif

#endif /* LED_H */
