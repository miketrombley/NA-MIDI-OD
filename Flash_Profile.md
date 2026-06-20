# Flash_Profile.md — external SPI NOR (W25Q16JV)

External serial flash on **SPI2** for non-volatile storage. The STM32F105 has
**no EEPROM**, so this part is how presets (and, later, firmware images for the
PK-Bootloader) survive a power-cycle. Read this before touching `w25q.[ch]`,
`preset_store.[ch]`, or the flash memory map.

## Part
- **Winbond W25Q16JVSNIQ** — 16 Mbit / **2 MB**, SOIC-8 150-mil, 2.7–3.6 V,
  -40…+85 °C, 100k erase cycles, 20-yr retention.
- Geometry: 256-byte **pages** (program unit), 4 KB **sectors** (smallest erase),
  32/64 KB blocks, whole-chip erase. NOR erases to `0xFF`; programming only
  clears bits (1→0), so **a region must be erased before re-writing**.
- JEDEC ID (9Fh) = `EF 40 15` → `0x00EF4015` (`EF` = Winbond, `40` = type,
  `15` = 16 Mbit). The "IQ" variant ships **QE=1 fixed** — irrelevant to us, we
  drive it in plain single-IO standard SPI.

## Wiring / bus (SPI2 — already in CubeMX, no `.ioc` change needed)
- **SCK = PB13, MISO = PB14, MOSI = PB15**, full-duplex master, Mode 0
  (CPOL=0/CPHA=0), MSB-first, **/16 = 2.25 MHz** (APB1 36 MHz ÷16). Far inside
  the 50 MHz Read-Data (03h) limit; bump the prescaler later if we want faster
  firmware-image transfers.
- **CS = PB12** — labeled `SPI2_CS`, software-driven GPIO push-pull, **idle-high**
  (CubeMX PinState = SET). The driver toggles it per transaction.
- Unlike the bias DPOT (SPI3, 1-line TX-only), SPI2 is full-duplex so this driver
  **reads back** (JEDEC ID, status register, data).

The flash chip pins map to the W25Q16JV SOIC-8: /CS, DO(IO1), /WP(IO2), GND,
DI(IO0), CLK, /HOLD(IO3), VCC. For single-IO use tie /WP and /HOLD high (or leave
per the board's pull-ups). Confirm those two are not strapped low on the PCB or
status-register writes / clocking will misbehave.

## Driver — `w25q.[ch]`
Generic, **blocking**, standard single-IO SPI. Bind it to an SPI handle + CS pin
(`w25q_init` → wakes the part with Release Power-down ABh, reads the JEDEC ID,
sets `present` if the manufacturer byte is `0xEF`). API:
- `w25q_read(addr, buf, len)` — Read Data (03h), auto-incrementing, any length.
- `w25q_erase_sector(addr)` — Sector Erase (20h), 4 KB, blocks (tSE ≤ ~400 ms).
- `w25q_page_program(addr, buf, len)` — Page Program (02h), ≤256 B, must not
  cross a 256-byte page boundary (it wraps), target must be pre-erased.
- `w25q_write(addr, buf, len)` — convenience: splits across page boundaries.
- `w25q_erase_chip()`, `w25q_reset()` (66h+99h), `w25q_read_status1()`,
  `w25q_wait_ready(timeout)`.

Every erase/program asserts Write Enable (06h) then busy-waits the SR1 BUSY bit.
**These block** (a sector erase can stall ~400 ms) — only call them from boot or
an explicit user gesture, never from an ISR or the audio/control hot path.

If the chip doesn't answer at boot (`flash.present == false`), all of
`preset_store_*` no-op and the unit runs RAM-only — a missing/dead flash never
bricks boot.

## Flash memory map
```
0x000000 .. 0x1EFFFF   reserved — future PK-Bootloader firmware images (~1.9 MB)
0x1F0000 .. 0x1FFFFF   config block (64 KB)
  0x1FF000 .. 0x1FFFFF   preset record sector (4 KB)  ← preset_store
```
Presets live in the **last 4 KB sector** so the bottom of the array stays clear
for firmware staging. When the bootloader work starts, carve its image
slot(s) out of the bottom region and keep config at the top.

## Preset persistence — `preset_store.[ch]`
Keeps `preset.c` HAL-free; this is the only code that knows the on-flash layout
and address. One CRC-checked record (`PresetRecord`): magic `'MODP'`, version,
`num_pots`, the six `float` pot values, CRC32 (poly `0xEDB88320`, bitwise — no
table). The struct is padding-free (a `_Static_assert` locks `sizeof`); bump
`STORE_VERSION` if the layout ever changes so old records are rejected, not
mis-read.

- **Boot** (`main.c` USER CODE 2, after `preset_init`): `w25q_init` then
  `preset_store_load`. A valid record is injected via `preset_load_snapshot`
  (sets `has_preset`); the unit still **boots LIVE** (LED2 off) — the first SW_2
  tap recalls it, same as before. Blank/corrupt/foreign sector → loads nothing.
- **Save**: only on the preset **commit** (second ~1 s SW_2 hold, SAVE_ARMED →
  PRESET). `main.c` detects that edge and calls `preset_store_save`, which erases
  the record sector and writes a fresh CRC-stamped record. The ~tens-to-400 ms
  erase blocks the loop briefly — fine for a deliberate gesture (MIDI RX is
  buffered in its ISR, the TIM7 smoother keeps the outputs gliding; only the
  LEDs/loop pause for that moment).

## TODO / future
- **PK-Bootloader firmware updates**: a modified PK-Bootloader (see
  `../PK-Bootloader`) will stage a new image in the bottom region over USB/DFU,
  verify it, then flash the F105. Reuse `w25q.[ch]` for the staging read/write.
- **Endurance / wear**: presets re-erase the same 4 KB sector on every commit.
  100k cycles is plenty for hand saves; if multi-slot or auto-save ever lands,
  rotate records across the 16 sectors of the config block (simple log + newest
  -valid-CRC wins) instead of erase-in-place.
- **Multi-slot presets**: `PresetRecord` already carries `num_pots`/version;
  growing to N slots is an array of records (one per sector) + a slot index.
- **Factory preset**: a built-in default snapshot in firmware to seed
  `preset_load_snapshot()` when `preset_store_load` fails (blank/corrupt flash),
  and as a "restore to factory" target. See `Preset_Profile.md` Future / planned.
- **Faster bus** for image transfers: raise the SPI2 prescaler (Read Data is
  good to 50 MHz; other commands to 133 MHz).
