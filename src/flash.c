/* Parallel flash backup memory (0x0E000000 window).
 *
 * Pokemon Emerald identifies its save chip through the JEDEC autoselect
 * sequence (0xAA/0x55 unlock, then 0x90) and reads the manufacturer and
 * device bytes back before it will touch the save block at all. A plain byte
 * array cannot answer that probe, so the game stalls in its save
 * initialisation and never leaves the BIOS handoff.
 *
 * The command machine follows mGBA src/gba/savedata.c
 * (GBASavedataWriteFlash / GBASavedataReadFlash): a command latch separated
 * from the two-step unlock, so erase needs a second unlock pair before the
 * sector address is presented. See also GBATEK "Backup Memory - FLASH".
 */
#include <string.h>
#include "gba.h"
#include "mem.h"

/* JEDEC IDs as they appear at autoselect offsets 0 (manufacturer) and 1
   (device). A 1M cartridge answers with the Sanyo part, 512K with Panasonic;
   Emerald's save driver accepts either and only needs a non-0xFF answer. */
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
#define FLASH_CMD_ERASE_CHIP  0x10
#define FLASH_CMD_ERASE_SECT  0x30

/* Reads return the data# polling bit while a program/erase settles. Emerald's
   save driver spins on it, so it has to change at some point; a short window
   is enough because nothing here is cycle accurate. */
#define FLASH_SETTLE_READS    64

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
        return 0;
    }
    if (f->settle > 0) {
        f->settle--;
        return (uint8_t)(g->save_mem[f->bank * 0x10000 + off] ^ 0x80);
    }
    return g->save_mem[f->bank * 0x10000 + off];
}

void flash_write(GBA *g, uint32_t addr, uint8_t val) {
    Flash *f = &g->flash;
    uint32_t off = addr & 0xFFFF;

    switch (f->state) {
    case FLASH_READY:
        /* A latched command consumes the next hi-address write. */
        switch (f->command) {
        case FLASH_CMD_PROGRAM:
            if (off == FLASH_UNLOCK_HI) {
                g->save_mem[f->bank * 0x10000 + off] &= val;
                g->save_dirty = true;
                f->command = FLASH_CMD_NONE;
                f->settle = FLASH_SETTLE_READS;
            }
            return;
        case FLASH_CMD_SWITCH_BANK:
            if (off == FLASH_UNLOCK_HI) {
                if (g->save_type == SAVE_FLASH128) f->bank ^= 1;
                f->command = FLASH_CMD_NONE;
            }
            return;
        default:
            if (off == FLASH_UNLOCK_HI && val == 0xAA) f->state = FLASH_CMD1;
            return;
        }

    case FLASH_CMD1:
        f->state = (off == FLASH_UNLOCK_LO && val == 0x55) ? FLASH_CMD2 : FLASH_READY;
        break;

    case FLASH_CMD2:
        f->state = FLASH_READY;
        /* An erase sector address can land anywhere, unlike the other
           commands which only respond at the unlock address. */
        if (f->command == FLASH_CMD_ERASE && val == FLASH_CMD_ERASE_SECT) {
            uint32_t base = (f->bank * 0x10000) + (off & 0xF000);
            if (base + 0x1000 <= sizeof(g->save_mem))
                memset(g->save_mem + base, 0xFF, 0x1000);
            g->save_dirty = true;
            f->command = FLASH_CMD_NONE;
            f->settle = FLASH_SETTLE_READS;
            break;
        }
        if (off == FLASH_UNLOCK_HI) {
            switch (val) {
            case FLASH_CMD_ID:           f->command = FLASH_CMD_ID; break;
            case FLASH_CMD_ERASE:        f->command = FLASH_CMD_ERASE; break;
            case FLASH_CMD_PROGRAM:      f->command = FLASH_CMD_PROGRAM; break;
            case FLASH_CMD_SWITCH_BANK:  f->command = FLASH_CMD_SWITCH_BANK; break;
            case 0xF0:                   f->command = FLASH_CMD_NONE; break;
            default: break;
            }
        }
        break;

    case FLASH_AUTOSELECT:
        if (val == 0xF0) { f->command = FLASH_CMD_NONE; f->state = FLASH_READY; }
        break;

    case FLASH_PROGRAM:
        f->state = FLASH_READY;
        break;

    case FLASH_ERASE:
        /* Reached after the 0x80 latch; the sector address arrives once the
           unlock pair has been repeated. */
        f->state = FLASH_READY;
        if (off == FLASH_UNLOCK_HI && val == 0xAA) {
            f->state = FLASH_CMD1;
        } else if (val == FLASH_CMD_ERASE_CHIP) {
            memset(g->save_mem, 0xFF, sizeof(g->save_mem));
            g->save_dirty = true;
            f->command = FLASH_CMD_NONE;
            f->settle = FLASH_SETTLE_READS;
        } else if (val == FLASH_CMD_ERASE_SECT) {
            uint32_t base = (f->bank * 0x10000) + (off & 0xF000);
            if (base + 0x1000 <= sizeof(g->save_mem))
                memset(g->save_mem + base, 0xFF, 0x1000);
            g->save_dirty = true;
            f->command = FLASH_CMD_NONE;
            f->settle = FLASH_SETTLE_READS;
        }
        break;

    default:
        f->state = FLASH_READY;
        break;
    }
}
