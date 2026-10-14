/*
 * mem.c - GBA memory map and I/O register emulation.
 * Clean-room implementation, guided by BoycottAdvance 0.2.8's structure.
 */
#include "gba.h"
#include "mem.h"
#include "arm.h"

/* ---- helpers ---- */
static uint16_t rd16_al(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32_al(const uint8_t *p) { return p[0] | (p[1]<<8) | (p[2]<<16) | ((uint32_t)p[3]<<24); }
static void wr16_al(uint8_t *p, uint16_t v) { p[0] = v & 0xFF; p[1] = v >> 8; }
static void wr32_al(uint8_t *p, uint32_t v) { p[0]=v&0xFF; p[1]=(v>>8)&0xFF; p[2]=(v>>16)&0xFF; p[3]=(v>>24)&0xFF; }

/* IO register accessors (16-bit) - real registers */
uint16_t io_read16(GBA *g, uint32_t addr) {
    uint32_t reg = addr & 0x3FE;
    switch (reg) {
    case 0x00: return g->ppu.dispcnt;
    case 0x02: return g->ppu.dispstat;
    case 0x04: return g->ppu.vcount;
    case 0x06: return g->ppu.bgcnt[0];
    case 0x08: return g->ppu.bgcnt[1];
    case 0x0A: return g->ppu.bgcnt[2];
    case 0x0C: return g->ppu.bgcnt[3];
    case 0x10: return g->ppu.bghofs[0] & 0x3FF;
    case 0x12: return g->ppu.bgvofs[0] & 0x3FF;
    case 0x14: return g->ppu.bghofs[1] & 0x3FF;
    case 0x16: return g->ppu.bgvofs[1] & 0x3FF;
    case 0x18: return g->ppu.bghofs[2] & 0x3FF;
    case 0x1A: return g->ppu.bgvofs[2] & 0x3FF;
    case 0x1C: return g->ppu.bghofs[3] & 0x3FF;
    case 0x1E: return g->ppu.bgvofs[3] & 0x3FF;
    case 0x20: return g->ppu.bgaffx[0] & 0x1FF;
    case 0x22: return g->ppu.bgaffy[0] & 0x1FF;
    case 0x24: return g->ppu.bgaffx[1] & 0x1FF;
    case 0x26: return g->ppu.bgaffy[1] & 0x1FF;
    case 0x30: return g->ppu.bgaffpa[0];
    case 0x32: return g->ppu.bgaffpb[0];
    case 0x34: return g->ppu.bgaffpc[0];
    case 0x36: return g->ppu.bgaffpd[0];
    case 0x38: return g->ppu.bgaffpa[1];
    case 0x3A: return g->ppu.bgaffpb[1];
    case 0x3C: return g->ppu.bgaffpc[1];
    case 0x3E: return g->ppu.bgaffpd[1];
    case 0x40: return g->ppu.winh[0];
    case 0x42: return g->ppu.winh[1];
    case 0x44: return g->ppu.winv[0];
    case 0x46: return g->ppu.winv[1];
    case 0x48: return g->ppu.winin[0];
    case 0x4A: return g->ppu.winin[1];
    case 0x4C: return g->ppu.winout;
    case 0x50: return g->ppu.mosaic;
    case 0x52: return g->ppu.bldcnt;
    case 0x54: return g->ppu.blendalpha;
    case 0x56: return g->ppu.blendy;
    case 0x60: return g->timer[0].counter;
    case 0x62: return g->timer[0].reload;
    case 0x64: return g->timer[1].counter;
    case 0x66: return g->timer[1].reload;
    case 0x68: return g->timer[2].counter;
    case 0x6A: return g->timer[2].reload;
    case 0x6C: return g->timer[3].counter;
    case 0x6E: return g->timer[3].reload;
    case 0x70: return g->io[0x70] | (g->io[0x71] << 8);
    case 0x72: return g->io[0x72] | (g->io[0x73] << 8);
    case 0x74: return g->io[0x74] | (g->io[0x75] << 8);
    case 0x76: return g->io[0x76] | (g->io[0x77] << 8);
    case 0x80: return g->io[0x80] | (g->io[0x81] << 8);
    case 0x82: return g->io[0x82] | (g->io[0x83] << 8);
    case 0x84: return g->io[0x84] | (g->io[0x85] << 8);
    case 0x86: return g->io[0x86] | (g->io[0x87] << 8);
    case 0x90: return g->io[0x90] | (g->io[0x91] << 8);
    case 0x92: return g->io[0x92] | (g->io[0x93] << 8);
    case 0x94: return g->io[0x94] | (g->io[0x95] << 8);
    case 0x98: return g->io[0x98] | (g->io[0x99] << 8);   /* waitcnt */
    case 0xA0: return g->io[0xA0] | (g->io[0xA1] << 8);
    case 0xA2: return g->io[0xA2] | (g->io[0xA3] << 8);
    case 0xA4: return g->io[0xA4] | (g->io[0xA5] << 8);
    case 0xA6: return g->io[0xA6] | (g->io[0xA7] << 8);
    case 0xB0: return g->io[0xB0] | (g->io[0xB1] << 8);   /* IE */
    case 0xB2: return g->if_reg;
    case 0xB8: return g->io[0xB8] | (g->io[0xB9] << 8);
    case 0xC0: return g->io[0xC0] | (g->io[0xC1] << 8);   /* sound bias */
    case 0xD0: return g->io[0xD0] | (g->io[0xD1] << 8);
    default: return g->io[reg] | (g->io[reg+1] << 8);
    }
}

void io_write16(GBA *g, uint32_t addr, uint16_t v) {
    uint32_t reg = (addr & 0x3FE);
    switch (reg) {
    case 0x00: g->ppu.dispcnt = v; return;
    case 0x02: g->ppu.dispstat = (g->ppu.dispstat & 0x0007) | (v & 0xFFF8); return;
    case 0x04: g->ppu.vcount = v; return;
    case 0x06: g->ppu.bgcnt[0] = v; return;
    case 0x08: g->ppu.bgcnt[1] = v; return;
    case 0x0A: g->ppu.bgcnt[2] = v; return;
    case 0x0C: g->ppu.bgcnt[3] = v; return;
    case 0x10: g->ppu.bghofs[0] = v; return;
    case 0x12: g->ppu.bgvofs[0] = v; return;
    case 0x14: g->ppu.bghofs[1] = v; return;
    case 0x16: g->ppu.bgvofs[1] = v; return;
    case 0x18: g->ppu.bghofs[2] = v; return;
    case 0x1A: g->ppu.bgvofs[2] = v; return;
    case 0x1C: g->ppu.bghofs[3] = v; return;
    case 0x1E: g->ppu.bgvofs[3] = v; return;
    case 0x20: g->ppu.bgaffx[0] = v; return;
    case 0x22: g->ppu.bgaffy[0] = v; return;
    case 0x24: g->ppu.bgaffx[1] = v; return;
    case 0x26: g->ppu.bgaffy[1] = v; return;
    case 0x30: g->ppu.bgaffpa[0] = (int16_t)v; return;
    case 0x32: g->ppu.bgaffpb[0] = (int16_t)v; return;
    case 0x34: g->ppu.bgaffpc[0] = (int16_t)v; return;
    case 0x36: g->ppu.bgaffpd[0] = (int16_t)v; return;
    case 0x38: g->ppu.bgaffpa[1] = (int16_t)v; return;
    case 0x3A: g->ppu.bgaffpb[1] = (int16_t)v; return;
    case 0x3C: g->ppu.bgaffpc[1] = (int16_t)v; return;
    case 0x3E: g->ppu.bgaffpd[1] = (int16_t)v; return;
    case 0x40: g->ppu.winh[0] = v; return;
    case 0x42: g->ppu.winh[1] = v; return;
    case 0x44: g->ppu.winv[0] = v; return;
    case 0x46: g->ppu.winv[1] = v; return;
    case 0x48: g->ppu.winin[0] = v; return;
    case 0x4A: g->ppu.winin[1] = v; return;
    case 0x4C: g->ppu.winout = v; return;
    case 0x50: g->ppu.mosaic = v; return;
    case 0x52: g->ppu.bldcnt = v; return;
    case 0x54: g->ppu.blendalpha = v; return;
    case 0x56: g->ppu.blendy = v; return;
    /* timers */
    case 0x60: case 0x64: case 0x68: case 0x6C: {
        int ti = (reg >> 2) & 3;
        g->timer[ti].counter = v; return;
    }
    case 0x62: case 0x66: case 0x6A: case 0x6E: {
        int ti = (reg - 0x62) >> 2;
        g->timer[ti].reload = v;
        if (reg == 0x6E) {
            g->timer[ti].enabled  = v & 0x80;
            g->timer[ti].irq_enable = v & 0x40;
            g->timer[ti].count_up = v & 0x04;
            g->timer[ti].prescaler = 1;
            if ((v & 3) == 1) g->timer[ti].prescaler = 64;
            if ((v & 3) == 2) g->timer[ti].prescaler = 256;
            if ((v & 3) == 3) g->timer[ti].prescaler = 1024;
            if (v & 0x80) g->timer[ti].counter = g->timer[ti].reload & 0xFFFF;
        } else {
            g->timer[ti].enabled  = v & 0x80;
            g->timer[ti].irq_enable = v & 0x40;
            g->timer[ti].count_up = v & 0x04;
            g->timer[ti].prescaler = 1;
            if ((v & 3) == 1) g->timer[ti].prescaler = 64;
            if ((v & 3) == 2) g->timer[ti].prescaler = 256;
            if ((v & 3) == 3) g->timer[ti].prescaler = 1024;
            if (v & 0x80) g->timer[ti].counter = g->timer[ti].reload & 0xFFFF;
            else g->timer[ti].counter = g->timer[ti].reload & 0xFFFF;
        }
        return;
    }
    /* keypad control */
    case 0x70: g->io[0x70] = v & 0xFF; g->io[0x71] = v >> 8; return;
    case 0x72: g->io[0x72] = v & 0xFF; g->io[0x73] = v >> 8; return;
    case 0x74: g->io[0x74] = v & 0xFF; g->io[0x75] = v >> 8; return;
    case 0x76: g->io[0x76] = v & 0xFF; g->io[0x77] = v >> 8; return;
    /* serial */
    case 0x80: case 0x82: case 0x84: case 0x86:
    case 0x90: case 0x92: case 0x94: case 0x98:
    case 0xA0: case 0xA2: case 0xA4: case 0xA6:
        g->io[reg] = v & 0xFF; g->io[reg+1] = v >> 8; return;
    /* IE / IF */
    case 0xB0: g->ie = v; return;
    case 0xB2: g->if_reg &= ~(v & 0x3FFF); return;  /* writing 1 clears */
    case 0xB8: g->io[0xB8] = v & 0xFF; g->io[0xB9] = v >> 8; return;
    case 0xC0: g->io[0xC0] = v & 0xFF; g->io[0xC1] = v >> 8; return;
    case 0xD0: g->io[0xD0] = v & 0xFF; g->io[0xD1] = v >> 8; return;
    default:   g->io[reg] = v & 0xFF; g->io[reg+1] = v >> 8; return;
    }
}

/* DMA engine */
void dma_run(GBA *g, int ch) {
    DmaChan *d = &g->dma[ch];
    uint32_t src = d->src, dst = d->dst;
    int words = 0;
    if (ch == 3) { src &= 0x0FFFFFFF; dst &= 0x0FFFFFFF; }
    if (src >= 0x0E000000) src &= 0xFE;   /* internal rom */
    if (dst && !(dst & 0x00020000)) {}
    uint32_t count = d->count & 0x3FFF;
    if (count == 0 && (ch == 3)) count = 0x4000;
    if (count == 0) count = 0x4000;
    int inc_s = d->src_ctl, inc_d = d->dst_ctl;
    if (inc_s == 0) src = d->src;          /* fixed */
    if (inc_d == 0) dst = d->dst;
    uint32_t unit = d->word ? 4 : 2;
    for (uint32_t i = 0; i < count; i++) {
        if (d->word) {
            uint32_t v = (inc_s == 3) ? gba_read16(g, (src & ~3)) : gba_read16(g, src);
            if (d->word) v = gba_read32(g, src);
            if (inc_d == 3) gba_write16(g, dst & ~3, v & 0xFFFF);
            else gba_write32(g, dst, v);
        } else {
            uint32_t v = gba_read16(g, src);
            gba_write16(g, dst, v);
        }
        if (inc_s == 1) src += unit;
        if (inc_s == 3) src += 4;
        if (inc_d == 1) dst += unit;
        if (inc_d == 3) dst += 4;
        words++;
    }
    if (d->irq_enable) g->if_reg |= (0x100 << ch);
    if (!d->repeat) d->enabled = false;
    /* fixed-mode (src ctl 3) for DMAs uses fixed source */
    (void)words;
}

/* timer update */
void io_update_timers(GBA *g, uint32_t cycles) {
    /* simplistic: increment counters by cycles/prescaler */
    for (int i = 0; i < 4; i++) {
        Timer *t = &g->timer[i];
        if (!t->enabled) continue;
        if (t->count_up) continue;   /* chained: updated elsewhere */
        uint32_t inc = cycles / t->prescaler;
        uint32_t cnt = t->counter + inc;
        if (cnt > 0xFFFF) {
            t->counter = (cnt & 0xFFFF) + t->reload;
            if (t->irq_enable) g->if_reg |= (0x8 << i);
            if (i < 3) t->count_up = true; /* let next timer see overflow */
        } else t->counter = cnt;
    }
}

void io_trigger_keypad(GBA *g, uint32_t cycles) { (void)cycles;
    uint16_t en = g->io[0x72] | (g->io[0x73] << 8);   /* KEYCNT at 0x4000132 */
    uint16_t cnt = g->key_input;
    bool and_mode = en & 0x4000;
    uint8_t req = en & 0x3FF;
    bool hit;
    if (and_mode) hit = (cnt & req) == req;
    else hit = (cnt & req) != 0;
    if (hit) g->if_reg |= IF_KEYPAD;
}

/* ---- memory map ---- */
uint32_t gba_read32(GBA *g, uint32_t addr) {
    uint32_t page = addr >> 24;
    switch (page) {
    case 0x00: return rd32_al(g->bios + (addr & 0x3FFC));
    case 0x02: return rd32_al(g->ewram + (addr & 0x3FFFC));
    case 0x03: return rd32_al(g->iwram + (addr & 0x7FFC));
    case 0x04: {
        uint16_t lo = io_read16(g, addr);
        uint16_t hi = io_read16(g, addr + 2);
        return lo | (hi << 16);
    }
    case 0x05: return rd32_al(g->pal + (addr & 0x3FC));
    case 0x06: return rd32_al(g->vram + (addr & 0x17FFC));
    case 0x07: return rd32_al(g->oam + (addr & 0x3FC));
    case 0x08:
    case 0x09:
    case 0x0A:
    case 0x0B:
    case 0x0C:
    case 0x0D: {
        uint32_t off = (addr & (g->rom_mask | 0xFFFC)) & 0x1FFFFFF;
        if (off < g->rom_size) return rd32_al(g->rom + off);
        return 0;
    }
    case 0x0E: return rd32_al(g->rom + (addr & 0xFFFF));
    default: return 0;
    }
}

uint16_t gba_read16(GBA *g, uint32_t addr) {
    uint32_t page = addr >> 24;
    switch (page) {
    case 0x00: return rd16_al(g->bios + (addr & 0x3FFE));
    case 0x02: return rd16_al(g->ewram + (addr & 0x3FFFE));
    case 0x03: return rd16_al(g->iwram + (addr & 0x7FFE));
    case 0x04: {
        uint32_t reg = addr & 0x3FE;
        if (reg == 0x30 || reg == 0x32 || reg == 0x34 || reg == 0x36 ||
            reg == 0x38 || reg == 0x3A || reg == 0x3C || reg == 0x3E)
            return io_read16(g, addr);
        if (reg == 0x54) return g->ppu.blendalpha;
        if (reg == 0x00) return g->ppu.dispcnt;
        if (reg == 0x02) return g->ppu.dispstat;
        if (reg == 0x04) return g->ppu.vcount;
        if (reg == 0xB0) return g->ie;
        if (reg == 0xB2) return g->if_reg;
        return io_read16(g, addr);
    }
    case 0x05: return rd16_al(g->pal + (addr & 0x3FE));
    case 0x06: return rd16_al(g->vram + (addr & 0x17FFE));
    case 0x07: return rd16_al(g->oam + (addr & 0x3FE));
    case 0x08: case 0x09: case 0x0A: case 0x0B: case 0x0C: case 0x0D: {
        uint32_t off = (addr & (g->rom_mask | 0xFFFE)) & 0x1FFFFFF;
        if (off < g->rom_size) return rd16_al(g->rom + off);
        return 0;
    }
    case 0x0E: return rd16_al(g->rom + (addr & 0xFFFE));
    case 0x0F: return g->io[0];  /* unused */
    default: return 0;
    }
}

uint8_t gba_read8(GBA *g, uint32_t addr) {
    uint32_t page = addr >> 24;
    switch (page) {
    case 0x00: return g->bios[addr & 0x3FFF];
    case 0x02: return g->ewram[addr & 0x3FFFF];
    case 0x03: return g->iwram[addr & 0x7FFF];
    case 0x04: return (uint8_t)(io_read16(g, addr & ~1) >> ((addr & 1) ? 8 : 0));
    case 0x05: return g->pal[addr & 0x3FF];
    case 0x06: return g->vram[addr & 0x17FFF];
    case 0x07: return g->oam[addr & 0x3FF];
    case 0x08: case 0x09: case 0x0A: case 0x0B: case 0x0C: case 0x0D: {
        uint32_t off = (addr & (g->rom_mask | 0xFFFF)) & 0x1FFFFFF;
        if (off < g->rom_size) return g->rom[off];
        return 0;
    }
    case 0x0E: return g->rom[addr & 0xFFFF];
    default: return 0;
    }
}

void gba_write32(GBA *g, uint32_t addr, uint32_t v) {
    uint32_t page = addr >> 24;
    switch (page) {
    case 0x02: wr32_al(g->ewram + (addr & 0x3FFFC), v); return;
    case 0x03: wr32_al(g->iwram + (addr & 0x7FFC), v); return;
    case 0x04: io_write16(g, addr, v & 0xFFFF); io_write16(g, addr + 2, v >> 16); return;
    case 0x05: wr32_al(g->pal + (addr & 0x3FC), v); return;
    case 0x06: wr32_al(g->vram + (addr & 0x17FFC), v); return;
    case 0x07: wr32_al(g->oam + (addr & 0x3FC), v); return;
    /* write-protect ROM */
    default: return;
    }
}

void gba_write16(GBA *g, uint32_t addr, uint16_t v) {
    uint32_t page = addr >> 24;
    switch (page) {
    case 0x02: wr16_al(g->ewram + (addr & 0x3FFFE), v); return;
    case 0x03: wr16_al(g->iwram + (addr & 0x7FFE), v); return;
    case 0x04: io_write16(g, addr, v); return;
    case 0x05: wr16_al(g->pal + (addr & 0x3FE), v); return;
    case 0x06: wr16_al(g->vram + (addr & 0x17FFE), v); return;
    case 0x07: wr16_al(g->oam + (addr & 0x3FE), v); return;
    default: return;
    }
}

void gba_write8(GBA *g, uint32_t addr, uint8_t v) {
    uint32_t page = addr >> 24;
    switch (page) {
    case 0x02: g->ewram[addr & 0x3FFFF] = v; return;
    case 0x03: g->iwram[addr & 0x7FFF] = v; return;
    case 0x04: { uint16_t cur = io_read16(g, addr & ~1); if (addr & 1) cur = (cur & 0x00FF) | (v << 8); else cur = (cur & 0xFF00) | v; io_write16(g, addr & ~1, cur); return; }
    case 0x05: g->pal[addr & 0x3FF] = v; return;
    case 0x06: g->vram[addr & 0x17FFF] = v; return;
    case 0x07: g->oam[addr & 0x3FF] = v; return;
    default: return;
    }
}