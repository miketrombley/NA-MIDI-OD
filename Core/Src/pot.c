#include "pot.h"
#include <math.h>

void pot_init(Pot* p, ADC_HandleTypeDef* hadc, uint32_t channel)
{
    p->hadc       = hadc;
    p->channel    = channel;
    /* Pots are high-impedance sources, so give the sample-and-hold plenty of
     * settling time. 239.5 cycles @ 12 MHz ADC ~= 21 us/conversion — trivial at
     * a 100 Hz poll, and rock-steady readings. */
    p->sampletime = ADC_SAMPLETIME_239CYCLES_5;
    p->raw        = 0;
    p->value      = -1.0f;   /* sentinel: first poll() always reports change */
}

float pot_read(Pot* p)
{
    /* Point the ADC's single regular conversion at this pot's channel. */
    ADC_ChannelConfTypeDef cfg = {0};
    cfg.Channel      = p->channel;
    cfg.Rank         = ADC_REGULAR_RANK_1;
    cfg.SamplingTime = p->sampletime;
    HAL_ADC_ConfigChannel(p->hadc, &cfg);

    float v = (p->value < 0.0f) ? 0.0f : p->value;  /* hold last on timeout */
    HAL_ADC_Start(p->hadc);
    if (HAL_ADC_PollForConversion(p->hadc, 5) == HAL_OK) {
        p->raw = (uint16_t)HAL_ADC_GetValue(p->hadc);  /* 12-bit, 0..4095 */
        v = (float)p->raw / 4095.0f;
    }
    HAL_ADC_Stop(p->hadc);

    /* End-of-travel correction: rescale the live span to a full 0..1. */
    v = (v - POT_RAIL_LO) / (POT_RAIL_HI - POT_RAIL_LO);
    if (v < 0.0f) v = 0.0f;
    else if (v > 1.0f) v = 1.0f;

    p->value = v;
    return v;
}

bool pot_poll(Pot* p)
{
    float prev = p->value;
    float v = pot_read(p);
    if (prev < 0.0f) return true;             /* first read */
    return fabsf(v - prev) > POT_HYSTERESIS;
}
