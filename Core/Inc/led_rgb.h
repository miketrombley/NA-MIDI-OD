#ifndef LED_RGB_H
#define LED_RGB_H

/**
 * @file led_rgb.h
 * @brief RGB LED = three led.c channels + SP1513 white balance.
 *
 * Usage:
 *   LedRgb rgb;
 *   ledrgb_init(&rgb, &htim3,
 *               TIM_CHANNEL_1, TIM_CHANNEL_2, TIM_CHANNEL_3,
 *               LED_POLARITY_ACTIVE_LOW, 1.0f);
 *   HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1);   // x3, once
 *   ...
 *   ledrgb_set(&rgb, 1.0f, 0.0f, 0.0f);         // red
 *
 * set() takes perceptual 0..1 per channel (think sRGB). White balance + gamma
 * are applied inside, so equal R=G=B reads neutral and fades look smooth.
 */

#include "led.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    LEDDriver r;
    LEDDriver g;
    LEDDriver b;
} LedRgb;

/** Wire three channels of one timer to an RGB LED. All channels share polarity,
 *  use the gamma 2.2 curve, and start with the given max-brightness cap. */
void ledrgb_init(LedRgb* rgb, TIM_HandleTypeDef* htim,
                 uint32_t ch_r, uint32_t ch_g, uint32_t ch_b,
                 LEDPolarity polarity, float max_brightness);

/** Same, but each color can live on a different timer/channel — needed when an
 *  LED's three channels aren't all on one timer (e.g. LED1: R on TIM3, G/B on
 *  TIM2). Pass the timer handle + channel per color. */
void ledrgb_init_split(LedRgb* rgb,
                       TIM_HandleTypeDef* htim_r, uint32_t ch_r,
                       TIM_HandleTypeDef* htim_g, uint32_t ch_g,
                       TIM_HandleTypeDef* htim_b, uint32_t ch_b,
                       LEDPolarity polarity, float max_brightness);

/** Set perceptual color, each channel 0..1. Balanced + gamma-corrected. */
void ledrgb_set(LedRgb* rgb, float r, float g, float b);

/** Set the brightness cap 0..1 on all three channels at once. */
void ledrgb_setBrightness(LedRgb* rgb, float max_brightness);

/** All channels off. */
static inline void ledrgb_off(LedRgb* rgb) { ledrgb_set(rgb, 0.0f, 0.0f, 0.0f); }

#ifdef __cplusplus
}
#endif

#endif /* LED_RGB_H */
