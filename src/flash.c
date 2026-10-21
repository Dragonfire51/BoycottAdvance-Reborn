/* Parallel flash backup memory (0x0E000000 window).
 *
 * Command machine modelled on mGBA src/gba/savedata.c
 * (GBASavedataWriteFlash / GBASavedataReadFlash) and GBATEK
 * "GBA Cart Backup Flash ROM":
 *
 *   ID mode         AA@5555 55@2AAA 90@5555  ... reads of 0/1 return MFR/DEV
 *   exit ID mode    AA@5555 55@2AAA F0@5555  (a lone F0 is also accepted)
 *   program byte    AA@5555 55@2AAA A0@5555  then ONE write of the data byte
 *                   to the DESTINATION address (any address, not 5555)
 *   erase chip      AA 55 80@5555  AA 55 10@5555
 *   erase 4K sector AA 55 80@5555  AA 55 30@<any address inside the sector>
 *   bank switch     AA 55 B0@5555  then one write of 0/1 to address 0000
 *
 * Programming can only clear bits (flash cells go 1 -> 0), so the stored
 * byte is  old & new.
 */
#include <string.h>
#include "gba.h"
#include "mem.h"

/* JEDEC IDs (autoselect offset 0 = manufacturer, 1 = device).
   Same values mGBA reports: Panasonic 64K, Sanyo 128K. */
#define FLASH_MFR_SANYO       0x62
#define FLASH_DEV_SANYO_1M    0x13
#define FLASH_MFR_PANASONIC   0x32
#define FLASH_DEV_PANASONIC   0x1B

#define FLASH_UNLOCK_HI   0x5555
#define FLASH_UNLOCK_LO   0x2AAA

#define FLASH_CMD_NONE        0x00
#define FLASH_CMD_ERASE       0x80
#define FLASH_CMD_ID          0x90
#define FLASH_CMD_PROGRAM     0xA0
#define FLASH_CMD_SWITCH_BANK 0xB0
#define FLASH_CMD_RESET       0xF0
#define FLASH_CMD_ERASE_CHIP  0x10
#define FLASH_CMD_ERASE_SECT  0x30

/* Data# polling: while a program/erase "settles", reads return the value
   with bit 7 inverted (what Emerald's WaitForFlashWrite compares against). */
#define FLASH_SETTLE_READS    64

static uint32_t flash_size(const GBA *g) {
    return g->save_type == SAVE_FLASH128 ? 0x20000u : 0x10000u;
}

void flash_init(GBA *g) {
    Flash *f = &g->flash;
    f->state = FLASH_READY;
    f->command = FLASH_CMD_NONE;
    f->bank = 0;
    f->settle = 0;
    if (g->save_type == SAVE_FLASH128) {
        f->manufacturer = FLASH_MFR_SANYO;
        f->device       = FLASH_DEV_SANYO_1M;
    } else {
        f->manufacturer = FLASH_MFR_PANASONIC;
        f->device       = FLASH_DEV_PANASONIC;
    }
}

bool flash_in_use(const GBA *g) {
    return g->save_type == SAVE_FLASH64 || g->save_type == SAVE_FLASH128;
}

uint8_t flash_read(GBA *g, uint32_t addr) {
    Flash *f = &g->flash;
    uint32_t off = addr & 0xFFFF;

    if (f->command == FLASH_CMD_ID) {
        if (off == 0) return f->manufacturer;
        if (off == 1) return f->device;
        /* other offsets fall through to the array, as in mGBA */
    }
    if (f->settle > 0) {
        f->settle--;
        return (uint8_t)(g->save_mem[f->bank * 0x10000u + off] ^ 0x80);
    }
    return g->save_mem[f->bank * 0x10000u + off];
}

static void flash_erase_sector(GBA *g, uint32_t off) {
    Flash *f = &g->flash;
    uint32_t base = f->bank * 0x10000u + (off & 0xF000u);
    if (base + 0x1000u <= flash_size(g))
        memset(g->save_mem + base, 0xFF, 0x1000);
    g->save_dirty = true;
    f->settle = FLASH_SETTLE_READS;
}

void flash_write(GBA *g, uint32_t addr, uint8_t val) {
    Flash *f = &g->flash;
    uint32_t off = addr & 0xFFFF;

    switch (f->state) {
    case FLASH_READY:
        /* A latched command consumes the NEXT write, whatever its address
           (except unlock traffic, which cannot occur in between). */
        if (f->command == FLASH_CMD_PROGRAM) {
            g->save_mem[f->bank * 0x10000u + off] &= val;
            g->save_dirty = true;
            f->command = FLASH_CMD_NONE;
            f->settle = FLASH_SETTLE_READS;
        } else if (f->command == FLASH_CMD_SWITCH_BANK) {
            if (off == 0 && val < 2 && g->save_type == SAVE_FLASH128)
                f->bank = val;
            f->command = FLASH_CMD_NONE;
        } else if (off == FLASH_UNLOCK_HI && val == 0xAA) {
            f->state = FLASH_CMD1;
        } else if (val == FLASH_CMD_RESET && f->command == FLASH_CMD_ID) {
            f->command = FLASH_CMD_NONE;           /* single-cycle reset */
        }
        return;

    case FLASH_CMD1:
        f->state = (off == FLASH_UNLOCK_LO && val == 0x55) ? FLASH_CMD2 : FLASH_READY;
        return;

    case FLASH_CMD2:
        f->state = FLASH_READY;
        if (f->command == FLASH_CMD_ERASE) {
            /* Third cycle after 0x80: chip erase at 5555, sector erase at
               any address of the sector. Anything else cancels the erase. */
            if (val == FLASH_CMD_ERASE_CHIP && off == FLASH_UNLOCK_HI) {
                memset(g->save_mem, 0xFF, flash_size(g));
                g->save_dirty = true;
                f->settle = FLASH_SETTLE_READS;
            } else if (val == FLASH_CMD_ERASE_SECT) {
                flash_erase_sector(g, off);
            }
            f->command = FLASH_CMD_NONE;
        } else if (off == FLASH_UNLOCK_HI) {
            switch (val) {
            case FLASH_CMD_ID:          f->command = FLASH_CMD_ID; break;
            case FLASH_CMD_ERASE:       f->command = FLASH_CMD_ERASE; break;
            case FLASH_CMD_PROGRAM:     f->command = FLASH_CMD_PROGRAM; break;
            case FLASH_CMD_SWITCH_BANK: f->command = FLASH_CMD_SWITCH_BANK; break;
            case FLASH_CMD_RESET:       f->command = FLASH_CMD_NONE; break;
            default: break;
            }
        }
        return;

    default:
        f->state = FLASH_READY;
        return;
    }
}
