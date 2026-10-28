/*
 * bios.c - GBA BIOS SWI handlers.
 * Clean-room implementation of the classic GBA BIOS calls used to boot games.
 */
#include "gba.h"
#include "mem.h"
#include "arm.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static inline bool thumb_mode_p(GBA *g) { return (g->cpu.cpsr & CPSR_T) != 0; }

/* Fixed-point trig used by the affine-set routines.  The GBA stores angles
 * as 8-bit values (bit 7 = sign of sin, low 7 bits = 0..180/128).  We only
 * ever need cos(a) and sin(a) for such angles, so precompute the 256-entry
 * cosine table in 1.15 fixed point (SCALE = 32768 = 1.0). */
#define AFF_SCALE 32768
static int32_t s_cos_tbl[256]; /* [angle] in 0..255 steps of 180/128; lazy fill */
static int32_t s_cos_ready = 0;
static void aff_ensure_cos(void) {
    if (s_cos_ready) return;
    for (int i = 0; i < 256; i++) {
        double a = (double)i / 128.0 * 3.14159265358979323846;
        s_cos_tbl[i] = (int32_t)(cos(a) * AFF_SCALE);
    }
    s_cos_ready = 1;
}
/* GBA angle byte -> (cos, sin) in 1.15 fixed point. */
static void aff_angle(int angle, int32_t *co, int32_t *si) {
    aff_ensure_cos();
    int idx = angle & 0xFF;
    int32_t c = s_cos_tbl[idx];
    int32_t s = s_cos_tbl[(idx + 64) & 0xFF];
    if (angle < 0) { s = -s; }  /* negative angle flips the sine */
    *co = c; *si = s;
}
static inline int32_t aff_sat16(int32_t v) {
    if (v > 32767) return 32767;
    if (v < -32768) return -32768;
    return v;
}
static inline int32_t aff_sat32(int32_t v) {
    /* rx/ry are stored as (rx * 256) in the dest per GBATEK; keep within s32 */
    return v;
}

static void swi_soft_reset(GBA *g) {
    /* SWI 0x00: restart from the BIOS entry (0x00000000) or the EWRAM entry
       depending on the boot flag at 0x03007FFA. Mirrors the real BIOS: the
       BIOS scratch area is cleared, the SYS/IRQ/SVC stack pointers are
       restored, and the CPU resumes in SYS mode with IRQs enabled. */
    uint32_t boot_target = (g->iwram[0x7FFA] != 0) ? 0x02000000 : 0x08000000;

    memset(g->iwram + 0x7E00, 0, 0x200);
    g->cpu.bank[BANK_IRQ][5] = 0x03007FA0;
    g->cpu.bank[BANK_SVC][5] = 0x03007FE0;
    g->cpu.bank[0][5] = 0x03007F00;
    g->cpu.r[13] = 0x03007F00;
    g->cpu.cpsr = 0x0000001F;            /* system mode, ARM, IRQ enabled */
    g->cpu.r[15] = boot_target;
}

