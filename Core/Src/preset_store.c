/**
 * @file preset_store.c
 * @brief Preset persistence on external SPI flash. See preset_store.h.
 *
 * One fixed-size record in its own 4 KB sector: a magic + version header, the
 * six pot values, and a CRC32 over everything before it. Load validates all
 * three before trusting the snapshot; a blank (0xFF) or corrupt sector simply
 * reads as "no preset" and the unit boots clean.
 */

#include "preset_store.h"
#include <string.h>

#define STORE_MAGIC    0x4D4F4450u   /* 'MODP' — MIDI-OD Preset */
#define STORE_VERSION  1u

/* On-flash record. Field order is chosen so every member is naturally aligned
 * and the struct has ZERO padding (4 + 2 + 2 + 6*4 + 4 = 36 B for 6 pots), so
 * the in-RAM layout == the on-flash bytes with no need to pack. The static
 * assert locks that: if PRESET_NUM_POTS or a type changes the size, bump
 * STORE_VERSION so old records are rejected rather than mis-read.
 * CRC covers magic .. values[] (everything up to, but not including, crc). */
typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t num_pots;
    float    values[PRESET_NUM_POTS];
    uint32_t crc;
} PresetRecord;
_Static_assert(sizeof(PresetRecord) == 12u + 4u * PRESET_NUM_POTS,
               "PresetRecord must be padding-free for a stable on-flash layout");

/* Bitwise CRC32 (poly 0xEDB88320, init/final 0xFFFFFFFF) — no table, no HAL.
 * Tiny and only runs on load/save, so the per-byte loop cost is irrelevant. */
static uint32_t crc32(const uint8_t* p, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < len; ++i) {
        crc ^= p[i];
        for (int b = 0; b < 8; ++b)
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1u)));
    }
    return crc ^ 0xFFFFFFFFu;
}

bool preset_store_load(W25Q* flash, Preset* p)
{
    if (!flash || !flash->present) return false;

    PresetRecord rec;
    w25q_read(flash, PRESET_STORE_ADDR, (uint8_t*)&rec, sizeof(rec));

    if (rec.magic != STORE_MAGIC)        return false;   /* blank or foreign */
    if (rec.version != STORE_VERSION)    return false;
    if (rec.num_pots != PRESET_NUM_POTS) return false;

    uint32_t want = crc32((const uint8_t*)&rec, sizeof(rec) - sizeof(rec.crc));
    if (want != rec.crc)                 return false;    /* corrupt */

    preset_load_snapshot(p, rec.values);
    return true;
}

bool preset_store_save(W25Q* flash, const Preset* p)
{
    if (!flash || !flash->present) return false;

    PresetRecord rec;
    memset(&rec, 0, sizeof(rec));
    rec.magic    = STORE_MAGIC;
    rec.version  = STORE_VERSION;
    rec.num_pots = PRESET_NUM_POTS;
    for (int i = 0; i < PRESET_NUM_POTS; ++i)
        rec.values[i] = preset_value(p, i);
    rec.crc = crc32((const uint8_t*)&rec, sizeof(rec) - sizeof(rec.crc));

    if (!w25q_erase_sector(flash, PRESET_STORE_ADDR)) return false;
    return w25q_write(flash, PRESET_STORE_ADDR, (const uint8_t*)&rec, sizeof(rec));
}
