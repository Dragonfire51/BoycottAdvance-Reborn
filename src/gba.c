/*
 * gba.c - GBA system core: init/reset, ROM load, frame loop, timing.
 * Clean-room implementation.
 */
#include "gba.h"
#include "mem.h"
#include "arm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const uint8_t builtin_bios[BIOS_SIZE] = {0};   /* zeros OK: many titles boot */

void gba_init(GBA *g) {
    memset(g, 0, sizeof(GBA));
    g->frameskip = 0;
    g->throttle = 60;
    g->bios_enabled = true;
    g->bios_fastboot = true;
    g->save_type = 0;
    g->cpu.cpsr = 0x0000001F;   /* SVC mode, IRQ disabled */
    g->cpu.m = 0;
    g->ppu.dispcnt = 0x0000;
    g->ppu.dispstat = 0;
    g->ppu.vcount = 0;
    g->ie = 0;
    g->if_reg = 0;
    g->ime = false;
    g->key_input = 0x03FF;        /* all released (low active) */
    g->cycle = 0;
    g->frame_count = 0;
    g->n_cheats = 0;
    g->state_buf = NULL;
    g->state_size = 0;
    g->rom_mask = ROM_MAX - 1;
    /* built-ins */
    memcpy(g->bios, builtin_bios, BIOS_SIZE);
}

void gba_reset(GBA *g) {
    memset(g->ewram, 0, sizeof(g->ewram));
    memset(g->iwram, 0, sizeof(g->iwram));
    memset(g->vram, 0, sizeof(g->vram));
    memset(g->oam, 0, sizeof(g->oam));
    memset(g->pal, 0, sizeof(g->pal));
    memset(g->io, 0, sizeof(g->io));
    memset(&g->timer, 0, sizeof(g->timer));
    memset(&g->dma, 0, sizeof(g->dma));
    memset(&g->apu, 0, sizeof(g->apu));
    memset(&g->ppu, 0, sizeof(g->ppu));
    g->cycle = 0;
    g->frame_count = 0;
    g->cst.halt = false;
    g->cst.stopped = false;
    g->ime = false;
    g->if_reg = 0;
    g->ie = 0;
    g->ppu.vcount = 0;

    /* Boot. The default (BIOS fast boot) replicates the state the real BIOS
       leaves after its init and jumps straight into the cartridge, so no
       copyrighted BIOS dump is required. With fast boot disabled, a BIOS
       image present and the intro not skipped, the ARM7 starts at
       0x00000000 and runs the BIOS. */
    if (g->bios_enabled && !g->bios_fastboot && !g->skip_intros &&
        g->bios[0] != 0) {
        g->cpu.cpsr = 0x0000001F;        /* system mode, ARM state, IRQ enabled */
        for (int i = 0; i < 16; i++) g->cpu.r[i] = 0;
        g->cpu.r[13] = 0x03007F00;       /* SP (USR/SYS) */
        g->cpu.r[15] = 0x00000000;       /* BIOS entry */
        g->cpu.bank[BANK_IRQ][5] = 0x03007FA0;
        g->cpu.bank[BANK_SVC][5] = 0x03007FE0;
        g->cpu.bank[0][5] = 0x03007F00;
        g->io[0x130] = 0xFF; g->io[0x131] = 0x03;
        g->key_input = 0x03FF;
        return;
    }

    g->cpu.cpsr = 0x0000001F;            /* system mode, ARM state, IRQ enabled */
    for (int i = 0; i < 16; i++) g->cpu.r[i] = 0;
    g->cpu.r[13] = 0x03007F00;           /* SP (USR/SYS) */
    g->cpu.r[15] = 0x08000000;           /* entry point in ROM */
    g->cpu.bank[BANK_IRQ][5] = 0x03007FA0;
    g->cpu.bank[BANK_SVC][5] = 0x03007FE0;
    g->cpu.bank[0][5] = 0x03007F00;
    /* default I/O state the BIOS sets up */
    g->io[0x204] = 0x00; g->io[0x205] = 0x00;
    g->io[0x130] = 0xFF; g->io[0x131] = 0x03;
    g->io[0x132] = 0x00; g->io[0x133] = 0x00;
    g->key_input = 0x03FF;
}

void gba_load_bios(GBA *g, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return;
    size_t got = fread(g->bios, 1, BIOS_SIZE, f);
    if (got < BIOS_SIZE) memset(g->bios + got, 0, BIOS_SIZE - got);
    fclose(f);
    g->bios_enabled = true;
}

