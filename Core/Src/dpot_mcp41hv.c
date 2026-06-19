#include "dpot_mcp41hv.h"
#include <math.h>

void mcp41hv_init(Mcp41hv* d, SPI_HandleTypeDef* hspi,
                  GPIO_TypeDef* cs_port, uint16_t cs_pin)
{
    d->hspi      = hspi;
    d->cs_port   = cs_port;
    d->cs_pin    = cs_pin;
    d->last_code = -1;

    /* CS idle-high (inactive). The pot already booted at mid-scale 0x3F. */
    HAL_GPIO_WritePin(cs_port, cs_pin, GPIO_PIN_SET);
}

void mcp41hv_write(Mcp41hv* d, uint8_t code)
{
    if (code > MCP41HV_CODE_MAX) code = MCP41HV_CODE_MAX;

    /* 16-bit "Write Data" to volatile Wiper 0 (datasheet Fig 7-2):
     *   command byte = [AD3..AD0 = 0000][C1 C0 = 00][D9 D8 = 00] = 0x00
     *   data byte    = [D7..D0]            = wiper code (7-bit fits in D6..D0)
     * CS must frame exactly 16 clocks; we drive CS low, send 2 bytes, CS high. */
    uint8_t tx[2] = { 0x00, code };

    HAL_GPIO_WritePin(d->cs_port, d->cs_pin, GPIO_PIN_RESET);
    HAL_SPI_Transmit(d->hspi, tx, 2, HAL_MAX_DELAY);   /* blocks until both bytes sent */
    HAL_GPIO_WritePin(d->cs_port, d->cs_pin, GPIO_PIN_SET);

    d->last_code = code;
}

bool mcp41hv_set_code(Mcp41hv* d, uint8_t code)
{
    if (code > MCP41HV_CODE_MAX) code = MCP41HV_CODE_MAX;

    /* Change-detect: only touch SPI when the tap actually moves -> no zipper
     * noise when called every loop from the 100 Hz pot read. */
    if ((int16_t)code == d->last_code) return false;

    mcp41hv_write(d, code);
    return true;
}

bool mcp41hv_set(Mcp41hv* d, float control)
{
    if (control < 0.0f) control = 0.0f;
    else if (control > 1.0f) control = 1.0f;

    /* Symmetric about mid-scale: 0.5 -> 63 (rail center, ~+0.28 V),
     * 0.0 -> 0 (~-4.35 V), 1.0 -> 126 (~+4.85 V). 126 (not 127) keeps the
     * code swing symmetric about the center tap 63. Rails are asymmetric, so
     * the center is NOT 0 V (that's ~code 59) — see Bias_Profile.md. */
    uint8_t code = (uint8_t)lroundf(control * 126.0f);
    return mcp41hv_set_code(d, code);
}