static void swi_register_ram_reset(GBA *g) {
    /* SWI 0x01: clear the memory regions selected by r0 and force the display
       into forced blank. Mirrors the hardware behaviour that games rely on to
       reinitialise their own shadow of the video registers. */
    uint32_t flags = g->cpu.r[0];

    g->ppu.dispcnt = 0x0080;
    g->io[0x00] = 0x80; g->io[0x01] = 0x00;

    if (flags & 0x01) memset(g->ewram, 0, EWRAM_SIZE);
    if (flags & 0x02) memset(g->iwram, 0, IWRAM_SIZE - 0x200);
    if (flags & 0x04) memset(g->pal, 0, PAL_SIZE);
    if (flags & 0x08) memset(g->vram, 0, VRAM_SIZE);
    if (flags & 0x10) memset(g->oam, 0, OAM_SIZE);

    if (flags & 0x20) {
        /* SIO registers 0x120-0x12F */
        for (int off = 0x120; off < 0x130; off += 2) { g->io[off] = 0; g->io[off+1] = 0; }
    }
    if (flags & 0x40) {
        /* Sound registers 0x060-0x0AF, SOUNDBIAS keeps its 0x200 default */
        memset(g->io + 0x60, 0, 0xB0 - 0x60);
        g->io[0x88] = 0x00; g->io[0x89] = 0x02;
    }
    if (flags & 0x80) {
        memset(g->io + 0x00, 0, 0x60);
        g->io[0x00] = 0x80; g->io[0x01] = 0x00;   /* DISPCNT: forced blank */
        memset(g->io + 0xB0, 0, 0xE0 - 0xB0);     /* DMA */
        memset(g->io + 0x100, 0, 0x10);           /* timers */
        memset(g->io + 0x130, 0, 4);              /* keypad */
        /* mGBA clears DISPSTAT/VCOUNT along with the video registers. The
           PPU keeps its own copy of DISPSTAT, so it has to be reset here too
           or the VBlank/HBlank/VCount IRQ enables survive the reset. */
        g->ppu.dispstat = 0;
        g->ppu.vcount = 0;
        g->ie = 0;
        /* mGBA writes 0xFFFF to IF, and IF is write-1-to-clear, so the net
           effect is that every pending flag is dropped. */
        g->if_reg = 0;
        g->io[0x202] = 0; g->io[0x203] = 0;
        g->io[0x204] = 0; g->io[0x205] = 0;       /* WAITCNT */
        g->ime = false;
        g->io[0x208] = 0; g->io[0x209] = 0;
    }
}
static void swi_halt(GBA *g) {
    g->cst.halt = true;
    g->cst.wait_mask = 0;       /* plain Halt: any unmasked IRQ wakes */
}
static void swi_stop(GBA *g) {
    g->cst.halt = true;
    g->cst.stopped = true;
    g->cst.wait_mask = 0;
}
void swi_intr_wait(GBA *g) {
    /* SWI 0x04: IntrWait(clear, irq).  r0 bit0 = clear flags first,
       r1 = mask of IRQs to wait for (only the enable bits are usable).
       If clear is set, any IRQ already pending is discarded and the CPU
       waits for a new one; otherwise a matching pending IRQ returns
       immediately and only a non-matching state halts. */
    int clear = (int)(g->cpu.r[0] & 1);
    uint32_t mask = g->cpu.r[1] & 0x3FFF;
    if (clear) {
        g->if_reg &= (uint16_t)~mask;
    } else {
        if (g->if_reg & mask) return;   /* ya hay una IRQ esperada */
    }
    /* GBATEK (SWI 04h IntrWait): the CPU sleeps until one of the awaited
       IRQs is raised. Unlike plain Halt, an unrelated IRQ must NOT wake it:
       the BIOS loop re-checks the mask and re-halts. The wait_mask reserves
       this path so only the awaited source releases the halt. */
    g->cst.wait_mask = (uint16_t)mask;
    g->cst.halt = true;
}
static void swi_vblank_intr_wait(GBA *g) {
    /* SWI 0x05: VBlankIntrWait. Equivalente a IntrWait(1,1): descarta un
       VBlank ya pendiente y duerme hasta el siguiente. GBATEK SWI 05h. */
    g->if_reg &= (uint16_t)~IF_VBLANK;
    g->cst.wait_mask = IF_VBLANK;
    g->cst.halt = true;
}
void swi_div32(GBA *g) {
    int32_t a = (int32_t)g->cpu.r[0];
    int32_t b = (int32_t)g->cpu.r[1];
    uint32_t cycles = 4 + 13 + 7;   /* fallback: one loop */
    if (b == 0) { g->cpu.r[0] = (uint32_t)a; g->cpu.r[1] = (uint32_t)a; g->cpu.r[3] = 0; }
    else if (b == -1 && a == INT32_MIN) {
        /* GBATEK: 0x80000000 / -1 yields 0x80000000 with remainder 0
           on ARM7TDMI; the C division would be undefined behaviour. */
        g->cpu.r[0] = (uint32_t)INT32_MIN;
        g->cpu.r[1] = 0;
        g->cpu.r[3] = 0;
    } else {
        g->cpu.r[0] = (uint32_t)(a / b);
        g->cpu.r[3] = (uint32_t)(a % b);
        g->cpu.r[1] = (uint32_t)(a % b);
        int loops = (b != 0) ? __builtin_clz((uint32_t)(b < 0 ? -b : b)) -
                              __builtin_clz((uint32_t)(a < 0 ? -a : a)) : 1;
        if (loops < 1) loops = 1;
        cycles = 4 + 13 * loops + 7;
    }
    gba_add_cycles(g, cycles);
}
static void swi_div_arm(GBA *g) { swi_div32(g); }
static void swi_sqrt(GBA *g) {
    uint32_t n = g->cpu.r[0];
    uint32_t r = 0, b = 0x40000000;
    while (b > n) b >>= 2;
    while (b) { if (n >= r + b) { n -= r + b; r = (r >> 1) | b; } else r >>= 1; b >>= 2; }
    g->cpu.r[0] = r;
}
static void swi_cpu_set(GBA *g) {
    uint32_t src = g->cpu.r[0];
    uint32_t dst = g->cpu.r[1];
    uint32_t ctl = g->cpu.r[2];
    uint32_t count = ctl & 0x1FFFFF;
    bool fixed = ctl & 0x01000000;
    bool is32 = ctl & 0x04000000;
    if (is32) {
        for (uint32_t i = 0; i < count; i++) {
            uint32_t v = gba_read32(g, src);
            gba_write32(g, dst, v);
            if (!fixed) src += 4;
            dst += 4;
        }
    } else {
        for (uint32_t i = 0; i < count; i++) {
            uint16_t v = gba_read16(g, src);
            gba_write16(g, dst, v);
            if (!fixed) src += 2;
            dst += 2;
        }
    }
    /* GBATEK (BIOS Functions, CpuSet): 2 cycles per 16-bit word, 1 per 32-bit */
    gba_add_cycles(g, is32 ? count : count * 2);
}
static void swi_cpu_fast_set(GBA *g) {
    uint32_t src = g->cpu.r[0];
    uint32_t dst = g->cpu.r[1];
    uint32_t ctl = g->cpu.r[2];
    uint32_t count = ctl & 0x1FFFFF;
    bool fixed = ctl & 0x01000000;
    bool is32 = ctl & 0x04000000;
    if (is32) {
        for (uint32_t i = 0; i < count; i++) {
            uint32_t v = gba_read32(g, src);
            gba_write32(g, dst, v);
            if (!fixed) src += 4;
            dst += 4;
        }
    } else {
        for (uint32_t i = 0; i < count; i++) {
            uint16_t v = gba_read16(g, src);
            gba_write16(g, dst, v);
            if (!fixed) src += 2;
            dst += 2;
        }
    }
    /* GBATEK (CpuFastSet): 1 cycle per 32-bit word */
    gba_add_cycles(g, count);
}
/* SWI 0x0E (BgAffineSet).  Source entries are 20 bytes:
 *   0: ox (s32, 8.8),  4: oy (s32, 8.8),   8: cx (s16), 10: cy (s16),
 *  12: sx (s16, 8.8), 14: sy (s16, 8.8),  16: theta (u8, 1/128 turn per 8)
 * Destination is a 16-byte BG affine matrix+offset block:
 *   +0..+6: pa/pb/pc/pd (s16, 8.8), +8: rx (s32, 8.8), +12: ry (s32, 8.8).
 * Formula (mGBA/GBATEK):
 *     [pa pb]   [sx 0 ] [cos -sin]         [rx]   [ox]   [pa pb] [cx]
 *     [pc pd] = [0  sy] [sin  cos]    ->   [ry] = [oy] - [pc pd] [cy]      */
