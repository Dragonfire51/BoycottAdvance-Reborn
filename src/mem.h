#ifndef MEM_H
#define MEM_H

#include "gba.h"

uint8_t  gba_read8(GBA *g, uint32_t addr);
uint16_t gba_read16(GBA *g, uint32_t addr);
uint32_t gba_read32(GBA *g, uint32_t addr);
void gba_write8(GBA *g, uint32_t addr, uint8_t v);
void gba_write16(GBA *g, uint32_t addr, uint16_t v);
void gba_write32(GBA *g, uint32_t addr, uint32_t v);

/* internal IO register emulation (called from mem.c) */
void io_write16(GBA *g, uint32_t addr, uint16_t v);
uint16_t io_read16(GBA *g, uint32_t addr);
void io_update_timers(GBA *g, uint32_t cycles);
void io_trigger_keypad(GBA *g, uint32_t cycles);
void dma_run(GBA *g, int ch);

/* BIOS helpers */
void gba_swi(GBA *g, uint32_t comment);

#endif