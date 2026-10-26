/*
 * mem.c - GBA memory map and I/O register emulation.
 * Revisado contra GBATEK ("GBA I/O Map", "GBA Memory Map", "GBA Timers",
 * "GBA DMA Transfers", "GBA Unpredictable Things").
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

static int32_t sext28(uint32_t v) { return (int32_t)(v << 4) >> 4; }

/* DMA bus access costs, per GBATEK "GBA DMA Transfers / Transfer Rate/Timing"
 * (2N+2(n-1)S+xI). Internal memory (BIOS/IWRAM/IO/PAL/VRAM/OAM) is
 * waitstate-free; EWRAM keeps its reset 2 waitstates (4000800h = 0Dh) and the
 * GamePak ROM uses the WS0 timing from WAITCNT. A 32-bit unit on a 16-bit bus
 * is split into a non-sequential and a sequential 16-bit access. */
enum { R_BIOS, R_EWRAM, R_IWRAM, R_IO, R_PAL, R_VRAM, R_OAM, R_ROM, R_SRAM, R_MISC };

static int dma_region(uint32_t a) {
    if (a < 0x00004000u) return R_BIOS;
    if (a < 0x02000000u) return R_MISC;
    if (a < 0x02040000u) return R_EWRAM;
    if (a < 0x03000000u) return R_MISC;
    if (a < 0x03008000u) return R_IWRAM;
    if (a < 0x04000000u) return R_MISC;
    if (a < 0x04000400u) return R_IO;
    if (a < 0x05000000u) return R_MISC;
    if (a < 0x05000400u) return R_PAL;
    if (a < 0x06000000u) return R_MISC;
    if (a < 0x06018000u) return R_VRAM;
    if (a < 0x07000000u) return R_MISC;
    if (a < 0x07000400u) return R_OAM;
    if (a < 0x08000000u) return R_MISC;
    if (a < 0x0E000000u) return R_ROM;
    if (a < 0x0E010000u) return R_SRAM;
    return R_MISC;
}

static void dma_region_ws(GBA *g, int region, int *nseq16, int *seq16) {
    uint16_t wc = (uint16_t)(g->io[0x204] | (g->io[0x205] << 8));
    static const int rom_ws[4] = {4, 3, 2, 8};
    switch (region) {
    case R_EWRAM: *nseq16 = *seq16 = 2; break;
    case R_ROM:
        *nseq16 = rom_ws[wc & 3];
        *seq16  = (wc & 0x10) ? 1 : 2;
        break;
    case R_SRAM: *nseq16 = *seq16 = rom_ws[wc & 3]; break;
    default:     *nseq16 = *seq16 = 0; break;
    }
}

/* Cost of one DMA access to `region`: non-sequential for the first unit of a
 * transfer, sequential afterwards. 32-bit accesses on the 16-bit bus add the
 * split second access. The 2 internal cycles are charged once per unit, not
 * once per bus access. */
static int dma_access_cycles(GBA *g, int region, int word, int first) {
    int ns16, seq16;
    dma_region_ws(g, region, &ns16, &seq16);
    if (!word) return first ? ns16 : seq16;
    if (region == R_EWRAM) return 2 * seq16 + 1;
    if (region == R_ROM || region == R_SRAM)
        return first ? ns16 + 1 + seq16 : 2 * seq16 + 1;
    return 0;                                    /* internal 16-bit bus regions */
}

static uint32_t dma_total_cycles(GBA *g, uint32_t src, uint32_t dst, int word,
                                 uint32_t count) {
    if (!count) return 0;
    int rs = dma_region(src), rd = dma_region(dst);
    uint32_t first = 2 + (uint32_t)(dma_access_cycles(g, rs, word, 1)
                                  + dma_access_cycles(g, rd, word, 1));
    uint32_t rest  = 2 + (uint32_t)(dma_access_cycles(g, rs, word, 0)
                                  + dma_access_cycles(g, rd, word, 0));
    return first + (count - 1) * rest;
}

/* =========================================================================
 * Bus timing model (GBATEK "Memory Map" / "GamePak Prefetch")
 * ========================================================================= */

/* Region index = top nibble of the address, matching GBATEK's memory map. */
int bus_region(uint32_t a) {
    switch (a >> 24) {
    case 0x00: case 0x01: return 0x0;   /* BIOS */
    case 0x02:            return 0x2;   /* EWRAM */
    case 0x03:            return 0x3;   /* IWRAM */
    case 0x04:            return 0x4;   /* I/O */
    case 0x05:            return 0x5;   /* Palette */
    case 0x06:            return 0x6;   /* VRAM */
    case 0x07:            return 0x7;   /* OAM */
    case 0x08: case 0x09: return 0x8;   /* ROM WS0 */
    case 0x0A: case 0x0B: return 0xA;   /* ROM WS1 */
    case 0x0C: case 0x0D: return 0xC;   /* ROM WS2 */
    case 0x0E: case 0x0F: return 0xE;   /* SRAM/Flash */
    default:              return 0x0;
    }
}