static void swi_affine_set(GBA *g) {
    int count = (int)g->cpu.r[2];
    uint32_t off = g->cpu.r[0];
    uint32_t dst = g->cpu.r[1];

    for (int i = 0; i < count; i++) {
        int32_t ox = gba_read32(g, off + 0);
        int32_t oy = gba_read32(g, off + 4);
        int32_t cx  = (int16_t)gba_read16(g, off + 8);
        int32_t cy  = (int16_t)gba_read16(g, off + 10);
        int32_t sx  = (int16_t)gba_read16(g, off + 12);   /* 8.8 fixed */
        int32_t sy  = (int16_t)gba_read16(g, off + 14);
        int32_t theta = (int8_t)gba_read8(g, off + 16) << 8; /* sign extend, 1.15 angle */
        off += 20;

        int32_t c, s;
        aff_angle(theta, &c, &s);                         /* 1.15 each */

        /* A = sx*cos,  B = -sx*sin,  C = sy*sin,  D = sy*cos (all 8.8 -> 1.15) */
        int32_t A = (sx * c) >> 7;                        /* 8.8 * 1.15 = 16.23 -> /256 = A in 8.8 */
        int32_t B = (-sx * s) >> 7;
        int32_t C = (sy * s) >> 7;
        int32_t D = (sy * c) >> 7;

        int32_t rx = ox - ((A * cx) >> 8) - ((B * cy) >> 8);
        int32_t ry = oy - ((C * cx) >> 8) - ((D * cy) >> 8);

        gba_write16(g, dst + 0, (uint16_t)aff_sat16(A));
        gba_write16(g, dst + 2, (uint16_t)aff_sat16(B));
        gba_write16(g, dst + 4, (uint16_t)aff_sat16(C));
        gba_write16(g, dst + 6, (uint16_t)aff_sat16(D));
        gba_write32(g, dst + 8,  (uint32_t)aff_sat32(rx));
        gba_write32(g, dst + 12, (uint32_t)aff_sat32(ry));
        dst += 16;
    }
}

