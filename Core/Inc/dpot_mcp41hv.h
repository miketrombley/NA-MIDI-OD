#ifndef DPOT_MCP41HV_H
#define DPOT_MCP41HV_H

/**
 * @file dpot_mcp41hv.h
 * @brief Driver for the MCP41HV31 high-voltage digital potentiometer (SPI).
 *
 * Used here as the **bias control**: the pot is wired as a voltage divider
 * (P0A = +5V_A, P0B = -5V_A) with the wiper buffered (OPA1677) into +V_BIAS.
 * So the wiper voltage swings the full -5 V .. +5 V as the code moves:
 *
 *     code 0   = wiper at B = V_B (~-4.35 V)  (max negative bias = most gated)
 *     code 63  = mid-scale  = rail center     (~+0.28 V, POR default)
 *     code 127 = wiper at A = V_A (~+5.0 V)   (max positive bias)
 *
 * NOTE: the rails are ASYMMETRIC (the negative rail only reaches ~-4.35 V via
 * the inverter), so mid-scale is NOT 0 V — it's the geometric center of the
 * swing, ~+0.28 V. Gating is referenced from THIS center, not from 0 V.
 * (The 0 V output crossing is code ~59.) See Bias_Profile.md.
 *
 * This part is a **7-bit** device (MCP41HV31): 128 taps, codes 0..127,
 * POR/BOR default mid-scale = 0x3F (63). SDO is unconnected on the board, so
 * the driver is transmit-only (no readback).
 *
 * SPI: Mode 0,0 (CPOL=0, CPHA=0), 8-bit, MSB-first, software NSS, <=10 MHz.
 * Bind it to the SPI handle + the CS GPIO; the driver toggles CS per write.
 */

#include "stm32f1xx_hal.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MCP41HV_CODE_MAX   127u   /* 7-bit full-scale (wiper at A, +5 V)      */
#define MCP41HV_CODE_MID    63u   /* 0x3F, POR mid-scale (0 V, no gating)     */

typedef struct {
    SPI_HandleTypeDef* hspi;
    GPIO_TypeDef*      cs_port;
    uint16_t           cs_pin;
    int16_t            last_code;  /* last code sent; -1 = nothing written yet */
} Mcp41hv;

/** Bind to an SPI bus + CS pin. Leaves CS idle-high; does NOT write the pot
 *  (it powers up at mid-scale 0x3F on its own). */
void mcp41hv_init(Mcp41hv* d, SPI_HandleTypeDef* hspi,
                  GPIO_TypeDef* cs_port, uint16_t cs_pin);

/** Raw 7-bit wiper write (0..127). Always transmits, updates last_code.
 *  Handy for bench profiling (code -> measured +V_BIAS). */
void mcp41hv_write(Mcp41hv* d, uint8_t code);

/** Change-detected raw write: set wiper to `code` (0..127) but only touch SPI
 *  when it differs from the last code sent (kills zipper noise at 100 Hz).
 *  Returns true if a write happened. Use when mapping a pot to a code range. */
bool mcp41hv_set_code(Mcp41hv* d, uint8_t code);

/** Map control [0..1] -> wiper code, **symmetric about mid-scale** so 0.5
 *  lands on the rail center (code 63 = ~+0.28 V), our gating reference:
 *      0.0 -> 0 (~-4.35 V) ,  0.5 -> 63 (~+0.28 V) ,  1.0 -> 126 (~+4.85 V)
 *  (The top LSB is sacrificed to keep the +/- code swing symmetric about 63.)
 *
 *  Writes SPI ONLY when the resulting code changes from the last one, so it
 *  can be called every control loop (e.g. 100 Hz pot read) without generating
 *  zipper noise. Returns true if a write actually happened. */
bool mcp41hv_set(Mcp41hv* d, float control);

#ifdef __cplusplus
}
#endif

#endif /* DPOT_MCP41HV_H */
