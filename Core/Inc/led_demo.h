#ifndef LED_DEMO_H
#define LED_DEMO_H

/**
 * @file led_demo.h
 * @brief Throwaway visual demos for the two RGB LEDs. Not part of the product
 *        UI — delete the call from main() once the LEDs are proven out.
 */

#include "led_rgb.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Continuously-rotating rainbow shared across both LEDs. `right` lags `left`
 *  in hue so the color appears to flow left -> right. Call every loop pass and
 *  feed it HAL_GetTick(); it derives the animation phase from the timestamp. */
void led_demo_rainbow(LedRgb* left, LedRgb* right, uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif /* LED_DEMO_H */