/* SWI 0x0F (ObjAffineSet).  Source entries are 8 bytes:
 *   0: sx (s16, 8.8), 2: sy (s16, 8.8), 4: theta (s8), 5: unused.
 * Destination entries are four s16 (pa/pb/pc/pd) spaced by the fourth
 * argument (default 8).  No translation is written. */
static void swi_obj_affine_set(GBA *g) {
    int count = (int)g->cpu.r[2];
    int diff = (int)g->cpu.r[3];
    if (diff <= 0) diff = 8;             /* GBATEK: 4th arg is the stride */
    uint32_t off = g->cpu.r[0];
    uint32_t dst = g->cpu.r[1];

    for (int i = 0; i < count; i++) {
        int32_t sx  = (int16_t)gba_read16(g, off + 0);
        int32_t sy  = (int16_t)gba_read16(g, off + 2);
        int32_t theta = (int8_t)gba_read8(g, off + 4) << 8;
        off += 8;

        int32_t c, s;
        aff_angle(theta, &c, &s);

        int32_t A = (sx * c) >> 7;      /* 8.8 * 1.15 -> 8.8 */
        int32_t B = (-sx * s) >> 7;
        int32_t C = (sy * s) >> 7;
        int32_t D = (sy * c) >> 7;

        gba_write16(g, dst + 0*diff, (uint16_t)aff_sat16(A));
        gba_write16(g, dst + 1*diff, (uint16_t)aff_sat16(B));
        gba_write16(g, dst + 2*diff, (uint16_t)aff_sat16(C));
        gba_write16(g, dst + 3*diff, (uint16_t)aff_sat16(D));
        dst += diff * 4;
    }
}

/* Read the 3-byte little-endian decompressed length after a header byte. */
static uint32_t swi_read_len(GBA *g, uint32_t *src) {
    uint32_t len = gba_read8(g, *src) | (gba_read8(g, *src + 1) << 8) |
                   (gba_read8(g, *src + 2) << 16);
    *src += 3;
    return len;
}

/* Decompression destination that writes bytes into VRAM as 16-bit units.
 * GBATEK ("LZ77UnCompVram"): the byte stream is packed two bytes per
 * halfword, so the address advances one byte per output byte. Writing one
 * halfword per byte (as an earlier version did) spreads the data over twice
 * the range and leaves half of VRAM untouched. */
