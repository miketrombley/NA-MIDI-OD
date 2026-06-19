#include "cvout.h"

void cvout_init(CvOut* c, TIM_HandleTypeDef* htim, uint32_t channel,
                float duty_min, float duty_max)
{
    c->htim     = htim;
    c->channel  = channel;
    c->duty_min = duty_min;
    c->duty_max = duty_max;
    cvout_set(c, 0.0f);
}

void cvout_set(CvOut* c, float control)
{
    if (control < 0.0f) control = 0.0f;
    else if (control > 1.0f) control = 1.0f;

    /* Linear interpolate the duty between the calibrated endpoints. */
    float duty = c->duty_min + control * (c->duty_max - c->duty_min);
    if (duty < 0.0f) duty = 0.0f;
    else if (duty > 1.0f) duty = 1.0f;

    /* Non-inverted: CCR 0 => 0 V, CCR (ARR+1) => full Vdd. */
    uint32_t arr = __HAL_TIM_GET_AUTORELOAD(c->htim);
    uint32_t ccr = (uint32_t)(duty * (float)(arr + 1) + 0.5f);
    if (ccr > arr + 1) ccr = arr + 1;
    __HAL_TIM_SET_COMPARE(c->htim, c->channel, ccr);
}
