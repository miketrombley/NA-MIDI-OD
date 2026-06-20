#ifndef PRESET_STORE_H
#define PRESET_STORE_H

/**
 * @file preset_store.h
 * @brief Non-volatile persistence for the preset snapshot, on external SPI flash.
 *
 * Bridges the (HAL-free) preset state machine to the W25Q flash driver. Keeps
 * preset.c pure: this is the only place that knows the on-flash byte layout and
 * the reserved flash address. One record, CRC-checked, in its own 4 KB sector.
 *
 * Flash memory map (W25Q16JV, 2 MB) — see Flash_Profile.md:
 *   0x000000 .. 0x1EFFFF  reserved for future firmware images (PK-Bootloader)
 *   0x1F0000 .. 0x1FFFFF  config block (64 KB)
 *     0x1FF000 .. 0x1FFFFF  preset record sector (this module)
 *
 * On a valid record, load injects it via preset_load_snapshot() (boots LIVE; a
 * tap recalls it). Save erases the sector and writes a fresh record — call it
 * from the commit gesture, NOT every loop (erase blocks up to ~400 ms).
 */

#include "preset.h"
#include "w25q.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Reserved preset record location (last 4 KB sector of the config block). */
#define PRESET_STORE_ADDR   0x1FF000u

/** Read + validate the stored record; on success load it into `p` (sets
 *  has_preset) and return true. Returns false if flash is absent, the record is
 *  blank/unwritten, or the CRC/magic/version don't check out — `p` is untouched
 *  in that case (host just boots with no preset). */
bool preset_store_load(W25Q* flash, Preset* p);

/** Snapshot the current preset values from `p`, erase the record sector, and
 *  write a fresh CRC-stamped record. Returns false on any flash error. */
bool preset_store_save(W25Q* flash, const Preset* p);

#ifdef __cplusplus
}
#endif

#endif /* PRESET_STORE_H */