typedef struct { GBA *g; uint32_t addr; bool vram; uint8_t half; } Dst;
static void dst_put(Dst *d, uint8_t v) {
    if (!d->vram) { gba_write8(d->g, d->addr, v); d->addr++; return; }
    if (d->addr & 1) gba_write16(d->g, d->addr ^ 1u, (uint16_t)(d->half | (v << 8)));
    else             d->half = v;
    d->addr++;
}

/* SWI 0x11/0x12: LZ77 (header 0x10). */
static void swi_lz77(GBA *g, bool vram) {
    uint32_t src = g->cpu.r[0];
    src++;                                  /* header 0x10 */
    uint32_t len = swi_read_len(g, &src);
    Dst d = { g, g->cpu.r[1], vram, 0 };
    uint32_t left = len;
    while (left > 0) {
        uint8_t flags = gba_read8(g, src++);
        for (int i = 0; i < 8 && left > 0; i++) {
            if (flags & 0x80) {
                uint8_t b1 = gba_read8(g, src++);
                uint8_t b2 = gba_read8(g, src++);
                uint32_t count = (b1 >> 4) + 3;
                uint32_t disp = (((b1 & 0xF) << 8) | b2) + 1;
                for (uint32_t k = 0; k < count && left > 0; k++) {
                    uint8_t v = gba_read8(g, d.addr - disp);
                    dst_put(&d, v);
                    left--;
                }
            } else {
                uint8_t v = gba_read8(g, src++);
                dst_put(&d, v);
                left--;
            }
            flags <<= 1;
        }
    }
    /* mGBA _unLz77: ~20 prologue + 14/flag byte + 10/output byte */
    gba_swi_stall(g, 20 + 14 * (len / 8 + 1) + 10 * len);
    g->cpu.r[0] = len;
}

/* SWI 0x14/0x15: run-length (header 0x30). */
static void swi_rluncomp(GBA *g, bool vram) {
    uint32_t src = g->cpu.r[0];
    src++;                                  /* header 0x30 */
    uint32_t len = swi_read_len(g, &src);
    Dst d = { g, g->cpu.r[1], vram, 0 };
    uint32_t left = len;
    /* VRAM output is padded up to a whole halfword after the payload. */
    uint32_t pad = vram ? ((4 - (len & 3)) & 3) : 0;
    while (left > 0) {
        uint8_t flags = gba_read8(g, src++);
        if (flags & 0x80) {
            uint32_t count = (flags & 0x7F) + 3;
            uint8_t v = gba_read8(g, src++);
            for (uint32_t k = 0; k < count && left > 0; k++, left--) dst_put(&d, v);
        } else {
            uint32_t count = (flags & 0x7F) + 1;
            for (uint32_t k = 0; k < count && left > 0; k++, left--) dst_put(&d, gba_read8(g, src++));
        }
    }
    while (pad--) dst_put(&d, 0);
    gba_swi_stall(g, 16 + 10 * (len + 1));
    g->cpu.r[0] = len;
}

/* SWI 0x13: Huffman. Header 0x20; tree size and bitstream length nibbles. */
static void swi_huffuncomp(GBA *g) {
    uint32_t src = g->cpu.r[0];
    uint32_t dst = g->cpu.r[1];
    src++;                                  /* header 0x20 */
    uint32_t len = swi_read_len(g, &src);
    uint8_t hdr = gba_read8(g, src++);
    uint32_t tree_size = ((hdr & 0xF) + 1) * 2;
    uint32_t bit_len = (hdr >> 4) + 1;
    uint8_t tree[64], bits[64];
    for (uint32_t i = 0; i < tree_size && i < 64; i++) tree[i] = gba_read8(g, src++);
    for (uint32_t i = 0; i < bit_len && i < 64; i++) bits[i] = gba_read8(g, src++);
    uint32_t left = len, bit = 0;
    while (left > 0) {
        uint32_t node = 0;
        do {
            uint8_t b = bits[bit >> 3];
            uint32_t v = (b >> (7 - (bit & 7))) & 1;
            bit++;
            node = tree[node * 2 + v];
        } while ((int8_t)node >= 0);
        gba_write8(g, dst, (uint8_t)node);
        dst++; left--;
    }
    gba_swi_stall(g, 30 + 12 * len);
    g->cpu.r[0] = len;
}

