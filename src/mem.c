/*
 * mem.c - GBA memory map and I/O register emulation.
 * Clean-room implementation, guided by BoycottAdvance 0.2.8's structure
 * and the public GBA hardware reference (GBATEK layout).
 *
 * I/O registers are addressed by their real offsets from 0x04000000,
 * e.g. DISPCNT=0x000, VCOUNT=0x006, IE=0x200, IF=0x202, IME=0x208,
 * KEYINPUT=0x130, timers=0x100, DMA=0x0B0, sound=0x060.
 */
#include "gba.h"
#include "mem.h"
#include "arm.h"

/* ---- helpers ---- */
static uint16_t rd16_al(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32_al(const uint8_t *p) { return p[0] | (p[1]<<8) | (p[2]<<16) | ((uint32_t)p[3]<<24); }
static void wr16_al(uint8_t *p, uint16_t v) { p[0] = v & 0xFF; p[1] = v >> 8; }
static void wr32_al(uint8_t *p, uint32_t v) { p[0]=v&0xFF; p[1]=(v>>8)&0xFF; p[2]=(v>>16)&0xFF; p[3]=(v>>24)&0xFF; }

void dma_run(GBA *g, int ch);

/* =========================================================================
 * I/O reads (16-bit register space)
 * ========================================================================= */
uint16_t io_read16(GBA *g, uint32_t addr) {
    uint32_t reg = addr & 0x3FE;
    switch (reg) {
    /* ---- display ---- */
    case 0x000: return g->ppu.dispcnt;
    case 0x002: {
        uint16_t v = g->ppu.dispstat & 0xFFF8;
        if (g->ppu.vcount >= 160) v |= 0x0001;               /* VBlank */
        if (g->ppu.vcount == ((g->ppu.dispstat >> 8) & 0xFF)) v |= 0x0004;
        return v;
    }
    case 0x004: return g->ppu.vcount;
    case 0x006: return g->ppu.bgcnt[0];
    case 0x008: return g->ppu.bgcnt[1];
    case 0x00A: return g->ppu.bgcnt[2];
    case 0x00C: return g->ppu.bgcnt[3];
    case 0x010: return g->ppu.bghofs[0];
    case 0x012: return g->ppu.bgvofs[0];
    case 0x014: return g->ppu.bghofs[1];
    case 0x016: return g->ppu.bgvofs[1];
    case 0x018: return g->ppu.bghofs[2];
    case 0x01A: return g->ppu.bgvofs[2];
    case 0x01C: return g->ppu.bghofs[3];
    case 0x01E: return g->ppu.bgvofs[3];
    case 0x020: return g->ppu.bgaffx[0];
    case 0x022: return g->ppu.bgaffy[0];
    case 0x024: return g->ppu.bgaffx[1];
    case 0x026: return g->ppu.bgaffy[1];
    case 0x030: return g->ppu.bgaffpa[0];
    case 0x032: return g->ppu.bgaffpb[0];
    case 0x034: return g->ppu.bgaffpc[0];
    case 0x036: return g->ppu.bgaffpd[0];
    case 0x038: return g->ppu.bgaffpa[1];
    case 0x03A: return g->ppu.bgaffpb[1];
    case 0x03C: return g->ppu.bgaffpc[1];
    case 0x03E: return g->ppu.bgaffpd[1];
    case 0x040: return g->ppu.winh[0];
    case 0x042: return g->ppu.winh[1];
    case 0x044: return g->ppu.winv[0];
    case 0x046: return g->ppu.winv[1];
    case 0x048: return g->ppu.winin[0];
    case 0x04A: return g->ppu.winin[1];
    case 0x04C: return g->ppu.winout;
    case 0x050: return g->ppu.mosaic;
    case 0x052: return g->ppu.bldcnt;
    case 0x054: return g->ppu.blendalpha;
    case 0x056: return g->ppu.blendy;
    /* ---- sound ---- */
    case 0x060: return g->apu.nr10; case 0x062: return g->apu.nr11;
    case 0x064: return g->apu.nr12; case 0x066: return g->apu.nr13;
    case 0x068: return g->apu.nr14; case 0x06A: return g->apu.nr21;
    case 0x06C: return g->apu.nr22; case 0x06E: return g->apu.nr23;
    case 0x070: return g->apu.nr24; case 0x072: return g->apu.nr30;
    case 0x074: return g->apu.nr31; case 0x076: return g->apu.nr32;
    case 0x078: return g->apu.nr33; case 0x07A: return g->apu.nr34;
    case 0x07C: return g->apu.nr41; case 0x07E: return g->apu.nr42;
    case 0x080: return g->apu.nr43; case 0x082: return g->apu.nr44;
    case 0x084: return g->apu.nr50; case 0x086: return g->apu.nr51;
    case 0x088: return g->apu.nr52;
    case 0x090: { int i = (addr & 0xF) >> 1; return (uint16_t)(g->apu.wave[i*2] | (g->apu.wave[i*2+1] << 8)); }
    /* ---- DMA ---- */
    case 0x0B0: case 0x0C0: case 0x0D0: case 0x0E0: { int ch=(reg-0x0B0)>>4; return (uint16_t)g->dma[ch].src; }
    case 0x0B2: case 0x0C2: case 0x0D2: case 0x0E2: return 0;
    case 0x0B4: case 0x0C4: case 0x0D4: case 0x0E4: { int ch=(reg-0x0B4)>>4; return (uint16_t)g->dma[ch].dst; }
    case 0x0B6: case 0x0C6: case 0x0D6: case 0x0E6: return 0;
    case 0x0B8: case 0x0C8: case 0x0D8: case 0x0E8: { int ch=(reg-0x0B8)>>4; return (uint16_t)g->dma[ch].count; }
    case 0x0BA: case 0x0CA: case 0x0DA: case 0x0EA: {
        int ch = (reg - 0x0BA) >> 4;
        return (uint16_t)((g->dma[ch].src_ctl) | (g->dma[ch].dst_ctl << 2) |
               (g->dma[ch].word ? 0x0400 : 0) | (g->dma[ch].timing << 12) |
               (g->dma[ch].irq_enable ? 0x4000 : 0) | (g->dma[ch].enabled ? 0x8000 : 0));
    }
    /* ---- timers ---- */
    case 0x100: case 0x104: case 0x108: case 0x10C: { int t=(reg-0x100)>>2; return g->timer[t].counter; }
    case 0x102: case 0x106: case 0x10A: case 0x10E: { int t=(reg-0x102)>>2; return g->timer[t].reload; }
    /* ---- serial / keypad ---- */
    case 0x120: return g->io[0x120] | (g->io[0x121] << 8);
    case 0x122: return g->io[0x122] | (g->io[0x123] << 8);
    case 0x128: return g->io[0x128] | (g->io[0x129] << 8);   /* SIOCNT */
    case 0x130: return (uint16_t)(g->key_input & 0x3FF);      /* KEYINPUT */
    case 0x132: return g->io[0x132] | (g->io[0x133] << 8);    /* KEYCNT */
    /* ---- interrupts / waitstate ---- */
    case 0x200: return g->ie;                                 /* IE */
    case 0x202: return g->if_reg;                             /* IF */
    case 0x204: return g->io[0x204] | (g->io[0x205] << 8);    /* WAITCNT */
    case 0x208: return g->ime ? 1 : 0;                        /* IME */
    default:
        return (uint16_t)(g->io[reg] | (g->io[reg+1] << 8));
    }
}

/* =========================================================================
 * I/O writes (16-bit register space)
 * ========================================================================= */
void io_write16(GBA *g, uint32_t addr, uint16_t v) {
    uint32_t reg = addr & 0x3FE;
    switch (reg) {
    case 0x000: g->ppu.dispcnt = v; return;
    case 0x002: g->ppu.dispstat = (g->ppu.dispstat & 0x0007) | (v & 0xFFF8); return;
    case 0x004: return;                                   /* VCOUNT read-only */
    case 0x006: g->ppu.bgcnt[0] = v; return;
    case 0x008: g->ppu.bgcnt[1] = v; return;
    case 0x00A: g->ppu.bgcnt[2] = v; return;
    case 0x00C: g->ppu.bgcnt[3] = v; return;
    case 0x010: g->ppu.bghofs[0] = v & 0x1FF; return;
    case 0x012: g->ppu.bgvofs[0] = v & 0x1FF; return;
    case 0x014: g->ppu.bghofs[1] = v & 0x1FF; return;
    case 0x016: g->ppu.bgvofs[1] = v & 0x1FF; return;
    case 0x018: g->ppu.bghofs[2] = v & 0x1FF; return;
    case 0x01A: g->ppu.bgvofs[2] = v & 0x1FF; return;
    case 0x01C: g->ppu.bghofs[3] = v & 0x1FF; return;
    case 0x01E: g->ppu.bgvofs[3] = v & 0x1FF; return;
    case 0x020: g->ppu.bgaffx[0] = v & 0x1FF; return;
    case 0x022: g->ppu.bgaffy[0] = v & 0x1FF; return;
    case 0x024: g->ppu.bgaffx[1] = v & 0x1FF; return;
    case 0x026: g->ppu.bgaffy[1] = v & 0x1FF; return;
    case 0x030: g->ppu.bgaffpa[0] = (int16_t)v; return;
    case 0x032: g->ppu.bgaffpb[0] = (int16_t)v; return;
    case 0x034: g->ppu.bgaffpc[0] = (int16_t)v; return;
    case 0x036: g->ppu.bgaffpd[0] = (int16_t)v; return;
    case 0x038: g->ppu.bgaffpa[1] = (int16_t)v; return;
    case 0x03A: g->ppu.bgaffpb[1] = (int16_t)v; return;
    case 0x03C: g->ppu.bgaffpc[1] = (int16_t)v; return;
    case 0x03E: g->ppu.bgaffpd[1] = (int16_t)v; return;
    case 0x040: g->ppu.winh[0] = v & 0xFF; return;
    case 0x042: g->ppu.winh[1] = v & 0xFF; return;
    case 0x044: g->ppu.winv[0] = v & 0xFF; return;
    case 0x046: g->ppu.winv[1] = v & 0xFF; return;
    case 0x048: g->ppu.winin[0] = v & 0x3F; return;
    case 0x04A: g->ppu.winin[1] = v & 0x3F; return;
    case 0x04C: g->ppu.winout = v & 0x3F; return;
    case 0x050: g->ppu.mosaic = v; return;
    case 0x052: g->ppu.bldcnt = v; return;
    case 0x054: g->ppu.blendalpha = v; return;
    case 0x056: g->ppu.blendy = v; return;
    /* ---- sound ---- */
    case 0x060: g->apu.nr10 = v; return;
    case 0x062: g->apu.nr11 = v; return;
    case 0x064: g->apu.nr12 = v; return;
    case 0x066: g->apu.nr13 = v; return;
    case 0x068: g->apu.nr14 = v;
        if (v & 0x8000) { g->apu.ch1_on = true; g->apu.ch1_vol = (g->apu.nr12 >> 4) & 0xF; g->apu.ch1_dir = (g->apu.nr12 & 0x0800) ? true : false; }
        return;
    case 0x06A: g->apu.nr21 = v; return;
    case 0x06C: g->apu.nr22 = v; return;
    case 0x06E: g->apu.nr23 = v; return;
    case 0x070: g->apu.nr24 = v;
        if (v & 0x8000) { g->apu.ch2_on = true; g->apu.ch2_vol = (g->apu.nr22 >> 4) & 0xF; g->apu.ch2_dir = (g->apu.nr22 & 0x0800) ? true : false; }
        return;
    case 0x072: g->apu.nr30 = v; if (!(v & 0x80)) g->apu.ch3_on = false; return;
    case 0x074: g->apu.nr31 = v; return;
    case 0x076: g->apu.nr32 = v; return;
    case 0x078: g->apu.nr33 = v; return;
    case 0x07A: g->apu.nr34 = v; if (v & 0x8000) g->apu.ch3_on = true; return;
    case 0x07C: g->apu.nr41 = v; return;
    case 0x07E: g->apu.nr42 = v; return;
    case 0x080: g->apu.nr43 = v; return;
    case 0x082: g->apu.nr44 = v; if (v & 0x8000) { g->apu.ch4_on = true; g->apu.ch4_lfsr = 0x7FFF; } return;
    case 0x084: g->apu.nr50 = v; return;
    case 0x086: g->apu.nr51 = v; return;
    case 0x088: g->apu.nr52 = v; return;
    case 0x090: case 0x092: case 0x094: case 0x096: case 0x098: case 0x09A:
    case 0x09C: case 0x09E: {
        int i = (reg - 0x090) >> 1;
        g->apu.wave[i*2] = v & 0xFF; g->apu.wave[i*2+1] = v >> 8; return;
    }
    /* ---- DMA ---- */
    case 0x0B0: case 0x0C0: case 0x0D0: case 0x0E0: { int ch=(reg-0x0B0)>>4; g->dma[ch].src = (g->dma[ch].src & 0xFFFF0000) | v; return; }
    case 0x0B2: case 0x0C2: case 0x0D2: case 0x0E2: { int ch=(reg-0x0B2)>>4; g->dma[ch].src = (g->dma[ch].src & 0xFFFF) | ((uint32_t)v << 16); return; }
    case 0x0B4: case 0x0C4: case 0x0D4: case 0x0E4: { int ch=(reg-0x0B4)>>4; g->dma[ch].dst = (g->dma[ch].dst & 0xFFFF0000) | v; return; }
    case 0x0B6: case 0x0C6: case 0x0D6: case 0x0E6: { int ch=(reg-0x0B6)>>4; g->dma[ch].dst = (g->dma[ch].dst & 0xFFFF) | ((uint32_t)v << 16); return; }
    case 0x0B8: case 0x0C8: case 0x0D8: case 0x0E8: { int ch=(reg-0x0B8)>>4; g->dma[ch].count = v & 0xFFFF; return; }
    case 0x0BA: case 0x0CA: case 0x0DA: case 0x0EA: {
        int ch = (reg - 0x0BA) >> 4;
        DmaChan *d = &g->dma[ch];
        d->dst_ctl = (v >> 5) & 3;
        d->src_ctl = (v >> 7) & 3;
        d->repeat = (v >> 9) & 1;
        d->word = (v >> 10) & 1;
        d->timing = (v >> 12) & 3;
        d->irq_enable = (v >> 14) & 1;
        d->enabled = (v >> 15) & 1;
        if (d->enabled && d->timing == 0) dma_run(g, ch);   /* immediate */
        return;
    }
    /* ---- timers ---- */
    case 0x100: case 0x104: case 0x108: case 0x10C: { int t=(reg-0x100)>>2; g->timer[t].counter = v; return; }
    case 0x102: case 0x106: case 0x10A: case 0x10E: {
        int t = (reg - 0x102) >> 2;
        Timer *tm = &g->timer[t];
        tm->reload = v;
        tm->enabled = (v >> 7) & 1;
        tm->irq_enable = (v >> 6) & 1;
        tm->count_up = (v >> 2) & 1;
        int ps = v & 3;
        tm->prescaler = ps == 0 ? 1 : ps == 1 ? 64 : ps == 2 ? 256 : 1024;
        if (tm->enabled) tm->counter = tm->reload;
        return;
    }
    /* ---- serial / keypad ---- */
    case 0x120: case 0x122: case 0x128:
        g->io[reg] = v & 0xFF; g->io[reg+1] = v >> 8; return;
    case 0x130: return;                                   /* KEYINPUT read-only */
    case 0x132: g->io[0x132] = v & 0xFF; g->io[0x133] = v >> 8; return;
    /* ---- interrupts / waitstate ---- */
    case 0x200: g->ie = v & 0x3FFF; return;               /* IE  */
    case 0x202: g->if_reg &= ~(v & 0x3FFF); return;       /* IF: write 1 clears */
    case 0x204: g->io[0x204] = v & 0xFF; g->io[0x205] = v >> 8; return;  /* WAITCNT */
    case 0x208: g->ime = v & 1; return;                   /* IME */
    default:
        g->io[reg] = v & 0xFF; g->io[reg+1] = v >> 8; return;
    }
}

/* =========================================================================
 * DMA engine
 * ========================================================================= */
void dma_run(GBA *g, int ch) {
    DmaChan *d = &g->dma[ch];
    if (!d->enabled) return;
    uint32_t unit = d->word ? 4 : 2;
    uint32_t count = d->count & 0xFFFF;
    if (count == 0) count = (ch == 3) ? 0x10000 : 0x4000;
    uint32_t src = d->src, dst = d->dst;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t v = d->word ? gba_read32(g, src) : gba_read16(g, src);
        if (d->word) gba_write32(g, dst, v); else gba_write16(g, dst, (uint16_t)v);
        if (d->src_ctl == 0 || d->src_ctl == 1) src += unit;
        else if (d->src_ctl == 2) src -= unit;
        if (d->dst_ctl == 0 || d->dst_ctl == 1) dst += unit;
        else if (d->dst_ctl == 2) dst -= unit;
    }
    d->src = src;   /* advanced address is kept for repeat transfers */
    d->dst = dst;
    if (d->irq_enable) g->if_reg |= (IF_DMA0 << ch);
    if (!d->repeat) d->enabled = false;
}