bool gba_load_rom(GBA *g, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > ROM_MAX) { fclose(f); return false; }
    size_t got = fread(g->rom, 1, (size_t)sz, f);
    fclose(f);
    g->rom_size = (uint32_t)got;
    g->rom_mask = 0x1FFFFFF;
    if (g->rom_size <= 0x20000) g->rom_mask = 0x1FFFF;
    else if (g->rom_size <= 0x40000) g->rom_mask = 0x3FFFF;    /* 256K */
    else if (g->rom_size <= 0x80000) g->rom_mask = 0x7FFFF;    /* 512K */
    else if (g->rom_size <= 0x100000) g->rom_mask = 0xFFFFF;   /* 1M */
    else if (g->rom_size <= 0x200000) g->rom_mask = 0x1FFFFF;  /* 2M */
    else { g->rom_mask = 0x1FFFFFF; }
    /* title */
    memcpy(g->rom_title, g->rom + 0xA0, 12);
    g->rom_title[12] = 0;
    /* save type heuristic from header maker code etc. */
    g->save_type = 0;
    if (g->rom_size >= 0x100) {
        /* detect EEPROM/Flash/SRAM markers anywhere near 0xFC.. 0x100 is not enough.
           Simple: homebrew & most games without SRAM -> type 0 */
    }
    return true;
}

void gba_set_savetype(GBA *g, int type) {
    g->save_type = type;
    g->save_size = type == 3 ? 0x10000 : type == 2 ? 0x10000 : type == 1 ? 0x8000 : 0;
}

/* update keypad values */
void gba_update_keypad(GBA *g, uint16_t keys) {
    g->key_input = (uint16_t)(~keys) & 0x3FF;  /* 0 = pressed */
    io_trigger_keypad(g, 0);
}

/* Run one video frame: step the PPU line by line, running the CPU for the
 * cycles each scanline takes, firing HBlank/VBlank/VCount interrupts. */
static void run_frame_core(GBA *g) {
    const uint32_t line_cycles = 1232;      /* 240px @ 4 cyc + hblank */

    for (int line = 0; line < 228; line++) {
        g->ppu.vcount = (uint16_t)line;

        /* VBlank: start (line 160) sets IF and wakes the CPU */
        if (line == 160) {
            if (g->ppu.dispstat & 0x0008) g->if_reg |= IF_VBLANK;
            ppu_render_frame(g);
            g->frame_count++;
            if (g->cst.halt) g->cst.halt = false;   /* VBlankIntrWait satisfied */
        }

        /* render visible lines lazily (all at VBlank, already done above) */

        /* HBlank IRQ at the start of each line's blanking period */
        if (g->ppu.dispstat & 0x0010) g->if_reg |= IF_HBLANK;

        /* VCount match IRQ */
        if (g->ppu.dispstat & 0x0020) {
            int vm = (g->ppu.dispstat >> 8) & 0xFF;
            if (line == vm) g->if_reg |= IF_VCOUNT;
        }

        io_update_timers(g, line_cycles);

        /* Run the CPU for this scanline's cycles (interrupts handled inside) */
        arm_execute(g, line_cycles);
    }
    g->ppu.vcount = 0;
}

void gba_run_frame(GBA *g) {
    run_frame_core(g);
}

/* cheats */
void gba_add_cheat(GBA *g, uint32_t addr, uint16_t value) {
    if (g->n_cheats < 100) {
        g->cheats[g->n_cheats].addr = addr & 0xFFFFFF;
        g->cheats[g->n_cheats].value = value;
        g->cheats[g->n_cheats].stored = 0;
        g->cheats[g->n_cheats].active = true;
        g->cheats[g->n_cheats].is_gameshark = false;
        g->n_cheats++;
    }
}
void gba_clear_cheats(GBA *g) { g->n_cheats = 0; }
void gba_apply_cheats(GBA *g) {
    for (int i = 0; i < g->n_cheats; i++) {
        if (!g->cheats[i].active) continue;
        uint32_t addr = g->cheats[i].addr;
        uint16_t v = g->cheats[i].value;
        if (addr >= 0x02000000 && addr < 0x02040000)
            gba_write16(g, addr, v);
        else if (addr >= 0x04000000 && addr < 0x04000400) {
            if ((addr & 3) == 0) gba_write16(g, addr, v);
        }
    }
}