/* SWI 0x10: BitUnPack. Expands a packed source to a given destination width. */
static void swi_bitunpack(GBA *g) {
    uint32_t src = g->cpu.r[0];
    uint32_t dst = g->cpu.r[1];
    uint32_t info = g->cpu.r[2];
    uint32_t src_len = gba_read16(g, info);
    if (src_len == 0) src_len = 0x10000;
    uint32_t src_bpp = gba_read8(g, info + 2);
    uint32_t dst_bpp = gba_read8(g, info + 3);
    uint8_t data_off = gba_read8(g, info + 4);
    uint32_t out_unit = gba_read8(g, info + 5);
    uint32_t data_len = (dst_bpp >= 32) ? 0xFFFFFFFFu : ((1u << dst_bpp) - 1);
    uint32_t total = src_bpp ? (src_len * 8) / src_bpp : 0;
    uint32_t bit = 0;
    for (uint32_t u = 0; u < total; u++) {
        uint8_t byte = gba_read8(g, src + (bit >> 3));
        uint32_t val = (byte >> (8 - src_bpp - (bit & 7))) & ((1u << src_bpp) - 1);
        bit += src_bpp;
        if (val != data_len) {
            uint8_t out = (uint8_t)(val + data_off);
            if (out_unit == 1) gba_write16(g, dst, out);
            else if (out_unit == 2) gba_write32(g, dst, out);
            else gba_write8(g, dst, out);
        }
        dst += (out_unit == 1) ? 2 : (out_unit == 2) ? 4 : 1;
    }
    gba_swi_stall(g, 20 + 6 * total);
}

/* SWI 0x16/0x17/0x18: difference filters (header 0x80). 0x16 = 8bit->WRAM,
 * 0x17 = 8bit->VRAM, 0x18 = 16bit->16bit (GBATEK "Diff8bitUnFilterWram",
 * "Diff8bitUnFilterVram", "Diff16bitUnFilter"). */
static void swi_diffunfilter(GBA *g, bool vram, bool is16bit) {
    uint32_t src = g->cpu.r[0] & ~3u;
    src++;                                  /* header 0x80 */
    uint32_t len = swi_read_len(g, &src);
    Dst d = { g, g->cpu.r[1], vram, 0 };
    if (is16bit) {
        uint16_t prev = 0;
        for (uint32_t i = 0; i < len / 2; i++) {
            prev = (uint16_t)(prev + gba_read16(g, src));
            src += 2;
            gba_write16(g, d.addr, prev);
            d.addr += 2;
        }
        gba_swi_stall(g, 16 + 10 * (len / 2));
    } else {
        uint8_t prev = 0;
        for (uint32_t i = 0; i < len; i++) {
            prev = (uint8_t)(prev + gba_read8(g, src++));
            dst_put(&d, prev);
        }
        gba_swi_stall(g, 16 + 8 * len);
    }
}

