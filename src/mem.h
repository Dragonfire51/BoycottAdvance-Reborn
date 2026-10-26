#ifndef MEM_H
#define MEM_H

#include "gba.h"

uint8_t  gba_read8(GBA *g, uint32_t addr);
uint16_t gba_read16(GBA *g, uint32_t addr);
uint32_t gba_read32(GBA *g, uint32_t addr);
void gba_write8(GBA *g, uint32_t addr, uint8_t v);
void gba_write16(GBA *g, uint32_t addr, uint16_t v);
void gba_write32(GBA *g, uint32_t addr, uint32_t v);

/* ---- bus timing model (GBATEK "Memory Map", "GamePak Prefetch") ----------
 * Region waitstates from WAITCNT plus the GamePak prefetch buffer.  The CPU
 * core asks for the cost of one instruction fetch or one data access; the
 * internal 32-bit cycle is charged by the core, not here. */
void     bus_update_waitstates(GBA *g);         /* recompute from WAITCNT */
int      bus_fetch_cycles(GBA *g, uint32_t pc); /* instruction fetch at pc */
void     bus_prefetch_branch(GBA *g, uint32_t pc); /* reload after a branch */
int      bus_region(uint32_t addr);

/* internal IO register emulation (called from mem.c) */
void io_write16(GBA *g, uint32_t addr, uint16_t v);
uint16_t io_read16(GBA *g, uint32_t addr);
void io_update_timers(GBA *g, uint32_t cycles);
void io_trigger_keypad(GBA *g, uint32_t cycles);
void dma_run(GBA *g, int ch);
void dma_trigger(GBA *g, int timing);
void dma_fifo_request(GBA *g, int fifo);   /* timing 3: sound FIFO A (0) / B (1) */

/* BIOS helpers */
void gba_swi(GBA *g, uint32_t comment);
void bios_install_stub(GBA *g);             /* BIOS HLE minima: vector IRQ en 0x18 */

/* serial EEPROM backup (0x0D000000 window) */
bool eeprom_addr_in_range(const GBA *g, uint32_t addr);
void eeprom_init(GBA *g);
void eeprom_dma_begin(GBA *g, uint32_t count);
void eeprom_write_bit(GBA *g, uint8_t bit);
uint8_t eeprom_read_bit(GBA *g);

/* parallel flash backup (0x0E000000 window) */
void flash_init(GBA *g);
bool flash_in_use(const GBA *g);
uint8_t flash_read(GBA *g, uint32_t addr);
void flash_write(GBA *g, uint32_t addr, uint8_t val);

/* battery save file (path is the ROM path with a .sav suffix) */
bool gba_save_battery(GBA *g, const char *rom_path);
void gba_load_battery(GBA *g, const char *rom_path);

#endif
