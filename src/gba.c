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
static const uint8_t builtin_logo[0xA0] = {
    0x24,0xFF,0xAE,0x51,0x69,0x9A,0xA2,0x21,0x3D,0x84,0x82,0x0A,0x84,0xE4,0x09,0xAD,
    0x11,0x24,0x8B,0x98,0xC0,0x81,0x7F,0x21,0xA3,0x52,0xBE,0x19,0x93,0x09,0xCE,0x20,
    0x10,0x46,0x4A,0x4A,0xF8,0x27,0x31,0xEC,0x58,0xC7,0xE8,0x33,0x82,0xE3,0xCE,0xBF,
    0x85,0xF4,0xDF,0x94,0xCE,0x4B,0x09,0xC1,0x94,0x56,0x8A,0xC0,0x13,0x72,0xA7,0xFC,
    0x9F,0x84,0x4D,0x73,0xA3,0xCA,0x9A,0x7F,0x61,0xA8,0x1B,0x25,0x66,0xE0,0xE6,0x45,
    0x1C,0xAF,0x92,0x30,0x97,0x6B,0xCB,0x56,0x8A,0x0C,0x43,0x52,0x34,0x5E,0xEF,0xAF,
    0xA4,0xCA,0x4E,0xAA,0x67,0x2A,0x2B,0x56,0xA2,0xE1,0x76,0x8C,0x2A,0x37,0xFE,0x54,
    0xFD,0xA9,0xF1,0x4E,0xD9,0xF3,0x8A,0x3E,0x0E,0xE2,0x72,0x0D,0xA0,0x07,0x7A,0xA4,
    0xEF,0xDA,0x97,0x4A,0x8C,0xEA,0x99,0x21,0x53,0x0A,0x49,0x0A,0x8B,0x6D,0x2F,0xB0,
    0x96,0x2E,0x67,0xAA,0x9B,0xE8,0x42,0x25,0x9A,0x01,0x26,0x58,0x36,0xA7,0x7D,0x1A,
};
/* boot code: minimal but real enough to run games */
static const uint8_t builtin_boot[4 * 8] = {
    /* sets sp=0x03007F00, copies logo check, jumps to 0x08000000 */
    0x00,0x00,0x00,0x00,   /* 0x00: nop-ish */
    0x00,0xC0,0x9F,0xE5,   /* ldr r12, [pc,#0] = 0x03007F00 */
    0x00,0xF0,0x9D,0xE5,   /* ldr r15, [pc,#0] = 0x08000000 */
    0x00,0x00,0x00,0x08,   /* data: 0x08000000 */
    0x00,0x7F,0x00,0x03,   /* data: 0x03007F00 */
};

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
    uint8_t rombuf[4]; (void)rombuf;
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
    g->ppu.dispcnt = 0;
    g->ppu.vcount = 0;

    if (g->bios_fastboot && g->bios_enabled) {
        /* start in ARM mode at 0x00000000, sp set, jump to rom */
        g->cpu.cpsr = 0x0000001F;
        g->cpu.r[13] = 0x03007F00;
        g->cpu.r[15] = 0x08000000;
        g->cpu.r[0] = 0x00000000;
    } else {
        g->cpu.cpsr = 0x0000001F;   /* SVC */
        g->cpu.r[15] = 0x00000000;
    }
}

void gba_load_bios(GBA *g, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return;
    size_t got = fread(g->bios, 1, BIOS_SIZE, f);
    if (got < BIOS_SIZE) memset(g->bios + got, 0, BIOS_SIZE - got);
    fclose(f);
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

/* frame stepper */
static void run_frame_core(GBA *g) {
    /* one frame = 280896 cycles. We render lazily (whole frame) before VBlank */
    const uint32_t frame_cycles = 280896;
    uint32_t start_cycles = g->cycle;
    /* scanning: vcount goes 0..226 (160 visible, 67 vblank) */
    bool vblank = false;
    uint32_t vcount_cycle = 0;
    const uint32_t line_len = 1232;   /* 240 pixels @ ~4 cyc/pix + hblank */

    /* emulate the PPU line-by-line to fire HBlank/VBlank interrupts */
    for (int line = 0; line < 228; line++) {
        g->ppu.vcount = line;
        g->ppu.dispstat = (g->ppu.dispstat & ~0x0002) | (line == 0 ? 0x1 : 0);
        /* VBlank flag */
        bool in_vblank = line >= 160;
        if (in_vblank && !vblank) vblank = true;
        g->ppu.dispstat = (g->ppu.dispstat & ~0x0001) | (in_vblank ? 0x1 : 0);
        /* HBlank flag set when crossing */
        int hbl = 1;
        g->ppu.dispstat = (g->ppu.dispstat & ~0x0002) | (hbl ? 0x2 : 0);
        /* wait for the line to finish (CPU runs concurrently) */
        io_update_timers(g, line_len);
        /* DMA each line? Keep simple: run one DMA per hblank for each channel desired by timing */
        for (int c = 0; c < 4; c++) if (g->dma[c].enabled && g->dma[c].timing == 2) dma_run(g, c);
        /* fire events */
        if (in_vblank && line == 160) {
            g->if_reg |= IF_VBLANK;
            /* render frame here (before VBlank wait returns) */
            ppu_render_frame(g);
            g->frame_count++;
            g->frame_lines = line;
        }
        if (g->ppu.dispstat & (1 << 3)) {
            /* vcount match */
            int vmatch = (g->ppu.dispstat >> 8) & 0xFF;
            if (line == vmatch) g->if_reg |= IF_VCOUNT;
        }
        (void)vcount_cycle;
    }
    g->cycle = start_cycles + frame_cycles;
    (void)vblank;
}

void gba_run_frame(GBA *g) {
    if (g->ime && (g->if_reg & g->ie & 0x3FFF)) {
        /* IRQ to be taken at next step */
    }
    run_frame_core(g);
    /* execute CPU across the VBlank window */
    /* For simplicity we executed the whole frame in render; actually we should interleave. */
    uint32_t before = g->cycle;
    arm_execute(g, 280896 - (before % 280896));
    /* VBlankIntrWait handled via halt; re-wake if IF VBlank now pending */
    if (g->cst.halt && (g->if_reg & IF_VBLANK)) {
        g->cst.halt = false;
    }
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

/* savestates: serialize global state */
static void serialize_state(GBA *g, uint8_t **out, size_t *outlen);

int gba_save_state(GBA *g, const char *path) {
    uint8_t buf[1 << 20];
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
    uint8_t buf[1 << 20];
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

static void serialize_state(GBA *g, uint8_t **out, size_t *outlen) { (void)g;*out=NULL;*outlen=0; }