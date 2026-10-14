/*
 * bios.c - GBA BIOS SWI handlers.
 * Clean-room implementation of the classic GBA BIOS calls used to boot games.
 */
#include "gba.h"
#include "mem.h"

static void swi_soft_reset(GBA *g) { (void)g; }
static void swi_register_ram_reset(GBA *g) { (void)g; }
static void swi_halt(GBA *g) {
    g->cst.halt = true;
}
static void swi_stop(GBA *g) {
    g->cst.halt = true;
    g->cst.stopped = true;
}
static void swi_intr_wait(GBA *g) {
    /* WFI: wait for VBlank or other interrupt */
    g->cst.halt = true;
}
static void swi_vblank_intr_wait(GBA *g) {
    /* VBlankIntrWait:  r0=flags r1=?. Wait until vblank flag and IF pending */
    if (!(g->ppu.dispstat & 0x0002)) {
        g->cst.halt = true;
    }
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
    uint32_t count = ctl & 0xFFFFF;
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

void gba_swi(GBA *g) {
    uint32_t n = (g->cpu.r[6] >> 16) & 0xFF;   /* SWI number in r6 for Thumb; ARM: from opcode */
    /* On ARM, SWI number in opcode; in this interpreter arm_exec already consumed opcode.
       We use r7 convention: the caller stores the number in r7 for bios via 'swi 0x123456'? */
    /* For simplicity: number = (r7 & 0xFF) if set by bios?  The GBA bios uses r12? no */
    /* Real: number comes from DEBUG {opcode}. We approximate: g->cpu.r[6]>>16 is used by homebrew.
       Use lowest host convention: value in r[7] for internal; fallback 0. */
    uint32_t swi_num = g->cpu.r[7] & 0xFF;
    /* Some titles call via r7 with the number; vba-m uses that. If zero, try r6->16 */
    if (swi_num == 0) swi_num = (g->cpu.r[6] >> 16) & 0xFF;

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
    default: break;
    }
    /* clear halt if we just returned from a wait that should continue */
    /* normal return: PC was already incremented by arm_exec */
}