/* =========================================================================
 * Timers
 * ========================================================================= */
void io_update_timers(GBA *g, uint32_t cycles) {
    for (int i = 0; i < 4; i++) {
        Timer *t = &g->timer[i];
        if (!t->enabled || t->count_up) continue;
        uint32_t inc = cycles / (uint32_t)t->prescaler;
        uint32_t cnt = t->counter + inc;
        if (cnt > 0xFFFF) {
            t->counter = (uint16_t)((cnt & 0xFFFF) + t->reload);
            if (t->irq_enable) g->if_reg |= (IF_TIMER0 << i);
            if (i < 3) g->timer[i+1].counter++;   /* cascade */
        } else t->counter = (uint16_t)cnt;
    }
}

void io_trigger_keypad(GBA *g, uint32_t cycles) {
    (void)cycles;
    uint16_t en = (uint16_t)(g->io[0x132] | (g->io[0x133] << 8));
    if (!(en & 0x4000)) return;                  /* IRQ disabled */
    uint16_t cnt = (uint16_t)(g->key_input & 0x3FF);
    uint16_t req = en & 0x3FF;
    bool and_mode = en & 0x8000;
    bool hit = and_mode ? ((cnt & req) == req) : ((cnt & req) != 0);
    if (hit) g->if_reg |= IF_KEYPAD;
}