int gba_save_state(GBA *g, const char *path) {
    static uint8_t buf[1 << 20];
    size_t n = 0;
    /* store header */
    memcpy(buf+n, "BARS", 4); n += 4;
    buf[n++] = 1;   /* version */
    /* CPU */
    memcpy(buf+n, &g->cpu, sizeof(g->cpu)); n += sizeof(g->cpu);
    memcpy(buf+n, &g->cst, sizeof(g->cst)); n += sizeof(g->cst);
    memcpy(buf+n, &g->cycle, sizeof(g->cycle)); n += sizeof(g->cycle);
    /* mem */
    memcpy(buf+n, g->ewram, sizeof(g->ewram)); n += sizeof(g->ewram);
    memcpy(buf+n, g->iwram, sizeof(g->iwram)); n += sizeof(g->iwram);
    memcpy(buf+n, g->vram, sizeof(g->vram));   n += sizeof(g->vram);
    memcpy(buf+n, g->oam, sizeof(g->oam));     n += sizeof(g->oam);
    memcpy(buf+n, g->pal, sizeof(g->pal));     n += sizeof(g->pal);
    memcpy(buf+n, g->io, sizeof(g->io));       n += sizeof(g->io);
    memcpy(buf+n, g->save_mem, sizeof(g->save_mem)); n += sizeof(g->save_mem);
    memcpy(buf+n, &g->ie, sizeof(g->ie)); n += sizeof(g->ie);
    memcpy(buf+n, &g->if_reg, sizeof(g->if_reg)); n += sizeof(g->if_reg);
    memcpy(buf+n, &g->ime, sizeof(g->ime)); n += sizeof(g->ime);
    memcpy(buf+n, &g->ppu, sizeof(g->ppu)); n += sizeof(g->ppu);
    memcpy(buf+n, &g->timer, sizeof(g->timer)); n += sizeof(g->timer);
    memcpy(buf+n, &g->dma, sizeof(g->dma)); n += sizeof(g->dma);
    memcpy(buf+n, &g->apu, sizeof(g->apu)); n += sizeof(g->apu);
    memcpy(buf+n, &g->frameskip, sizeof(g->frameskip)); n += sizeof(g->frameskip);
    memcpy(buf+n, &g->rom_size, sizeof(g->rom_size)); n += sizeof(g->rom_size);
    memcpy(buf+n, &g->save_type, sizeof(g->save_type)); n += sizeof(g->save_type);

    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    fwrite(buf, 1, n, f);
    fclose(f);
    return 0;
}

int gba_load_state(GBA *g, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    static uint8_t buf[1 << 20];
    size_t got = fread(buf, 1, sizeof(buf), f);
    fclose(f);
    size_t n = 0;
    if (got < 8) return -1;
    if (memcmp(buf+n, "BARS", 4) != 0) return -1;
    n += 4;
    int ver = buf[n++];
    if (ver != 1) return -1;
    size_t cpu_sz = sizeof(g->cpu);
    if (n + cpu_sz > got) return -1;
    memcpy(&g->cpu, buf+n, cpu_sz); n += cpu_sz;
    size_t cst_sz = sizeof(g->cst);
    if (n + cst_sz > got) return -1;
    memcpy(&g->cst, buf+n, cst_sz); n += cst_sz;
    if (n + sizeof(g->cycle) > got) return -1;
    memcpy(&g->cycle, buf+n, sizeof(g->cycle)); n += sizeof(g->cycle);
    memcpy(g->ewram, buf+n, sizeof(g->ewram)); n += sizeof(g->ewram);
    memcpy(g->iwram, buf+n, sizeof(g->iwram)); n += sizeof(g->iwram);
    memcpy(g->vram, buf+n, sizeof(g->vram)); n += sizeof(g->vram);
    memcpy(g->oam, buf+n, sizeof(g->oam)); n += sizeof(g->oam);
    memcpy(g->pal, buf+n, sizeof(g->pal)); n += sizeof(g->pal);
    memcpy(g->io, buf+n, sizeof(g->io)); n += sizeof(g->io);
    memcpy(g->save_mem, buf+n, sizeof(g->save_mem)); n += sizeof(g->save_mem);
    memcpy(&g->ie, buf+n, sizeof(g->ie)); n += sizeof(g->ie);
    memcpy(&g->if_reg, buf+n, sizeof(g->if_reg)); n += sizeof(g->if_reg);
    memcpy(&g->ime, buf+n, sizeof(g->ime)); n += sizeof(g->ime);
    memcpy(&g->ppu, buf+n, sizeof(g->ppu)); n += sizeof(g->ppu);
    memcpy(&g->timer, buf+n, sizeof(g->timer)); n += sizeof(g->timer);
    memcpy(&g->dma, buf+n, sizeof(g->dma)); n += sizeof(g->dma);
    memcpy(&g->apu, buf+n, sizeof(g->apu)); n += sizeof(g->apu);
    memcpy(&g->frameskip, buf+n, sizeof(g->frameskip)); n += sizeof(g->frameskip);
    memcpy(&g->rom_size, buf+n, sizeof(g->rom_size)); n += sizeof(g->rom_size);
    memcpy(&g->save_type, buf+n, sizeof(g->save_type)); n += sizeof(g->save_type);
    return 0;
}
