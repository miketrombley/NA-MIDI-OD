#ifndef W25Q_H
#define W25Q_H

/**
 * @file w25q.h
 * @brief Driver for the Winbond W25Q-series SPI NOR flash (tested on W25Q16JV).
 *
 * Generic, blocking, standard-SPI (single-IO) driver: JEDEC ID, read, page
 * program, sector/block/chip erase, software reset, power-down release. Built
 * for our **U?? external flash on SPI2** (CS = PB12 / SPI2_CS) so we can persist
 * presets now and, later, stage firmware images for the PK-Bootloader.
 *
 * Chip geometry (W25Q16JV, 16 Mbit / 2 MB):
 *   - 256-byte pages          (program unit; a program can't cross a page bound)
 *   - 4 KB sectors  (512)     (smallest erase)
 *   - 32 / 64 KB blocks       (coarse erase)
 *   - whole chip              (chip erase)
 * NOR erases to all-1s (0xFF); programming only clears bits (1->0), so a region
 * MUST be erased before it can be re-written. JEDEC ID = 0xEF4015.
 *
 * Bus: standard SPI Mode 0 (CPOL=0, CPHA=0), MSB-first, <=50 MHz for Read Data
 * (03h). Our SPI2 runs /16 = 2.25 MHz — far inside spec. SPI2 is full-duplex
 * (MISO on PB14), so unlike the bias DPOT this driver can read back. CS is
 * software-driven (PB12), toggled per transaction.
 *
 * All calls block (HAL_SPI_Transmit/Receive + status-register polling). Erase
 * and program busy-wait on the BUSY bit; a sector erase can take up to ~400 ms,
 * so don't call these from an ISR or a tight real-time path — they're for the
 * boot-time load and the explicit "save preset" gesture.
 */

#include "stm32f1xx_hal.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Geometry (W25Q16JV) */
#define W25Q_PAGE_SIZE      256u
#define W25Q_SECTOR_SIZE    4096u
#define W25Q_BLOCK32_SIZE   32768u
#define W25Q_BLOCK64_SIZE   65536u
#define W25Q_TOTAL_SIZE     0x200000u   /* 2 MB */

/* Expected JEDEC ID (Read JEDEC ID 9Fh): EF=Winbond, 40=mem type, 15=16Mbit. */
#define W25Q_JEDEC_W25Q16JV 0x00EF4015u

typedef struct {
    SPI_HandleTypeDef* hspi;
    GPIO_TypeDef*      cs_port;
    uint16_t           cs_pin;
    uint32_t           jedec_id;   /* cached from init's Read JEDEC ID         */
    bool               present;    /* true if jedec_id looks like a real part  */
} W25Q;

/** Bind to an SPI bus + CS pin, leave CS idle-high, send Release Power-down,
 *  then read the JEDEC ID. Returns true (and sets d->present) when the ID has a
 *  plausible Winbond manufacturer byte (0xEF). Safe to ignore the result and
 *  just check d->present later. Does not touch the array. */
bool w25q_init(W25Q* d, SPI_HandleTypeDef* hspi,
               GPIO_TypeDef* cs_port, uint16_t cs_pin);

/** Read JEDEC ID (9Fh) -> 0x00MMTTCC (manufacturer/type/capacity). */
uint32_t w25q_read_jedec_id(W25Q* d);

/** Read Status Register-1 (05h). Bit0 = BUSY, bit1 = WEL. */
uint8_t w25q_read_status1(W25Q* d);

/** Poll BUSY until clear or timeout. Returns false on timeout. */
bool w25q_wait_ready(W25Q* d, uint32_t timeout_ms);

/** Read `len` bytes starting at `addr` (03h, auto-incrementing). No erase/align
 *  constraints; reads may span pages/sectors freely. */
void w25q_read(W25Q* d, uint32_t addr, uint8_t* buf, uint32_t len);

/** Erase the 4 KB sector containing `addr` (20h). Blocks until done (<=~400 ms).
 *  Returns false on timeout. */
bool w25q_erase_sector(W25Q* d, uint32_t addr);

/** Erase the whole chip (C7h). Blocks (up to ~25 s!). Returns false on timeout. */
bool w25q_erase_chip(W25Q* d, uint32_t timeout_ms);

/** Program up to one page (<=256 bytes) that does NOT cross a 256-byte page
 *  boundary (02h). The target must already be erased. Blocks until done.
 *  Returns false on bad args or timeout. Prefer w25q_write() for arbitrary
 *  lengths/addresses. */
bool w25q_page_program(W25Q* d, uint32_t addr, const uint8_t* buf, uint32_t len);

/** Program `len` bytes at `addr`, auto-splitting across page boundaries
 *  (issues one Page Program per page span). Does NOT erase first — the span
 *  must already be erased. Returns false on overflow/timeout. */
bool w25q_write(W25Q* d, uint32_t addr, const uint8_t* buf, uint32_t len);

/** Software reset (66h + 99h), then wait tRST (~30 us). Returns device to its
 *  power-on state (volatile settings cleared). */
void w25q_reset(W25Q* d);

#ifdef __cplusplus
}
#endif

#endif /* W25Q_H */