/* Internal (on-chip) regions run at one cycle per access with no waitstates;
   EWRAM has its fixed 2-waitstate timing (GBATEK: 4000800h reset value 0Dh). */
static bool region_is_rom(int r) { return r == 0x8 || r == 0xA || r == 0xC; }

void bus_update_waitstates(GBA *g) {
    static const int rom_ns[4] = { 4, 3, 2, 8 };       /* SRAM/WS0/WS1/WS2 first */
    static const int rom_s[6]  = { 2, 1, 4, 1, 8, 1 }; /* WS0/1/2 sequential pairs */
    uint16_t wc = (uint16_t)(g->io[0x204] | (g->io[0x205] << 8));
    int i;

    for (i = 0; i < 16; i++) g->bus_ns16[i] = g->bus_s16[i] = 0;

    g->bus_ns16[0x0] = g->bus_s16[0x0] = 0;           /* BIOS: 1 cycle, no wait */
    g->bus_ns16[0x2] = g->bus_s16[0x2] = 2;           /* EWRAM */
    g->bus_ns16[0x3] = g->bus_s16[0x3] = 0;           /* IWRAM */
    g->bus_ns16[0x4] = g->bus_s16[0x4] = 0;           /* I/O */
    g->bus_ns16[0x5] = g->bus_s16[0x5] = 0;           /* Palette */
    g->bus_ns16[0x6] = g->bus_s16[0x6] = 0;           /* VRAM */
    g->bus_ns16[0x7] = g->bus_s16[0x7] = 0;           /* OAM */

    g->bus_ns16[0x8] = rom_ns[wc & 3];
    g->bus_s16[0x8]  = (wc & 0x10) ? 1 : 2;
    g->bus_ns16[0xA] = rom_ns[(wc >> 2) & 3];
    g->bus_s16[0xA]  = rom_s[2 + ((wc >> 4) & 1)];
    g->bus_ns16[0xC] = rom_ns[(wc >> 5) & 3];
    g->bus_s16[0xC]  = rom_s[4 + ((wc >> 7) & 1)];

    g->bus_ns16[0xE] = g->bus_s16[0xE] = rom_ns[wc & 3];   /* SRAM uses WS0 first */

    for (i = 0; i < 16; i++) {
        if (region_is_rom(i)) {
            /* 32-bit unit on the 16-bit bus: first 16 + 1 internal + seq 16 */
            g->bus_ns32[i] = g->bus_ns16[i] + 1 + g->bus_s16[i];
            g->bus_s32[i]  = 2 * g->bus_s16[i] + 1;
        } else {
            g->bus_ns32[i] = g->bus_ns16[i] + 1;
            g->bus_s32[i]  = g->bus_s16[i] + 1;
        }
    }
    g->prefetch_enable = (wc & 0x4000) != 0;
    g->prefetch_pc = 0xFFFFFFFFu;                 /* buffer empty */
}

/* Fetch cost of the instruction at `pc`.
 *
 * The GamePak prefetch buffer (GBATEK "GamePak Prefetch") only exists when
 * WAITCNT bit 14 is set; with it off every fetch is a plain single cycle, as
 * on-chip memory.  With it on the buffer keeps fetching ahead of the CPU, so
 * a fetch that follows on sequentially from the previous one is already
 * buffered (one internal cycle) while the first fetch after a branch misses
 * the buffer and pays the GamePak first-access (non-sequential) cost. */
int bus_fetch_cycles(GBA *g, uint32_t pc) {
    int r = bus_region(pc);
    int step = (g->cpu.cpsr & CPSR_T) ? 2 : 4;
    bool seq = (pc == g->prefetch_pc);

    g->prefetch_pc = pc + (uint32_t)step;
    if (r == 0x0 || !region_is_rom(r) || !g->prefetch_enable) return 1;
    if (seq) return 1;                             /* served from the buffer */
    return g->bus_ns16[r] + 1;                     /* branch: first access */
}

void bus_prefetch_branch(GBA *g) {
    g->prefetch_pc = 0xFFFFFFFFu;                  /* next fetch misses the buffer */
}

/* Cost of one data access (load or store) of `width` bytes at `addr`.
 *
 * Not wired up yet: a data access and the instruction fetch of the next
 * instruction overlap on real hardware, so this only becomes meaningful once
 * the whole pipeline (fetch of 32 bit + 8x16 prefetch buffer + the internal
 * 32-bit cycle) is modelled and the per-opcode cycle charges in arm.c are
 * replaced by it (GBATEK "Instruction Cycle Summary").  Left unused on
 * purpose until that port lands.
 */
#if 0
int bus_data_cycles(GBA *g, uint32_t addr, int width, bool store) {
    int r = bus_region(addr);
    int c;

    if (r == 0x0) c = (width == 4) ? 2 : 1;       /* BIOS */
    else if (width == 4) c = g->bus_ns32[r];
    else                 c = g->bus_ns16[r];
    c += 1;                                       /* +1 internal cycle */

    if (store && c > 1) c -= 1;                   /* writes are buffered */
    return c;
}
#endif

