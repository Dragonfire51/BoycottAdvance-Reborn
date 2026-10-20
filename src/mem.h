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
void dma_trigger(GBA *g, int timing);

/* BIOS helpers */
void gba_swi(GBA *g, uint32_t comment);

/* serial EEPROM backup (0x0D000000 window) */
bool eeprom_addr_in_range(const GBA *g, uint32_t addr);
void eeprom_init(GBA *g);
void eeprom_dma_begin(GBA *g, uint32_t count);
void eeprom_write_bit(GBA *g, uint8_t bit);
uint8_t eeprom_read_bit(GBA *g);

/* battery save file (path is the ROM path with a .sav suffix) */
bool gba_save_battery(GBA *g, const char *rom_path);
void gba_load_battery(GBA *g, const char *rom_path);

#endif
