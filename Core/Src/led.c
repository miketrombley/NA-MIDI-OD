#include "led.h"
#include "sp1513.h"   /* sp1513_gamma22() shares the gamma curve */
#include <math.h>

/* ---- helpers ------------------------------------------------------------ */

static inline float clamp_01(float v)
{
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

static float apply_exponential(float linear)
{
    /* (e^(3x) - 1) / (e^3 - 1): stays dim, then ramps hard. */
    const float e3 = 20.0855f;
    return (expf(3.0f * linear) - 1.0f) / (e3 - 1.0f);
}

static float apply_curve(const LEDDriver* led, float linear)
{
    switch (led->curve) {
        case LED_CURVE_GAMMA_2_2:   return sp1513_gamma22(linear);
        case LED_CURVE_EXPONENTIAL: return apply_exponential(linear);
        case LED_CURVE_LINEAR:
        default:                    return linear;
    }
}

/* Convert a 0..1 PWM duty into a compare value and write it to the channel.
 * For up-counting PWM mode 1 the output is active while CNT < CCR, so a CCR of
 * (ARR+1) is a true 100% and 0 is a true 0%. */
static void write_duty(const LEDDriver* led, float duty)
{
    duty = clamp_01(duty);
    uint32_t arr = __HAL_TIM_GET_AUTORELOAD(led->htim);   /* period (ARR)      */
    uint32_t ccr = (uint32_t)(duty * (float)(arr + 1) + 0.5f);
    if (ccr > arr + 1) ccr = arr + 1;
    __HAL_TIM_SET_COMPARE(led->htim, led->channel, ccr);
}

/* ---- public API --------------------------------------------------------- */

void led_init(LEDDriver* led, TIM_HandleTypeDef* htim, uint32_t channel,
              LEDPolarity polarity, LEDBrightnessCurve curve, float max_brightness)
{
    led->htim    = htim;
    led->channel = channel;
    led->polarity = polarity;
    led->curve    = curve;
    led->max_brightness = clamp_01(max_brightness);
    led->current_brightness = 0.0f;
    led->enabled = true;
    led_setBrightness(led, 0.0f);   /* start off */
}

void led_setBrightness(LEDDriver* led, float brightness)
{
    brightness = clamp_01(brightness);
    led->current_brightness = brightness;

    float b = led->enabled ? brightness : 0.0f;

    /* perceptual -> linear duty, then cap */
    float duty = apply_curve(led, b) * led->max_brightness;

    /* active-low (common anode): invert so duty 0 drives the cathode low = ON */
    if (led->polarity == LED_POLARITY_ACTIVE_LOW)
        duty = 1.0f - duty;

    write_duty(led, duty);
}

void led_setEnabled(LEDDriver* led, bool enable)
{
    led->enabled = enable;
    led_setBrightness(led, led->current_brightness);
}

float led_getBrightness(const LEDDriver* led)
{
    return led->current_brightness;
}