/* =========================================================================
 * Memory map
 * ========================================================================= */
uint32_t gba_read32(GBA *g, uint32_t addr) {
    switch (addr >> 24) {
    case 0x00: return rd32_al(g->bios + (addr & 0x3FFC));
    case 0x02: return rd32_al(g->ewram + (addr & 0x3FFFC));
    case 0x03: return rd32_al(g->iwram + (addr & 0x7FFC));
    case 0x04: return io_read16(g, addr) | ((uint32_t)io_read16(g, addr + 2) << 16);
    case 0x05: return rd32_al(g->pal + (addr & 0x3FC));
    case 0x06: return rd32_al(g->vram + (addr & 0x17FFC));
    case 0x07: return rd32_al(g->oam + (addr & 0x3FC));
    case 0x08: case 0x09: case 0x0A: case 0x0B: case 0x0C: case 0x0D: {
        uint32_t off = (addr - 0x08000000) & (g->rom_mask & ~3u);
        return off < g->rom_size ? rd32_al(g->rom + off) : 0;
    }
    case 0x0E: return rd32_al(g->rom + (addr & 0xFFFF));
    default: return 0;
    }
}

uint16_t gba_read16(GBA *g, uint32_t addr) {
    switch (addr >> 24) {
    case 0x00: return rd16_al(g->bios + (addr & 0x3FFE));
    case 0x02: return rd16_al(g->ewram + (addr & 0x3FFFE));
    case 0x03: return rd16_al(g->iwram + (addr & 0x7FFE));
    case 0x04: return io_read16(g, addr);
    case 0x05: return rd16_al(g->pal + (addr & 0x3FE));
    case 0x06: return rd16_al(g->vram + (addr & 0x17FFE));
    case 0x07: return rd16_al(g->oam + (addr & 0x3FE));
    case 0x08: case 0x09: case 0x0A: case 0x0B: case 0x0C: case 0x0D: {
        uint32_t off = (addr - 0x08000000) & (g->rom_mask & ~1u);
        return off < g->rom_size ? rd16_al(g->rom + off) : 0;
    }
    case 0x0E: return rd16_al(g->rom + (addr & 0xFFFE));
    default: return 0;
    }
}

