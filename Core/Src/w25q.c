/**
 * @file w25q.c
 * @brief Winbond W25Q-series SPI NOR flash driver. See w25q.h for the model.
 *
 * Standard single-IO SPI, blocking. Every transaction is framed by CS low ->
 * HAL_SPI_Transmit (command [+ address] [+ data]) [-> HAL_SPI_Receive] -> CS
 * high. Writes/erases set WEL (06h) first, then poll BUSY until the self-timed
 * cycle finishes.
 */

#include "w25q.h"

/* Instruction set (subset we use — see W25Q16JV datasheet section 9). */
#define CMD_WRITE_ENABLE   0x06u
#define CMD_READ_STATUS1   0x05u
#define CMD_READ_DATA      0x03u   /* up to 50 MHz; we're at 2.25 MHz */
#define CMD_PAGE_PROGRAM   0x02u
#define CMD_SECTOR_ERASE   0x20u   /* 4 KB  */
#define CMD_CHIP_ERASE     0xC7u
#define CMD_JEDEC_ID       0x9Fu
#define CMD_RELEASE_PD     0xABu   /* Release Power-down / Device ID */
#define CMD_ENABLE_RESET   0x66u
#define CMD_RESET_DEVICE   0x99u

#define STATUS_BUSY        0x01u   /* SR1 bit0 — erase/program/write in progress */

#define SPI_TIMEOUT_MS     100u    /* per HAL transfer (bytes are tiny @2.25 MHz) */

/* --- CS + raw transfer helpers -------------------------------------------- */

static inline void cs_low(W25Q* d)  { HAL_GPIO_WritePin(d->cs_port, d->cs_pin, GPIO_PIN_RESET); }
static inline void cs_high(W25Q* d) { HAL_GPIO_WritePin(d->cs_port, d->cs_pin, GPIO_PIN_SET);  }

static void tx(W25Q* d, const uint8_t* buf, uint16_t len)
{
    HAL_SPI_Transmit(d->hspi, (uint8_t*)buf, len, SPI_TIMEOUT_MS);
}

static void rx(W25Q* d, uint8_t* buf, uint16_t len)
{
    /* Full-duplex master: Receive clocks out dummy bytes while latching MISO. */
    HAL_SPI_Receive(d->hspi, buf, len, SPI_TIMEOUT_MS);
}

/* A single-byte command with no address/data (06h, C7h, etc.). */
static void cmd1(W25Q* d, uint8_t cmd)
{
    cs_low(d);
    tx(d, &cmd, 1);
    cs_high(d);
}

/* Build a [cmd, A23..A16, A15..A8, A7..A0] header into a 4-byte buffer. */
static void put_cmd_addr(uint8_t* hdr, uint8_t cmd, uint32_t addr)
{
    hdr[0] = cmd;
    hdr[1] = (uint8_t)(addr >> 16);
    hdr[2] = (uint8_t)(addr >> 8);
    hdr[3] = (uint8_t)(addr);
}

/* --- Status / readiness --------------------------------------------------- */

uint8_t w25q_read_status1(W25Q* d)
{
    uint8_t cmd = CMD_READ_STATUS1, sr = 0;
    cs_low(d);
    tx(d, &cmd, 1);
    rx(d, &sr, 1);
    cs_high(d);
    return sr;
}

bool w25q_wait_ready(W25Q* d, uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();
    while (w25q_read_status1(d) & STATUS_BUSY) {
        if ((HAL_GetTick() - start) >= timeout_ms) return false;
    }
    return true;
}

static void write_enable(W25Q* d) { cmd1(d, CMD_WRITE_ENABLE); }

/* --- Identity / lifecycle ------------------------------------------------- */

uint32_t w25q_read_jedec_id(W25Q* d)
{
    uint8_t cmd = CMD_JEDEC_ID, id[3] = {0};
    cs_low(d);
    tx(d, &cmd, 1);
    rx(d, id, 3);
    cs_high(d);
    return ((uint32_t)id[0] << 16) | ((uint32_t)id[1] << 8) | id[2];
}