/* =========================================================================
 * I/O reads (16-bit register space)
 * ========================================================================= */
uint16_t io_read16(GBA *g, uint32_t addr) {
    uint32_t reg = addr & 0x3FE;
    Ppu *p = &g->ppu;

    /* ---- DMA: 12 bytes por canal (0x0B0, 0x0BC, 0x0C8, 0x0D4).
       GBATEK: SAD/DAD/CNT_L son de solo escritura; solo CNT_H se lee. ---- */
    if (reg >= 0x0B0 && reg <= 0x0DF) {
        int ch = (int)((reg - 0x0B0) / 12);
        if (((reg - 0x0B0) % 12) == 10) {
            DmaChan *d = &g->dma[ch];
            return (uint16_t)((d->dst_ctl << 5) | (d->src_ctl << 7) |
                   (d->repeat ? 0x0200 : 0) | (d->word ? 0x0400 : 0) |
                   (d->timing << 12) | (d->irq_enable ? 0x4000 : 0) |
                   (d->enabled ? 0x8000 : 0));
        }
        return 0;
    }
    /* ---- sonido 0x060-0x09F (PSG, control, wave RAM) ---- */
    if (reg >= 0x060 && reg <= 0x09F) return apu_read_reg(g, reg);

    switch (reg) {
    /* ---- display ---- */
    case 0x000: return p->dispcnt;
    case 0x004: {
        /* bits 0-2 son estado: VBlank (lineas 160..226, NO la 227), HBlank, VCount */
        uint16_t v = p->dispstat & 0xFFB8;
        if (p->vcount >= 160 && p->vcount != 227) v |= 0x0001;
        if (p->hblank) v |= 0x0002;
        if (p->vcount == ((p->dispstat >> 8) & 0xFF)) v |= 0x0004;
        return v;
    }
    case 0x006: return p->vcount;
    case 0x008: return p->bgcnt[0];
    case 0x00A: return p->bgcnt[1];
    case 0x00C: return p->bgcnt[2];
    case 0x00E: return p->bgcnt[3];
    /* BGxHOFS/VOFS, BG2/3 afines, WINxH/V, MOSAIC, BLDY: solo escritura */
    case 0x048: return p->winin;
    case 0x04A: return p->winout;
    case 0x050: return p->bldcnt;
    case 0x052: return p->bldalpha;
    /* ---- timers: 0x100+4n = TMnCNT_L (contador), 0x102+4n = TMnCNT_H (control) ---- */
    case 0x100: case 0x104: case 0x108: case 0x10C:
        return g->timer[(reg - 0x100) >> 2].counter;
    case 0x102: case 0x106: case 0x10A: case 0x10E: {
        Timer *t = &g->timer[(reg - 0x102) >> 2];
        int ps = t->prescaler == 64 ? 1 : t->prescaler == 256 ? 2 : t->prescaler == 1024 ? 3 : 0;
        return (uint16_t)(ps | (t->count_up ? 0x04 : 0) | (t->irq_enable ? 0x40 : 0) |
                          (t->enabled ? 0x80 : 0));
    }
    /* ---- serial / keypad ---- */
    case 0x130: return (uint16_t)(g->key_input & 0x3FF);      /* KEYINPUT (0 = pulsado) */
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
    Ppu *p = &g->ppu;

    /* ---- DMA ---- */
    if (reg >= 0x0B0 && reg <= 0x0DF) {
        int ch = (int)((reg - 0x0B0) / 12);
        DmaChan *d = &g->dma[ch];
        switch ((reg - 0x0B0) % 12) {
        case 0: d->src_latch = (d->src_latch & 0xFFFF0000u) | v; return;
        case 2: d->src_latch = (d->src_latch & 0x0000FFFFu) | ((uint32_t)v << 16); return;
        case 4: d->dst_latch = (d->dst_latch & 0xFFFF0000u) | v; return;
        case 6: d->dst_latch = (d->dst_latch & 0x0000FFFFu) | ((uint32_t)v << 16); return;
        case 8: d->count = v; return;       /* se enmascara a 14/16 bit al disparar */
        case 10: {
            bool was = d->enabled;
            d->dst_ctl = (v >> 5) & 3;
            d->src_ctl = (v >> 7) & 3;
            d->repeat = (v >> 9) & 1;
            d->word = (v >> 10) & 1;
            d->timing = (v >> 12) & 3;
            d->irq_enable = (v >> 14) & 1;
            d->enabled = (v >> 15) & 1;
            /* GBATEK: los registros internos se recargan solo al pasar enable 0 -> 1.
               DMA0: SAD/DAD de 27 bit; DMA1/2: SAD 28 bit, DAD 27 bit; DMA3: ambos 28. */
            if (d->enabled && !was) {
                d->src = d->src_latch & (ch == 0 ? 0x07FFFFFFu : 0x0FFFFFFFu);
                d->dst = d->dst_latch & (ch == 3 ? 0x0FFFFFFFu : 0x07FFFFFFu);
                if (d->timing == 0) dma_run(g, ch);                 /* inmediato */
            }
            return;
        }
        }
        return;
    }
    /* ---- sonido 0x060-0x09F ---- */
    if (reg >= 0x060 && reg <= 0x09F) { apu_write_reg(g, reg, v); return; }

    switch (reg) {
    case 0x000: p->dispcnt = v; return;
    case 0x004: p->dispstat = v & 0xFFB8; return;          /* bits 0-2 solo lectura */
    case 0x006: return;                                    /* VCOUNT read-only */
    case 0x008: p->bgcnt[0] = v; return;
    case 0x00A: p->bgcnt[1] = v; return;
    case 0x00C: p->bgcnt[2] = v; return;
    case 0x00E: p->bgcnt[3] = v; return;
    case 0x010: p->bghofs[0] = v & 0x1FF; return;
    case 0x012: p->bgvofs[0] = v & 0x1FF; return;
    case 0x014: p->bghofs[1] = v & 0x1FF; return;
    case 0x016: p->bgvofs[1] = v & 0x1FF; return;
    case 0x018: p->bghofs[2] = v & 0x1FF; return;
    case 0x01A: p->bgvofs[2] = v & 0x1FF; return;
    case 0x01C: p->bghofs[3] = v & 0x1FF; return;
    case 0x01E: p->bgvofs[3] = v & 0x1FF; return;
    /* BG2: PA 0x20, PB 0x22, PC 0x24, PD 0x26, X 0x28/0x2A, Y 0x2C/0x2E */
    case 0x020: p->pa[0] = (int16_t)v; return;
    case 0x022: p->pb[0] = (int16_t)v; return;
    case 0x024: p->pc[0] = (int16_t)v; return;
    case 0x026: p->pd[0] = (int16_t)v; return;
    case 0x028: p->bgx[0] = sext28(((uint32_t)p->bgx[0] & 0xFFFF0000u) | v); p->refx[0] = p->bgx[0]; return;
    case 0x02A: p->bgx[0] = sext28(((uint32_t)p->bgx[0] & 0xFFFFu) | ((uint32_t)(v & 0x0FFF) << 16)); p->refx[0] = p->bgx[0]; return;
    case 0x02C: p->bgy[0] = sext28(((uint32_t)p->bgy[0] & 0xFFFF0000u) | v); p->refy[0] = p->bgy[0]; return;
    case 0x02E: p->bgy[0] = sext28(((uint32_t)p->bgy[0] & 0xFFFFu) | ((uint32_t)(v & 0x0FFF) << 16)); p->refy[0] = p->bgy[0]; return;
    /* BG3: PA 0x30, PB 0x32, PC 0x34, PD 0x36, X 0x38/0x3A, Y 0x3C/0x3E */
    case 0x030: p->pa[1] = (int16_t)v; return;
    case 0x032: p->pb[1] = (int16_t)v; return;
    case 0x034: p->pc[1] = (int16_t)v; return;
    case 0x036: p->pd[1] = (int16_t)v; return;
    case 0x038: p->bgx[1] = sext28(((uint32_t)p->bgx[1] & 0xFFFF0000u) | v); p->refx[1] = p->bgx[1]; return;
    case 0x03A: p->bgx[1] = sext28(((uint32_t)p->bgx[1] & 0xFFFFu) | ((uint32_t)(v & 0x0FFF) << 16)); p->refx[1] = p->bgx[1]; return;
    case 0x03C: p->bgy[1] = sext28(((uint32_t)p->bgy[1] & 0xFFFF0000u) | v); p->refy[1] = p->bgy[1]; return;
    case 0x03E: p->bgy[1] = sext28(((uint32_t)p->bgy[1] & 0xFFFFu) | ((uint32_t)(v & 0x0FFF) << 16)); p->refy[1] = p->bgy[1]; return;
    /* ventanas / mosaic / blend (GBATEK: WIN0H 0x40, WIN1H 0x42, WIN0V 0x44,
       WIN1V 0x46, WININ 0x48, WINOUT 0x4A, MOSAIC 0x4C, BLDCNT 0x50) */
    case 0x040: case 0x042: {   /* WIN0H/WIN1H: X2>240 o X1>X2 => X2=240 (GBATEK) */
        int w = (reg == 0x040) ? 0 : 1;
        uint8_t x2 = v & 0xFF, x1 = (v >> 8) & 0xFF;
        if (x2 > 240 || x1 > x2) x2 = 240;
        p->winh[w] = (uint16_t)((x1 << 8) | x2);
        return;
    }
    case 0x044: case 0x046: {   /* WIN0V/WIN1V: Y2>160 o Y1>Y2 => Y2=160 (GBATEK) */
        int w = (reg == 0x044) ? 0 : 1;
        uint8_t y2 = v & 0xFF, y1 = (v >> 8) & 0xFF;
        if (y2 > 160 || y1 > y2) y2 = 160;
        p->winv[w] = (uint16_t)((y1 << 8) | y2);
        return;
    }
    case 0x048: p->winin = v & 0x3F3F; return;
    case 0x04A: p->winout = v & 0x3F3F; return;
    case 0x04C: p->mosaic = v; return;
    case 0x050: p->bldcnt = v & 0x3FFF; return;
    case 0x052: p->bldalpha = v & 0x1F1F; return;
    case 0x054: p->bldy = v & 0x1F; return;
    /* ---- FIFO de Direct Sound: cada halfword aporta 2 muestras (byte bajo primero) ---- */
    case 0x0A0: case 0x0A2: { uint8_t b[2] = { (uint8_t)v, (uint8_t)(v >> 8) }; apu_fifo_write(g, 0, b, 2); return; }
    case 0x0A4: case 0x0A6: { uint8_t b[2] = { (uint8_t)v, (uint8_t)(v >> 8) }; apu_fifo_write(g, 1, b, 2); return; }
    /* ---- timers: TMnCNT_L = valor de recarga; TMnCNT_H = control ---- */
    case 0x100: case 0x104: case 0x108: case 0x10C:
        g->timer[(reg - 0x100) >> 2].reload = v; return;
    case 0x102: case 0x106: case 0x10A: case 0x10E: {
        Timer *tm = &g->timer[(reg - 0x102) >> 2];
        bool was = tm->enabled;
        static const int ps[4] = { 1, 64, 256, 1024 };
        tm->prescaler = ps[v & 3];
        tm->count_up = (v >> 2) & 1;
        tm->irq_enable = (v >> 6) & 1;
        tm->enabled = (v >> 7) & 1;
        if (tm->enabled && !was) { tm->counter = tm->reload; tm->clock = 0; }
        return;
    }
    /* ---- serial / keypad ---- */
    case 0x120: case 0x122: case 0x128:
        g->io[reg] = v & 0xFF; g->io[reg+1] = v >> 8; return;
    case 0x130: return;                                   /* KEYINPUT read-only */
    case 0x132: g->io[0x132] = v & 0xFF; g->io[0x133] = v >> 8; return;
    /* ---- interrupts / waitstate ---- */
    case 0x200: g->ie = v & 0x3FFF; return;               /* IE  */
    case 0x202: g->if_reg &= (uint16_t)~(v & 0x3FFF); return;   /* IF: write 1 clears */
    case 0x204: g->io[0x204] = v & 0xFF; g->io[0x205] = (v >> 8) & 0x7F;
                bus_update_waitstates(g); return;  /* WAITCNT */
    case 0x208: g->ime = v & 1; return;                   /* IME */
    case 0x300:                                           /* POSTFLG (byte bajo) + HALTCNT (byte alto) */
        g->io[0x300] = v & 0xFF;
        g->io[0x301] = v >> 8;
        g->cst.halt = true;
        g->cst.wait_mask = 0;   /* direct HALTCNT halt wakes on any IRQ */
        if (v & 0x8000) g->cst.stopped = true;
        return;
    default:
        g->io[reg] = v & 0xFF; g->io[reg+1] = v >> 8; return;
    }
}

/* =========================================================================
 * DMA engine (GBATEK: GBA DMA Transfers)
 * ========================================================================= */
void dma_run(GBA *g, int ch) {
    DmaChan *d = &g->dma[ch];
    if (!d->enabled) return;

    /* Timing 3 en DMA1/2 = modo FIFO de sonido: siempre 4 palabras de 32 bit,
       destino fijo, el contador DMAxCNT_L se ignora. */
    bool fifo = (d->timing == 3) && (ch == 1 || ch == 2);
    uint32_t unit = (d->word || fifo) ? 4 : 2;
    uint32_t count;
    if (fifo) count = 4;
    else if (ch == 3) { count = d->count & 0xFFFF; if (!count) count = 0x10000; }
    else              { count = d->count & 0x3FFF; if (!count) count = 0x4000; }

    uint32_t src = d->src & ~(unit - 1);
    uint32_t dst = d->dst & ~(unit - 1);

    /* EEPROM es bit-serie y solo accesible por DMA de 16 bit: el recuento le dice
       en que fase del protocolo esta. */
    if (g->save_type == SAVE_EEPROM && !fifo && !d->word &&
        (eeprom_addr_in_range(g, dst) || eeprom_addr_in_range(g, src)))
        eeprom_dma_begin(g, count);

    /* GBATEK (DMA Transfers, "Transfer Rate/Timing"): the transfer takes
       2N+2(n-1)S+xI cycles. Timers keep counting while the DMA holds the bus,
       so those cycles are charged to the timers that were already running when
       the transfer began (a DMA that itself enables a timer must not bill it). */
    uint32_t dma_cyc = dma_total_cycles(g, d->src, d->dst, unit == 4, count);
    io_update_timers(g, dma_cyc);

    for (uint32_t i = 0; i < count; i++) {
        if (unit == 4) gba_write32(g, dst, gba_read32(g, src));
        else           gba_write16(g, dst, gba_read16(g, src));
        /* src: 0 incrementa, 1 decrementa, 2 fijo, 3 prohibido (se trata como incremento) */
        if (d->src_ctl == 1) src -= unit;
        else if (d->src_ctl != 2) src += unit;
        /* dst: 0 incrementa, 1 decrementa, 2 fijo, 3 incrementa + recarga al repetir */
        if (!fifo) {
            if (d->dst_ctl == 1) dst -= unit;
            else if (d->dst_ctl != 2) dst += unit;
        }
    }
    d->src = src;
    d->dst = dst;
    gba_add_cycles(g, dma_cyc);
    if (d->irq_enable) g->if_reg |= (uint16_t)(IF_DMA0 << ch);

    if (d->repeat && d->timing != 0) {
        if (d->dst_ctl == 3 && !fifo)
            d->dst = d->dst_latch & (ch == 3 ? 0x0FFFFFFFu : 0x07FFFFFFu);
    } else {
        d->enabled = false;
    }
}

void dma_trigger(GBA *g, int timing) {
    for (int ch = 0; ch < 4; ch++) {
        DmaChan *d = &g->dma[ch];
        if (d->enabled && d->timing == timing) dma_run(g, ch);
    }
}

/* El FIFO A/B pide datos (<= 16 bytes en cola): dispara el DMA1/DMA2 cuyo destino sea el FIFO. */
void dma_fifo_request(GBA *g, int fifo) {
    uint32_t target = fifo ? 0x040000A4u : 0x040000A0u;
    for (int ch = 1; ch <= 2; ch++) {
        DmaChan *d = &g->dma[ch];
        if (d->enabled && d->timing == 3 && (d->dst & 0x0FFFFFFFu) == target) {
            dma_run(g, ch);
            return;
        }
    }
}

/* =========================================================================
 * Timers (GBATEK: GBA Timers)
 * ========================================================================= */
static void timer_add(GBA *g, int i, uint32_t ticks);

static void timer_overflow(GBA *g, int i, uint32_t n) {
    Timer *t = &g->timer[i];
    if (t->irq_enable) g->if_reg |= (uint16_t)(IF_TIMER0 << i);
    if (i < 2) apu_timer_overflow(g, i, n);          /* reloj de los FIFOs de Direct Sound */
    if (i < 3) {                                     /* cascada: solo si el siguiente esta en count-up */
        Timer *nt = &g->timer[i + 1];
        if (nt->enabled && nt->count_up) timer_add(g, i + 1, n);
    }
}

static void timer_add(GBA *g, int i, uint32_t ticks) {
    Timer *t = &g->timer[i];
    uint32_t cnt = (uint32_t)t->counter + ticks;
    if (cnt <= 0xFFFF) { t->counter = (uint16_t)cnt; return; }
    uint32_t period = 0x10000u - t->reload;          /* 1 .. 0x10000 */
    uint32_t over = cnt - 0x10000u;
    uint32_t n = 1 + over / period;                  /* desbordamientos en este paso */
    t->counter = (uint16_t)(t->reload + over % period);
    timer_overflow(g, i, n);
}

void io_update_timers(GBA *g, uint32_t cycles) {
    for (int i = 0; i < 4; i++) {
        Timer *t = &g->timer[i];
        if (!t->enabled) continue;
        if (i > 0 && t->count_up) continue;          /* avanza solo por cascada */
        uint32_t ps = t->prescaler ? (uint32_t)t->prescaler : 1u;
        t->clock += cycles;                          /* acumulador: no se pierde el resto */
        uint32_t ticks = t->clock / ps;
        t->clock -= ticks * ps;
        if (ticks) timer_add(g, i, ticks);
    }
}

/* KEYCNT: bit 14 = IRQ, bit 15 = 0 OR / 1 AND, bits 0-9 = teclas elegidas.
   key_input es activo en bajo, asi que se invierte para trabajar con "pulsadas". */
void io_trigger_keypad(GBA *g, uint32_t cycles) {
    (void)cycles;
    uint16_t en = (uint16_t)(g->io[0x132] | (g->io[0x133] << 8));
    if (!(en & 0x4000)) return;
    uint16_t pressed = (uint16_t)(~g->key_input & 0x3FF);
    uint16_t req = en & 0x3FF;
    bool and_mode = (en & 0x8000) != 0;
    bool hit = and_mode ? ((pressed & req) == req) : ((pressed & req) != 0);
    if (hit) g->if_reg |= IF_KEYPAD;
}

/* =========================================================================
 * Memory map
 * ========================================================================= */

/* Bus abierto del cartucho: fuera de la ROM se lee (direccion/2) & 0xFFFF (GBATEK). */
static uint16_t rom_open16(uint32_t addr) { return (uint16_t)((addr >> 1) & 0xFFFF); }

static uint16_t rom_read16(GBA *g, uint32_t addr) {
    uint32_t off = addr & 0x01FFFFFEu;
    return off + 1 < g->rom_size ? rd16_al(g->rom + off) : rom_open16(addr);
}

/* SRAM / Flash viven en un bus de 8 bit: las lecturas de 16/32 bit repiten el byte. */
static uint8_t backup_read8(GBA *g, uint32_t addr) {
    if (flash_in_use(g)) return flash_read(g, addr);
    return g->save_mem[addr & 0x7FFF];
}
static void backup_write8(GBA *g, uint32_t addr, uint8_t v) {
    if (flash_in_use(g)) { flash_write(g, addr, v); return; }
    if (g->save_type == SAVE_NONE) gba_set_savetype(g, SAVE_SRAM);   /* ROM sin etiqueta: asumir SRAM */
    g->save_mem[addr & 0x7FFF] = v;
    g->save_dirty = true;
}

uint32_t gba_read32(GBA *g, uint32_t addr) {
    switch (addr >> 24) {
    case 0x00:
        if (g->bios_last_pc == 0xFFFFFFFFu) return g->bios_prefetch;
        return rd32_al(g->bios + (addr & 0x3FFC));
    case 0x02: return rd32_al(g->ewram + (addr & 0x3FFFC));
    case 0x03: return rd32_al(g->iwram + (addr & 0x7FFC));
    case 0x04: return io_read16(g, addr & ~3u) | ((uint32_t)io_read16(g, (addr & ~3u) + 2) << 16);
    case 0x05: return rd32_al(g->pal + (addr & 0x3FC));
    case 0x06: { uint32_t o = addr & 0x1FFFC; if (o >= 0x18000) o -= 0x8000; return rd32_al(g->vram + o); }
    case 0x07: return rd32_al(g->oam + (addr & 0x3FC));
    case 0x08: case 0x09: case 0x0A: case 0x0B: case 0x0C: case 0x0D: {
        if (eeprom_addr_in_range(g, addr)) return 0;  /* EEPROM: bit-serie, solo por DMA de 16 bit */
        uint32_t a = addr & ~3u;
        return (uint32_t)rom_read16(g, a) | ((uint32_t)rom_read16(g, a + 2) << 16);
    }
    case 0x0E: case 0x0F: return (uint32_t)backup_read8(g, addr) * 0x01010101u;
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
    case 0x06: { uint32_t o = addr & 0x1FFFE; if (o >= 0x18000) o -= 0x8000; return rd16_al(g->vram + o); }
    case 0x07: return rd16_al(g->oam + (addr & 0x3FE));
    case 0x08: case 0x09: case 0x0A: case 0x0B: case 0x0C: case 0x0D:
        if (eeprom_addr_in_range(g, addr)) return (uint16_t)(eeprom_read_bit(g) & 1);
        return rom_read16(g, addr & ~1u);
    case 0x0E: case 0x0F: return (uint16_t)(backup_read8(g, addr) * 0x0101u);
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
    case 0x06: { uint32_t o = addr & 0x1FFFF; if (o >= 0x18000) o -= 0x8000; return g->vram[o]; }
    case 0x07: return g->oam[addr & 0x3FF];
    case 0x08: case 0x09: case 0x0A: case 0x0B: case 0x0C: case 0x0D: {
        if (eeprom_addr_in_range(g, addr))
            return (addr & 1) ? 0 : eeprom_read_bit(g);   /* bit 0 of each halfword */
        uint16_t h = rom_read16(g, addr & ~1u);
        return (uint8_t)((addr & 1) ? (h >> 8) : h);
    }
    case 0x0E: case 0x0F: return backup_read8(g, addr);
    default: return 0;
    }
}

void gba_write32(GBA *g, uint32_t addr, uint32_t v) {
    switch (addr >> 24) {
    case 0x02: wr32_al(g->ewram + (addr & 0x3FFFC), v); return;
    case 0x03: wr32_al(g->iwram + (addr & 0x7FFC), v); return;
    case 0x04: io_write16(g, addr & ~3u, v & 0xFFFF); io_write16(g, (addr & ~3u) + 2, v >> 16); return;
    case 0x05: wr32_al(g->pal + (addr & 0x3FC), v); return;
    case 0x06: { uint32_t o = addr & 0x1FFFC; if (o >= 0x18000) o -= 0x8000; wr32_al(g->vram + o, v); return; }
    case 0x07: wr32_al(g->oam + (addr & 0x3FC), v); return;
    case 0x0D: return;   /* EEPROM solo por DMA de 16 bit; el resto es ROM */
    case 0x0E: case 0x0F:
        /* bus de 8 bit: se escribe un solo byte, el que corresponde a la linea de direccion */
        backup_write8(g, addr, (uint8_t)(v >> ((addr & 3) * 8))); return;
    default: return;   /* ROM write-protected */
    }
}

void gba_write16(GBA *g, uint32_t addr, uint16_t v) {
    switch (addr >> 24) {
    case 0x02: wr16_al(g->ewram + (addr & 0x3FFFE), v); return;
    case 0x03: wr16_al(g->iwram + (addr & 0x7FFE), v); return;
    case 0x04: io_write16(g, addr, v); return;
    case 0x05: wr16_al(g->pal + (addr & 0x3FE), v); return;
    case 0x06: { uint32_t o = addr & 0x1FFFE; if (o >= 0x18000) o -= 0x8000; wr16_al(g->vram + o, v); return; }
    case 0x07: wr16_al(g->oam + (addr & 0x3FE), v); return;
    case 0x0D:
        if (eeprom_addr_in_range(g, addr)) {   /* un bit de protocolo por halfword */
            eeprom_write_bit(g, (uint8_t)(v & 1));
            if (g->eeprom.dirty) { g->save_dirty = true; g->eeprom.dirty = false; }
        }
        return;
    case 0x0E: case 0x0F:
        backup_write8(g, addr, (uint8_t)(v >> ((addr & 1) * 8))); return;
    default: return;
    }
}

/* Escritura de 8 bit en registros de I/O. Los registros de 16 bit se componen con
   lectura-modificacion-escritura, salvo los que tienen efectos al escribir. */
static void io_write8(GBA *g, uint32_t addr, uint8_t v) {
    uint32_t reg = addr & 0x3FF;

    if (reg == 0x300) { g->io[0x300] = v; return; }                 /* POSTFLG */
    if (reg == 0x301) {                                             /* HALTCNT */
        g->io[0x301] = v;
        g->cst.halt = true;
        g->cst.wait_mask = 0;
        if (v & 0x80) g->cst.stopped = true;
        return;
    }
    if (reg >= 0x0A0 && reg <= 0x0A7) {                             /* FIFO A/B */
        apu_fifo_write(g, reg >= 0x0A4, &v, 1);
        return;
    }
    if (reg == 0x202 || reg == 0x203) {                             /* IF: write-1-to-clear, por byte */
        g->if_reg &= (uint16_t)~((uint16_t)v << ((reg & 1) * 8));
        return;
    }
    if (reg >= 0x100 && reg <= 0x10F && !(reg & 2)) {               /* TMnCNT_L: recarga, no contador */
        Timer *tm = &g->timer[(reg - 0x100) >> 2];
        if (reg & 1) tm->reload = (uint16_t)((tm->reload & 0x00FF) | (v << 8));
        else         tm->reload = (uint16_t)((tm->reload & 0xFF00) | v);
        return;
    }
    if (reg >= 0x0B0 && reg <= 0x0DF) {                             /* DMA: SAD/DAD/CNT_L son solo escritura */
        int ch = (int)((reg - 0x0B0) / 12);
        uint32_t off = (reg - 0x0B0) % 12;
        DmaChan *d = &g->dma[ch];
        if (off < 4)      { uint32_t s = off * 8;       d->src_latch = (d->src_latch & ~(0xFFu << s)) | ((uint32_t)v << s); return; }
        else if (off < 8) { uint32_t s = (off - 4) * 8; d->dst_latch = (d->dst_latch & ~(0xFFu << s)) | ((uint32_t)v << s); return; }
        else if (off < 10){ uint32_t s = (off - 8) * 8; d->count = (d->count & ~(0xFFu << s)) | ((uint32_t)v << s); return; }
        /* CNT_H: lectura-modificacion-escritura normal */
    }
    uint16_t cur = io_read16(g, reg & ~1u);
    if (reg & 1) cur = (uint16_t)((cur & 0x00FF) | (v << 8));
    else         cur = (uint16_t)((cur & 0xFF00) | v);
    io_write16(g, reg & ~1u, cur);
}

void gba_write8(GBA *g, uint32_t addr, uint8_t v) {
    switch (addr >> 24) {
    case 0x02: g->ewram[addr & 0x3FFFF] = v; return;
    case 0x03: g->iwram[addr & 0x7FFF] = v; return;
    case 0x04: io_write8(g, addr, v); return;
    case 0x05:
        /* GBATEK: un byte escrito en la paleta se duplica en ambas mitades del halfword */
        g->pal[addr & 0x3FE] = v; g->pal[(addr & 0x3FE) | 1] = v; return;
    case 0x06: {
        uint32_t o = addr & 0x1FFFF; if (o >= 0x18000) o -= 0x8000;
        /* VRAM: en el area de BG (hasta 0x10000 en modos 0-2, 0x14000 en 3-5) el byte se
           duplica; en el area de OBJ la escritura de 8 bit se ignora. */
        uint32_t bg_end = (g->ppu.dispcnt & 7) >= 3 ? 0x14000u : 0x10000u;
        if (o < bg_end) { g->vram[o & ~1u] = v; g->vram[o | 1u] = v; }
        return;
    }
    case 0x07: return;                       /* OAM: escrituras de 8 bit ignoradas */
    case 0x0D: return;
    case 0x0E: case 0x0F: backup_write8(g, addr, v); return;
    default: return;
    }
}