uint8_t gba_read8(GBA *g, uint32_t addr) {
    switch (addr >> 24) {
    case 0x00: return g->bios[addr & 0x3FFF];
    case 0x02: return g->ewram[addr & 0x3FFFF];
    case 0x03: return g->iwram[addr & 0x7FFF];
    case 0x04: { uint16_t v = io_read16(g, addr & ~1u); return (uint8_t)((addr & 1) ? (v >> 8) : v); }
    case 0x05: return g->pal[addr & 0x3FF];
    case 0x06: return g->vram[addr & 0x17FFF];
    case 0x07: return g->oam[addr & 0x3FF];
    case 0x08: case 0x09: case 0x0A: case 0x0B: case 0x0C: case 0x0D: {
        uint32_t off = (addr - 0x08000000) & g->rom_mask;
        return off < g->rom_size ? g->rom[off] : 0;
    }
    case 0x0E: return g->rom[addr & 0xFFFF];
    default: return 0;
    }
}

void gba_write32(GBA *g, uint32_t addr, uint32_t v) {
    switch (addr >> 24) {
    case 0x02: wr32_al(g->ewram + (addr & 0x3FFFC), v); return;
    case 0x03: wr32_al(g->iwram + (addr & 0x7FFC), v); return;
    case 0x04: io_write16(g, addr, v & 0xFFFF); io_write16(g, addr + 2, v >> 16); return;
    case 0x05: wr32_al(g->pal + (addr & 0x3FC), v); return;
    case 0x06: wr32_al(g->vram + (addr & 0x17FFC), v); return;
    case 0x07: wr32_al(g->oam + (addr & 0x3FC), v); return;
    default: return;   /* ROM write-protected */
    }
}

void gba_write16(GBA *g, uint32_t addr, uint16_t v) {
    switch (addr >> 24) {
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
    switch (addr >> 24) {
    case 0x02: g->ewram[addr & 0x3FFFF] = v; return;
    case 0x03: g->iwram[addr & 0x7FFF] = v; return;
    case 0x04: { uint16_t cur = io_read16(g, addr & ~1u);
                 if (addr & 1) cur = (uint16_t)((cur & 0x00FF) | (v << 8));
                 else cur = (uint16_t)((cur & 0xFF00) | v);
                 io_write16(g, addr & ~1u, cur); return; }
    case 0x05: g->pal[addr & 0x3FF] = v; return;
    case 0x06: g->vram[addr & 0x17FFF] = v; return;
    case 0x07: g->oam[addr & 0x3FF] = v; return;
    default: return;
    }
}