void w25q_reset(W25Q* d)
{
    cmd1(d, CMD_ENABLE_RESET);
    cmd1(d, CMD_RESET_DEVICE);
    /* tRST ~30 us; HAL tick granularity is 1 ms — a short spin is plenty. */
    for (volatile uint32_t i = 0; i < 2000; ++i) { __NOP(); }
}

bool w25q_init(W25Q* d, SPI_HandleTypeDef* hspi,
               GPIO_TypeDef* cs_port, uint16_t cs_pin)
{
    d->hspi     = hspi;
    d->cs_port  = cs_port;
    d->cs_pin   = cs_pin;
    d->jedec_id = 0;
    d->present  = false;

    cs_high(d);                 /* idle before first frame */

    cmd1(d, CMD_RELEASE_PD);    /* wake in case it powered down; harmless otherwise */
    for (volatile uint32_t i = 0; i < 2000; ++i) { __NOP(); }  /* tRES ~3 us */

    d->jedec_id = w25q_read_jedec_id(d);
    /* Accept any Winbond part (mfr byte 0xEF) — keeps the driver usable if the
     * stuffed density ever changes. Caller can check the full ID if it cares. */
    d->present = (((d->jedec_id >> 16) & 0xFFu) == 0xEFu);
    return d->present;
}

/* --- Read ----------------------------------------------------------------- */

void w25q_read(W25Q* d, uint32_t addr, uint8_t* buf, uint32_t len)
{
    uint8_t hdr[4];
    put_cmd_addr(hdr, CMD_READ_DATA, addr);
    cs_low(d);
    tx(d, hdr, 4);
    rx(d, buf, (uint16_t)len);
    cs_high(d);
}

/* --- Erase ---------------------------------------------------------------- */

bool w25q_erase_sector(W25Q* d, uint32_t addr)
{
    uint8_t hdr[4];
    put_cmd_addr(hdr, CMD_SECTOR_ERASE, addr);

    write_enable(d);
    cs_low(d);
    tx(d, hdr, 4);
    cs_high(d);
    return w25q_wait_ready(d, 1000u);   /* tSE max ~400 ms */
}

bool w25q_erase_chip(W25Q* d, uint32_t timeout_ms)
{
    write_enable(d);
    cmd1(d, CMD_CHIP_ERASE);
    return w25q_wait_ready(d, timeout_ms);   /* tCE up to ~25 s */
}

/* --- Program -------------------------------------------------------------- */

bool w25q_page_program(W25Q* d, uint32_t addr, const uint8_t* buf, uint32_t len)
{
    if (len == 0 || len > W25Q_PAGE_SIZE) return false;
    /* A page program must not cross a 256-byte page boundary (it wraps). */
    if ((addr & (W25Q_PAGE_SIZE - 1)) + len > W25Q_PAGE_SIZE) return false;

    uint8_t hdr[4];
    put_cmd_addr(hdr, CMD_PAGE_PROGRAM, addr);

    write_enable(d);
    cs_low(d);
    tx(d, hdr, 4);
    tx(d, buf, (uint16_t)len);     /* CS stays low across header + data */
    cs_high(d);
    return w25q_wait_ready(d, 50u);   /* tPP max ~3 ms */
}

bool w25q_write(W25Q* d, uint32_t addr, const uint8_t* buf, uint32_t len)
{
    if (addr + len > W25Q_TOTAL_SIZE) return false;

    while (len) {
        uint32_t page_left = W25Q_PAGE_SIZE - (addr & (W25Q_PAGE_SIZE - 1));
        uint32_t chunk     = (len < page_left) ? len : page_left;
        if (!w25q_page_program(d, addr, buf, chunk)) return false;
        addr += chunk;
        buf  += chunk;
        len  -= chunk;
    }
    return true;
}
