/*
 * eeprom.c - serial EEPROM backup memory (4Kbit / 64Kbit).
 *
 * GBATEK: the EEPROM hangs off the cartridge's D0 data line and the top
 * ROM address line; the CPU talks to it one bit at a time, and only
 * through DMA. A DMA transfer to or from 0x0D000000 carries one protocol
 * bit per halfword in its LSB.
 *
 * The DMA count tells the chip which phase it is in:
 *
 *   command       4Kbit count   64Kbit count
 *   ------------  -----------   ------------
 *   read addr     9             17
 *   read data     68            68
 *   write         73            81
 *   ready poll    1             1
 *
 * We lock addr_bits (6 for 4Kbit, 14 for 64Kbit) on the first command
 * transfer we see, then follow the bit stream.
 */
#include <string.h>

#include "gba.h"

bool eeprom_addr_in_range(const GBA *g, uint32_t addr) {
    if (g->save_type != SAVE_EEPROM) return false;
    if (addr < 0x0D000000 || addr >= 0x0E000000) return false;
    /* Carts over 16MB only expose EEPROM in the last 256 bytes of the
     * window; everything below is ROM mirror. */
    if (g->rom_size > 0x01000000) return addr >= 0x0DFFFF00;
    return true;
}

void eeprom_init(GBA *g) {
    memset(&g->eeprom, 0, sizeof(g->eeprom));
    /* Erased EEPROM cells read back as 1. */
    memset(g->eeprom.data, 0xFF, sizeof(g->eeprom.data));
    g->eeprom.state = EEPROM_IDLE;
}

void eeprom_dma_begin(GBA *g, uint32_t count) {
    Eeprom *e = &g->eeprom;
    switch (count) {
    case 9:  case 73: if (e->addr_bits == 0) e->addr_bits = 6;  e->state = EEPROM_RX_CMD; e->rx_count = 0; break;
    case 17: case 81: if (e->addr_bits == 0) e->addr_bits = 14; e->state = EEPROM_RX_CMD; e->rx_count = 0; break;
    case 68: /* read-data return; state already set by the command transfer */ break;
    case 1:  /* ready poll; eeprom_read_bit reports 1 while idle/ready */      break;
    default: /* unknown count: leave the state alone so we do not desync */    break;
    }
}

static void eeprom_load_shift_for_read(Eeprom *e) {
    uint32_t mask = (e->addr_bits == 6) ? 0x3F : 0x3FFF;
    e->addr &= (uint16_t)mask;
    size_t base = (size_t)e->addr * 8;
    uint64_t v = 0;
    if (base + 8 <= sizeof(e->data))
        for (int i = 0; i < 8; i++) v = (v << 8) | e->data[base + i];
    e->shift = v;
}

static void eeprom_commit_write(Eeprom *e) {
    uint32_t mask = (e->addr_bits == 6) ? 0x3F : 0x3FFF;
    e->addr &= (uint16_t)mask;
    size_t base = (size_t)e->addr * 8;
    if (base + 8 > sizeof(e->data)) return;
    for (int i = 0; i < 8; i++)
        e->data[base + i] = (uint8_t)(e->shift >> ((7 - i) * 8));
    e->dirty = true;
}

void eeprom_write_bit(GBA *g, uint8_t bit) {
    Eeprom *e = &g->eeprom;
    bit &= 1;
    if (e->addr_bits == 0) return;   /* no command seen yet */

    switch (e->state) {
    case EEPROM_RX_CMD:
        if (e->rx_count == 0) {
            if (bit == 0) return;    /* first bit must be the 1 start bit */
            e->rx_count = 1;
        } else {
            e->state = (bit == 1) ? EEPROM_RX_ADDR_R : EEPROM_RX_ADDR_W;
            e->rx_count = 0;
            e->addr = 0;
        }
        return;

    case EEPROM_RX_ADDR_R:
        if (e->rx_count < e->addr_bits) e->addr = (uint16_t)((e->addr << 1) | bit);
        e->rx_count++;
        if (e->rx_count > e->addr_bits) {     /* address + 1 terminator bit */
            eeprom_load_shift_for_read(e);
            e->state = EEPROM_TX_DATA;
            e->tx_count = 0;
        }
        return;

    case EEPROM_RX_ADDR_W:
        e->addr = (uint16_t)((e->addr << 1) | bit);
        e->rx_count++;
        if (e->rx_count == e->addr_bits) {
            e->state = EEPROM_RX_DATA;
            e->rx_count = 0;
            e->shift = 0;
        }
        return;

    case EEPROM_RX_DATA:
        if (e->rx_count < 64) e->shift = (e->shift << 1) | bit;
        e->rx_count++;
        if (e->rx_count > 64) {               /* 64 data bits + terminator */
            eeprom_commit_write(e);
            e->state = EEPROM_READY;
        }
        return;

    default:
        return;   /* stray write during read/ready: ignore */
    }
}

uint8_t eeprom_read_bit(GBA *g) {
    Eeprom *e = &g->eeprom;
    if (e->state == EEPROM_TX_DATA) {
        uint8_t bit;
        if (e->tx_count < 4) {
            bit = 0;                          /* four leading dummy zeros */
        } else {
            int idx = 63 - (int)(e->tx_count - 4);
            bit = (uint8_t)((e->shift >> idx) & 1);
        }
        e->tx_count++;
        if (e->tx_count >= 68) e->state = EEPROM_IDLE;
        return bit;
    }
    return 1;   /* idle/ready: report ready */
}
