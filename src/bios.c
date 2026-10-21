/*
 * bios.c - GBA BIOS SWI handlers.
 * Clean-room implementation of the classic GBA BIOS calls used to boot games.
 */
#include "gba.h"
#include "mem.h"
#include <string.h>

static inline bool thumb_mode_p(GBA *g) { return (g->cpu.cpsr & CPSR_T) != 0; }

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
}
static void swi_stop(GBA *g) {
    g->cst.halt = true;
    g->cst.stopped = true;
}
static void swi_intr_wait(GBA *g) {
    /* SWI 0x04: wait for any of the IRQs in r0; r1=1 discards old flags.
       We let the frame loop keep running and only halt the CPU here. */
    g->cst.halt = true;
}
static void swi_vblank_intr_wait(GBA *g) {
    /* SWI 0x05: VBlankIntrWait. Equivalent to IntrWait(1,1).
       Halt until the VBlank IRQ condition is met; the frame loop clears
       halt as soon as IF_VBLANK is raised. */
    g->cst.halt = true;
}
static void swi_div32(GBA *g) {
    int32_t a = (int32_t)g->cpu.r[0];
    int32_t b = (int32_t)g->cpu.r[1];
    if (b == 0) { g->cpu.r[0] = a; g->cpu.r[1] = a; g->cpu.r[3] = 0; return; }
    g->cpu.r[0] = (uint32_t)(a / b);
    g->cpu.r[3] = (uint32_t)(a % b);
    g->cpu.r[1] = (uint32_t)(a % b);
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
}
static void swi_affine_set(GBA *g) { (void)g; }

/* Read the 3-byte little-endian decompressed length after a header byte. */
static uint32_t swi_read_len(GBA *g, uint32_t *src) {
    uint32_t len = gba_read8(g, *src) | (gba_read8(g, *src + 1) << 8) |
                   (gba_read8(g, *src + 2) << 16);
    *src += 3;
    return len;
}

/* SWI 0x11/0x12: LZ77. The VRAM variant writes one byte per halfword. */
static void swi_lz77(GBA *g, bool vram) {
    uint32_t src = g->cpu.r[0];
    uint32_t dst = g->cpu.r[1];
    src++;                                  /* header 0x10 */
    uint32_t len = swi_read_len(g, &src);
    uint32_t step = vram ? 2 : 1;
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
                    uint8_t v = gba_read8(g, dst - disp * step);
                    if (vram) gba_write16(g, dst, v); else gba_write8(g, dst, v);
                    dst += step; left--;
                }
            } else {
                uint8_t v = gba_read8(g, src++);
                if (vram) gba_write16(g, dst, v); else gba_write8(g, dst, v);
                dst += step; left--;
            }
            flags <<= 1;
        }
    }
    g->cpu.r[0] = len;
}

/* SWI 0x14/0x15: run-length. Header 0x30. */
static void swi_rluncomp(GBA *g, bool vram) {
    uint32_t src = g->cpu.r[0];
    uint32_t dst = g->cpu.r[1];
    src++;                                  /* header 0x30 */
    uint32_t len = swi_read_len(g, &src);
    uint32_t step = vram ? 2 : 1;
    uint32_t left = len;
    while (left > 0) {
        uint8_t flags = gba_read8(g, src++);
        for (int i = 0; i < 8 && left > 0; i++) {
            if (flags & 0x80) {
                uint32_t count = (gba_read8(g, src++) & 0x7F) + 3;
                uint8_t v = gba_read8(g, src++);
                for (uint32_t k = 0; k < count && left > 0; k++) {
                    if (vram) gba_write16(g, dst, v); else gba_write8(g, dst, v);
                    dst += step; left--;
                }
            } else {
                uint32_t count = (gba_read8(g, src++) & 0x7F) + 1;
                for (uint32_t k = 0; k < count && left > 0; k++) {
                    uint8_t v = gba_read8(g, src++);
                    if (vram) gba_write16(g, dst, v); else gba_write8(g, dst, v);
                    dst += step; left--;
                }
            }
            flags <<= 1;
        }
    }
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
}

/* SWI 0x16/0x17/0x18: difference filters. Header 0x80. */
static void swi_diffunfilter(GBA *g, bool vram, bool is16bit) {
    uint32_t src = g->cpu.r[0];
    uint32_t dst = g->cpu.r[1];
    src++;                                  /* header 0x80 */
    uint32_t len = swi_read_len(g, &src);
    if (is16bit) {
        uint32_t prev = 0;
        for (uint32_t i = 0; i < len / 2; i++) {
            prev = (uint16_t)(prev + gba_read16(g, src));
            src += 2;
            gba_write16(g, dst, (uint16_t)prev);
            dst += 2;
        }
    } else {
        uint8_t prev = 0;
        for (uint32_t i = 0; i < len; i++) {
            prev = (uint8_t)(prev + gba_read8(g, src++));
            if (vram) gba_write16(g, dst, prev); else gba_write8(g, dst, prev);
            dst++;
        }
    }
}

void gba_swi(GBA *g, uint32_t comment) {
    uint32_t swi_num = comment & 0xFF;

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
    case 0x10: swi_bitunpack(g); break;
    case 0x11: swi_lz77(g, false); break;
    case 0x12: swi_lz77(g, true); break;
    case 0x13: swi_huffuncomp(g); break;
    case 0x14: swi_rluncomp(g, false); break;
    case 0x15: swi_rluncomp(g, true); break;
    case 0x16: swi_diffunfilter(g, false, true); break;
    case 0x17: swi_diffunfilter(g, false, false); break;
    case 0x18: swi_diffunfilter(g, true, false); break;
    default: break;
    }
    /* clear halt if we just returned from a wait that should continue */
    /* normal return: PC was already incremented by arm_exec */
}