void gba_swi(GBA *g, uint32_t comment) {
    uint32_t swi_num = comment & 0xFF;

    /* GBATEK: durante la ejecucion del handler del SWI, una lectura a la BIOS
       devuelve el prefetch de 0x188+8. Como el SWI se emula (no se ejecuta el
       vector real de la BIOS), hay que fijarlo aqui. */
    g->bios_prefetch = 0xE3A02004u;

    /* Advance the program counter past the SWI instruction before running
       the handler. Handlers such as Halt/IntrWait set cst.halt, which the
       frame loop then uses to sleep the CPU until the next interrupt. */
    if (thumb_mode_p(g)) g->cpu.r[15] += 2;
    else                 g->cpu.r[15] += 4;

    switch (swi_num) {
    case 0x00: swi_soft_reset(g); break;
    case 0x01: swi_register_ram_reset(g); break;
    case 0x02: swi_halt(g); break;
    case 0x03: swi_stop(g); break;
    case 0x04: swi_intr_wait(g); break;
    case 0x05: swi_vblank_intr_wait(g); break;
    case 0x06: swi_div32(g); break;
    case 0x07: swi_div_arm(g); break;
    case 0x08: swi_sqrt(g); break;
    case 0x0B: swi_cpu_set(g); break;
    case 0x0C: swi_cpu_fast_set(g); break;
    case 0x0E: swi_affine_set(g); break;
    case 0x0F: swi_obj_affine_set(g); break;
    case 0x10: swi_bitunpack(g); break;
    case 0x11: swi_lz77(g, false); break;
    case 0x12: swi_lz77(g, true); break;
    case 0x13: swi_huffuncomp(g); break;
    case 0x14: swi_rluncomp(g, false); break;
    case 0x15: swi_rluncomp(g, true); break;
    case 0x16: swi_diffunfilter(g, false, false); break;
    case 0x17: swi_diffunfilter(g, true,  false); break;
    case 0x18: swi_diffunfilter(g, true,  true);  break;
    default: break;
    }
    /* clear halt if we just returned from a wait that should continue */
    /* normal return: PC was already incremented by arm_exec */
}
/* Minimal BIOS HLE for the interrupt vectors.  A real BIOS maps its default
 * IRQ handler at address 0x18 and, around entry, reads the address stored at
 * 0x03FFFFFC as the IRQ dispatcher pointer.  When no BIOS dump is loaded we
 * install a tiny stub in IWRAM (the BIOS shadow region is beyond the ARM7's
 * 0x03FF0000-0x03FFFFFF area) so an interrupt taken before the game installs
 * its own handler returns immediately instead of executing empty space.
 *
 * The stub must be DMA-ed into IWRAM because code cannot normally be written
 * there via normal 16/32-bit stores at 0x03000000+ (the game is still running
 * from a low address and has full access).  Practically we place it at the
 * very top of IWRAM (0x03007F00) which BIOS-driven code typically treats as
 * scratch.
 */
static void bios_write32(uint8_t *dst, uint32_t v) {
    dst[0] = (uint8_t)(v & 0xFF);
    dst[1] = (uint8_t)((v >> 8) & 0xFF);
    dst[2] = (uint8_t)((v >> 16) & 0xFF);
    dst[3] = (uint8_t)((v >> 24) & 0xFF);
}

void bios_install_stub(GBA *g) {
    /* Reproduce el vector IRQ y la rutina de despacho de la BIOS real
       (GBATEK "GBA Interrupt Control"; secuencia observada en un dump de
       la BIOS original):
        0x018: b  #0x128                  vector IRQ
        0x128: push {r0-r3, ip, lr}       guarda el contexto
        0x12c: mov  r0, #0x04000000
        0x130: add  lr, pc, #0            lr = 0x138 (epilogo)
        0x134: ldr  pc, [r0, #-4]         salta a [0x03FFFFFC] (handler del juego)
        0x138: pop  {r0-r3, ip, lr}
        0x13c: subs pc, lr, #4            retorno IRQ (restaura CPSR de SPSR_irq)
       Asi el flujo es identico haya o no volcado de BIOS: el juego instala su
       handler en 0x03007FFC y la IRQ entra por el vector 0x18, pasa por la
       rutina de despacho y vuelve con la semantica ARM correcta. */
    bios_write32(g->bios + 0x018, 0xEA000042u);   /* b  #0x128 */
    bios_write32(g->bios + 0x128, 0xE92D500Fu);   /* push {r0-r3, ip, lr} */
    bios_write32(g->bios + 0x12C, 0xE3A00301u);   /* mov  r0, #0x04000000 */
    bios_write32(g->bios + 0x130, 0xE28FE000u);   /* add  lr, pc, #0 */
    bios_write32(g->bios + 0x134, 0xE510F004u);   /* ldr  pc, [r0, #-4] */
    bios_write32(g->bios + 0x138, 0xE8BD500Fu);   /* pop  {r0-r3, ip, lr} */
    bios_write32(g->bios + 0x13C, 0xE25EF004u);   /* subs pc, lr, #4 */
    bios_write32(g->bios + 0x144, 0xE55EC002u);   /* ldrb ip, [lr, #-2]: valor que
                                                     retiene el prefetcher al
                                                     volver de la IRQ */
}
