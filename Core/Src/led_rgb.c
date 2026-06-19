#include "led_rgb.h"
#include "sp1513.h"

void ledrgb_init_split(LedRgb* rgb,
                       TIM_HandleTypeDef* htim_r, uint32_t ch_r,
                       TIM_HandleTypeDef* htim_g, uint32_t ch_g,
                       TIM_HandleTypeDef* htim_b, uint32_t ch_b,
                       LEDPolarity polarity, float max_brightness)
{
    led_init(&rgb->r, htim_r, ch_r, polarity, LED_CURVE_GAMMA_2_2, max_brightness);
    led_init(&rgb->g, htim_g, ch_g, polarity, LED_CURVE_GAMMA_2_2, max_brightness);
    led_init(&rgb->b, htim_b, ch_b, polarity, LED_CURVE_GAMMA_2_2, max_brightness);
}

void ledrgb_init(LedRgb* rgb, TIM_HandleTypeDef* htim,
                 uint32_t ch_r, uint32_t ch_g, uint32_t ch_b,
                 LEDPolarity polarity, float max_brightness)
{
    ledrgb_init_split(rgb, htim, ch_r, htim, ch_g, htim, ch_b,
                      polarity, max_brightness);
}

void ledrgb_set(LedRgb* rgb, float r, float g, float b)
{
    /* White balance once, here, so both the raw color and any animation render
     * an equal-RGB request as neutral. normalize() stays perceptual; each
     * led_setBrightness() then applies gamma 2.2 + cap + active-low invert. */
    sp1513_Color c = sp1513_normalize((sp1513_Color){ r, g, b });
    led_setBrightness(&rgb->r, c.r);
    led_setBrightness(&rgb->g, c.g);
    led_setBrightness(&rgb->b, c.b);
}

void ledrgb_setBrightness(LedRgb* rgb, float max_brightness)
{
    if (max_brightness < 0.0f) max_brightness = 0.0f;
    else if (max_brightness > 1.0f) max_brightness = 1.0f;
    rgb->r.max_brightness = max_brightness;
    rgb->g.max_brightness = max_brightness;
    rgb->b.max_brightness = max_brightness;
}
