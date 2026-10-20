/*
 * arm.c - ARM7TDMI interpreter (ARM + Thumb) for BoycottAdvance Reborn.
 *
 * Clean-room reimplementation of the ARM7TDMI instruction set (ARMv4T),
 * written from scratch against the ARM architecture reference.
 * Guided by the reverse engineered structure of BoycottAdvance 0.2.8
 * (Julien Frelat et al), but with a complete core so commercial titles
 * (e.g. Pokemon) boot better than the original 80% ARM / 95% Thumb core.
 */
#include "gba.h"
#include "arm.h"
#include "mem.h"
#include <stdio.h>

static inline bool thumb_mode(GBA *g) { return (g->cpu.cpsr & CPSR_T) != 0; }

/* r15 holds PC of the currently executed instruction. Reads of r15 add the
 * classic ARM pipeline offset (+8 ARM / +4 Thumb); writes clear bit 0. */
static inline uint32_t read_reg(GBA *g, int n) {
    if (n == 15) return g->cpu.r[15] + (thumb_mode(g) ? 4 : 8);
    return g->cpu.r[n];
}
static inline void write_reg(GBA *g, int n, uint32_t v) {
    if (n == 15) v &= ~1;   /* no ARM/Thumb bit in plain writes */
    g->cpu.r[n] = v;
}
static inline uint32_t r15(GBA *g) { return g->cpu.r[15]; }
static inline void set_pc(GBA *g, uint32_t v) { g->cpu.r[15] = v & ~1; }
static inline uint32_t get_cpsr(GBA *g) { return g->cpu.cpsr; }
static inline void set_cpsr(GBA *g, uint32_t v) { g->cpu.cpsr = v; }
static inline void add_cycles(GBA *g, uint32_t n) { g->cycle += n; }

/* ---- flags helpers ---------------------------------------------------- */
static void flags_logic(GBA *g, uint32_t r, uint32_t carry) {
    uint32_t c = get_cpsr(g);
    c &= ~(CPSR_N | CPSR_Z | CPSR_C);
    if (r & 0x80000000) c |= CPSR_N;
    if (r == 0) c |= CPSR_Z;
    if (carry) c |= CPSR_C;
    set_cpsr(g, c);
}
static void flags_add3(GBA *g, uint32_t a, uint32_t b, uint32_t r, uint32_t cin) {
    uint32_t c = get_cpsr(g) & ~(CPSR_N | CPSR_Z | CPSR_C | CPSR_V);
    if (r & 0x80000000) c |= CPSR_N;
    if (r == 0) c |= CPSR_Z;
    uint32_t r2 = (uint32_t)(a + b + cin);
    if ((uint64_t)a + b + cin >= 0x100000000ULL) c |= CPSR_C;
    if (~(a ^ b) & (a ^ r2) & 0x80000000) c |= CPSR_V;
    set_cpsr(g, c);
}
static void flags_sub3(GBA *g, uint32_t a, uint32_t b, uint32_t r, uint32_t bin) {
    uint32_t c = get_cpsr(g) & ~(CPSR_N | CPSR_Z | CPSR_C | CPSR_V);
    if (r & 0x80000000) c |= CPSR_N;
    if (r == 0) c |= CPSR_Z;
    if ((uint64_t)a + (~b) + bin >= 0x100000000ULL) c |= CPSR_C;
    if ((a ^ b) & (a ^ r) & 0x80000000) c |= CPSR_V;
    set_cpsr(g, c);
}

#define set_flags_logic(g, v, c)  flags_logic((g), (v), (c))
#define set_flags_add(g, a, b, r) flags_add3((g), (a), (b), (r), 0)
#define set_flags_sub(g, a, b, r) flags_sub3((g), (a), (b), (r), 1)
#define set_flags_addsub(g, a, b, r, is_sub) ((is_sub)? flags_sub3((g),(a),(b),(r),1) : flags_add3((g),(a),(b),(r),0))

/* mode helpers (banked regs) */
static int bank_of(uint32_t cpsr) {
    switch (cpsr & 0x1F) {
    case 0x11: return BANK_FIQ;
    case 0x12: return BANK_IRQ;
    case 0x13: return BANK_SVC;
    case 0x17: return BANK_ABT;
    case 0x1B: return BANK_UND;
    default:   return BANK_USR;
    }
}

static void change_mode(GBA *g, uint32_t new_cpsr) {
    int ob = bank_of(get_cpsr(g)), nb = bank_of(new_cpsr);
    if (ob == nb) { set_cpsr(g, new_cpsr); return; }
    /* save current mode's r13/r14 into banks */
    g->cpu.bank[ob][5] = g->cpu.r[13];
    g->cpu.bank[ob][6] = g->cpu.r[14];
    /* switch mode, restore r13/r14 of new mode */
    set_cpsr(g, new_cpsr);
    g->cpu.r[13] = g->cpu.bank[nb][5];
    g->cpu.r[14] = g->cpu.bank[nb][6];
}

void arm_set_mode(GBA *g, uint32_t mode) {
    change_mode(g, (get_cpsr(g) & ~0x1F) | mode);
}

static void enter_exception(GBA *g, uint32_t mode, uint32_t lr_val, uint32_t pc_addr) {
    uint32_t old = get_cpsr(g);
    g->cpu.spsr[bank_of(mode)] = old;               /* SPSR_<mode> = CPSR */
    /* Vectors run in ARM state; force IRQ-disable and clear the Thumb bit. */
    change_mode(g, (old & ~0x3Fu) | mode | CPSR_I);
    g->cpu.r[14] = lr_val;                          /* LR_<mode> */
    set_pc(g, pc_addr);
}

/* ---- shifter operand ------------------------------ */
typedef struct { uint32_t value; uint32_t carry; } Operand;

static inline uint32_t rotr(uint32_t v, uint32_t s) { return (v >> s) | (v << (32 - s)); }

static void barrel_shift(GBA *g, uint32_t v, uint32_t kind, uint32_t amt, bool reg, Operand *out) {
    uint32_t c = (get_cpsr(g) & CPSR_C) ? 1 : 0;
    /* A register-specified shift with a count of 0 performs no shift and
     * leaves the carry unchanged; only immediate-encoded shifts treat 0 as
     * "32" (LSR/ASR #32) or RRX (ROR #0). */
    if (reg && amt == 0) { out->value = v; out->carry = c; return; }
    switch (kind) {
    case 0: /* LSL */
        if (amt == 0) { out->value = v; out->carry = c; }
        else if (amt < 32) { out->value = v << amt; out->carry = (v >> (32 - amt)) & 1; }
        else if (amt == 32) { out->value = 0; out->carry = v & 1; }
        else { out->value = 0; out->carry = 0; }
        return;
    case 1: /* LSR */
        if (amt == 0) amt = 32;
        if (amt < 32) { out->value = v >> amt; out->carry = (v >> (amt - 1)) & 1; }
        else if (amt == 32) { out->value = 0; out->carry = v >> 31; }
        else { out->value = 0; out->carry = 0; }
        return;
    case 2: /* ASR */
        if (amt == 0) amt = 32;
        if (amt < 32) { out->value = (uint32_t)((int32_t)v >> amt); out->carry = (v >> (amt - 1)) & 1; }
        else { out->value = (v & 0x80000000) ? 0xFFFFFFFF : 0; out->carry = v >> 31; }
        return;
    case 3: /* ROR / RRX */
        if (amt == 0) { uint32_t newc = v & 1; out->value = (v >> 1) | (c ? 0x80000000u : 0); out->carry = newc; return; }
        amt &= 31;
        if (amt == 0) { out->value = v; out->carry = c; }
        else { out->value = rotr(v, amt); out->carry = (v >> (amt - 1)) & 1; }
        return;
    }
    out->value = 0; out->carry = 0;
}

static Operand get_operand(GBA *g, uint32_t insn) {
    Operand o;
    if (insn & (1 << 25)) {
        uint32_t imm = insn & 0xFF, rot = ((insn >> 8) & 0xF) << 1;
        o.value = rotr(imm, rot);
        o.carry = (rot == 0) ? ((get_cpsr(g) & CPSR_C) ? 1 : 0) : (o.value >> 31);
    } else {
        uint32_t rm = insn & 0xF, typ = (insn >> 5) & 3;
        uint32_t v = read_reg(g, rm);
        if (insn & (1 << 4)) {
            uint32_t rs = (insn >> 8) & 0xF;
            uint32_t amt = read_reg(g, rs);
            if (rs == 15) amt = r15(g) + 8;      /* architecture quirk */
            barrel_shift(g, v, typ, amt & 0xFF, true, &o);
        } else {
            uint32_t amt = (insn >> 7) & 0x1F;
            barrel_shift(g, v, typ, amt, false, &o);
        }
    }
    return o;
}

/* ---- condition codes ------------------------------ */
static bool cond_holds(uint32_t cpsr, uint32_t cond) {
    bool n = (cpsr & CPSR_N) != 0, z = (cpsr & CPSR_Z) != 0;
    bool c = (cpsr & CPSR_C) != 0, v = (cpsr & CPSR_V) != 0;
    switch (cond) {
    case 0x0: return z;                /* EQ */
    case 0x1: return !z;               /* NE */
    case 0x2: return c;                /* CS */
    case 0x3: return !c;               /* CC */
    case 0x4: return n;                /* MI */
    case 0x5: return !n;               /* PL */
    case 0x6: return v;                /* VS */
    case 0x7: return !v;               /* VC */
    case 0x8: return c && !z;          /* HI */
    case 0x9: return !c || z;          /* LS */
    case 0xA: return n == v;           /* GE */
    case 0xB: return n != v;           /* LT */
    case 0xC: return !z && (n == v);   /* GT */
    case 0xD: return z || (n != v);    /* LE */
    default:  return true;             /* AL / NV */
    }
}

/* ===================================================================== */
/* ARM mode interpreter                                                  */
/* ===================================================================== */
extern void gba_swi(GBA *g, uint32_t comment);

static void arm_exec_at(GBA *g, uint32_t insn) {
    uint32_t cpsr = get_cpsr(g);
    uint32_t cond = insn >> 28;
    if (!cond_holds(cpsr, cond)) { set_pc(g, r15(g) + 4); add_cycles(g, 1); return; }

    uint32_t op  = (insn >> 25) & 0x7;
    bool     i   = (insn >> 25) & 1;
    bool     p   = (insn >> 24) & 1, u = (insn >> 23) & 1, b = (insn >> 22) & 1, w = (insn >> 21) & 1;
    bool     l   = (insn >> 20) & 1;
    int      rn  = (insn >> 16) & 0xF, rd = (insn >> 12) & 0xF;

    /* ---- branch / SWI (bits 27-25 = 101) ---- */
    if (op == 0x5) {
        if ((insn & 0x0F000000u) == 0x0F000000u) {   /* SWI: bits 27-24 = 1111 */
            uint32_t comment = (insn >> 16) & 0xFF;
            gba_swi(g, comment);
            return;
        }
        uint32_t off = insn & 0x00FFFFFF;
        if (off & 0x00800000) off |= 0xFF000000;
        if (insn & (1u << 24)) write_reg(g, 14, r15(g) + 4);   /* BL */
        set_pc(g, r15(g) + 8 + (int32_t)(off << 2));
        add_cycles(g, 1);
        return;
    }
    /* ---- BX / BLX ---- */
    if ((insn & 0x0FFFFFF0) == 0x012FFF10) {  /* BX */
        uint32_t rm = read_reg(g, insn & 0xF);
        if (rm & 1) { set_cpsr(g, get_cpsr(g) | CPSR_T); set_pc(g, rm & ~1); }
        else { set_cpsr(g, get_cpsr(g) & ~CPSR_T); set_pc(g, rm & ~3); }
        add_cycles(g, 1);
        return;
    }
    if ((insn & 0x0FFFFFF0) == 0x012FFF30) {  /* BLX reg */
        uint32_t rm = read_reg(g, insn & 0xF);
        write_reg(g, 14, r15(g) + 4);
        set_cpsr(g, get_cpsr(g) & ~CPSR_T);
        set_pc(g, rm & ~3);
        add_cycles(g, 1);
        return;
    }

    /* ---- multiply ---- */
    if ((insn & 0x0FC000F0) == 0x00000090) {   /* MUL/MLA */
        /* Rd = bits 19-16, Rm = 3-0, Rs = 11-8, Rn (addend) = 15-12. */
        uint32_t rm = read_reg(g, insn & 0xF);
        uint32_t rs = read_reg(g, (insn >> 8) & 0xF);
        bool sacc = (insn >> 21) & 1, ss = (insn >> 20) & 1;
        int mul_rd = (insn >> 16) & 0xF;
        uint32_t r = rm * rs;
        if (sacc) r += read_reg(g, (insn >> 12) & 0xF);
        if (ss) flags_logic(g, r, (get_cpsr(g) & CPSR_C) ? 1 : 0);
        write_reg(g, mul_rd, r);
        set_pc(g, r15(g) + 4);
        add_cycles(g, 2);
        return;
    }
    /* long multiply (UMULL/SMULL/UMLAL/SMLAL) */
    if ((insn & 0x0F8000F0) == 0x00800090) {
        /* RdHi = bits 19-16, RdLo = bits 15-12, bit22 = signed, bit21 = accumulate. */
        uint32_t rm = read_reg(g, insn & 0xF), rs = read_reg(g, (insn >> 8) & 0xF);
        int rshi = (insn >> 16) & 0xF, rlo = (insn >> 12) & 0xF;
        bool acc = (insn >> 21) & 1, sgn = (insn >> 22) & 1;
        uint64_t r = sgn ? (uint64_t)((int64_t)(int32_t)rm * (int32_t)rs)
                         : (uint64_t)rm * rs;
        if (acc) r += ((uint64_t)read_reg(g, rlo)) | ((uint64_t)read_reg(g, rshi) << 32);
        write_reg(g, rlo, (uint32_t)r);
        write_reg(g, rshi, (uint32_t)(r >> 32));
        set_pc(g, r15(g) + 4);
        add_cycles(g, 3);
        return;
    }

    /* ---- halfword / signed / double transfer ---- */
    if ((insn & 0x0E000090) == 0x00000090) {
        bool       l   = (insn >> 20) & 1;
        int        hw  = (insn >> 5) & 3;
        uint32_t   off;
        if (insn & (1 << 22)) {
            off = ((insn >> 8) & 0xF) << 4 | (insn & 0xF);
        } else {
            off = read_reg(g, insn & 0xF);
        }
        if (!u) off = (uint32_t)(-(int32_t)off);
        uint32_t base = read_reg(g, rn);
        uint32_t addr = p ? base + off : base;
        if (l) {
            uint32_t val;
            if (hw == 1) val = gba_read16(g, addr);
            else if (hw == 2) val = (uint32_t)(int8_t)(gba_read8(g, addr));
            else val = (uint32_t)(int16_t)(gba_read16(g, addr));
            if (w || !p) write_reg(g, rn, p ? addr : base + off);
            if (rd == 15) { set_pc(g, val & ~3); add_cycles(g, 2); return; }
            write_reg(g, rd, val);
            set_pc(g, r15(g) + 4);
            add_cycles(g, 2);
            return;
        }
        if (hw == 1) gba_write16(g, addr, read_reg(g, rd));
        else gba_write8(g, addr, read_reg(g, rd));
        if (w || !p) write_reg(g, rn, p ? addr : base + off);
        set_pc(g, r15(g) + 4);
        add_cycles(g, 2);
        return;
    }

    /* ---- single data transfer (bits 27-26 = 01) ---- */
    if (op == 0x2 || op == 0x3) {
        uint32_t offset;
        if (i) {
            uint32_t rm = insn & 0xF, typ = (insn >> 5) & 3, amt = (insn >> 7) & 0x1F;
            Operand o; barrel_shift(g, read_reg(g, rm), typ, amt, false, &o);
            offset = o.value;
        } else offset = insn & 0xFFF;
        if (!u) offset = (uint32_t)(-(int32_t)offset);
        uint32_t base = read_reg(g, rn);
        uint32_t addr = p ? base + offset : base;
        if (l) {
            uint32_t val = b ? gba_read8(g, addr) : gba_read32(g, addr);
            if (w || !p) write_reg(g, rn, p ? addr : base + offset);
            if (rd == 15) { set_pc(g, val & ~3); add_cycles(g, 2); return; }
            write_reg(g, rd, val);
            add_cycles(g, 2);
            set_pc(g, r15(g) + 4);
            return;
        } else {
            if (b) gba_write8(g, addr, read_reg(g, rd));
            else gba_write32(g, addr, read_reg(g, rd));
            add_cycles(g, 1);
        }
        if (w || !p) write_reg(g, rn, p ? addr : base + offset);
        set_pc(g, r15(g) + 4);
        return;
    }

    /* ---- block data transfer (bits 27-25 = 100) ---- */
    if (op == 0x4) {
        uint32_t reglist = insn & 0xFFFF;
        int nreg = __builtin_popcount(reglist);
        uint32_t base = read_reg(g, rn);
        uint32_t addr;
        /* IA: base, IB: base+4, DA: base-n*4+4, DB: base-n*4 */
        if (u) addr = p ? base + 4 : base;
        else   addr = p ? base - (uint32_t)nreg * 4 : base - (uint32_t)nreg * 4 + 4;
        if (!l) {
            for (int i = 0; i < 16; i++) if (reglist & (1 << i)) { gba_write32(g, addr, (i == rn && w) ? base : read_reg(g, i)); addr += 4; }
            if (w && !(reglist & (1 << rn))) write_reg(g, rn, u ? base + nreg * 4 : base - nreg * 4);
        } else {
            bool loaded_pc = false;
            for (int i = 0; i < 16; i++) if (reglist & (1 << i)) {
                uint32_t val = gba_read32(g, addr);
                if (i == 15) { set_pc(g, val & ~3); loaded_pc = true; }
                else write_reg(g, i, val);
                addr += 4;
            }
            if (w && !(reglist & (1 << rn))) write_reg(g, rn, u ? base + nreg * 4 : base - nreg * 4);
            if ((reglist & (1 << 15)) && (insn & (1 << 22))) {
                uint32_t spsr = g->cpu.spsr[bank_of(get_cpsr(g))];
                uint32_t new_mode = spsr & 0x1F;
                change_mode(g, (get_cpsr(g) & ~0x1F) | new_mode | (spsr & (CPSR_N|CPSR_Z|CPSR_C|CPSR_V|CPSR_I|CPSR_F|CPSR_T)));
                set_cpsr(g, spsr);
            }
            if (loaded_pc) { add_cycles(g, nreg + 1); return; }
        }
        set_pc(g, r15(g) + 4);
        add_cycles(g, nreg + 1);
        return;
    }

    /* ---- data processing (bits 27-26 = 00) ---- */
    if (((insn >> 26) & 3) == 0) {
        Operand o = get_operand(g, insn);
        uint32_t opc = (insn >> 21) & 0xF;
        bool     s   = (insn >> 20) & 1;
        uint32_t a   = (rn == 15) ? (r15(g) + 8) : read_reg(g, rn);
        uint32_t bv  = o.value;
        uint32_t res = 0;
        bool carry = o.carry;

        /* PSR transfers (MRS/MSR) */
        if ((insn & 0x0FFFF000) == 0x010F0000) {          /* MRS Rd, CPSR */
            write_reg(g, rd, get_cpsr(g));
            set_pc(g, r15(g) + 4); add_cycles(g, 1); return;
        }
        if ((insn & 0x0FFFF000) == 0x014F0000) {          /* MRS Rd, SPSR */
            write_reg(g, rd, g->cpu.spsr[bank_of(get_cpsr(g))]);
            set_pc(g, r15(g) + 4); add_cycles(g, 1); return;
        }
        if ((insn & 0x0FB0F000) == 0x0120F000 ||   /* MSR reg: 00010 R 10 */
            (insn & 0x0FB0F000) == 0x0320F000) {   /* MSR imm: 00110 R 10 */
            uint32_t val = o.value;
            uint32_t mask = 0;
            if (insn & 0x00010000) mask |= 0x000000FF;    /* control */
            if (insn & 0x00020000) mask |= 0x0000FF00;    /* extension */
            if (insn & 0x00040000) mask |= 0x00FF0000;    /* status */
            if (insn & 0x00080000) mask |= 0xFF000000;    /* flags */
            if (insn & 0x00400000) {                      /* to SPSR */
                int b = bank_of(get_cpsr(g));
                g->cpu.spsr[b] = (g->cpu.spsr[b] & ~mask) | (val & mask);
            } else {                                      /* to CPSR */
                uint32_t nc = (get_cpsr(g) & ~mask) | (val & mask);
                /* A control-field write may change the CPU mode: route it
                   through change_mode so r13/r14 get banked in/out, exactly
                   like an exception return does. */
                if (mask & 0xFF) change_mode(g, nc);
                else set_cpsr(g, nc);
            }
            set_pc(g, r15(g) + 4); add_cycles(g, 1); return;
        }
        switch (opc) {
        case 0x0: res = a & bv;   break;
        case 0x1: res = a ^ bv;   break;
        case 0x2: res = a - bv;   break;
        case 0x3: res = bv - a;   break;
        case 0x4: res = a + bv;   break;
        case 0x5: res = a + bv + ((cpsr & CPSR_C) ? 1 : 0); break;
        case 0x6: res = a - bv - ((cpsr & CPSR_C) ? 0 : 1); break;
        case 0x7: res = bv - a - ((cpsr & CPSR_C) ? 0 : 1); break;
        case 0x8: res = a & bv;   break;  /* TST */
        case 0x9: res = a ^ bv;   break;  /* TEQ */
        case 0xA: res = a - bv;   break;  /* CMP */
        case 0xB: res = a + bv;   break;  /* CMN */
        case 0xC: res = a | bv;   break;  /* ORR */
        case 0xD: res = bv;       break;  /* MOV */
        case 0xE: res = a & ~bv;  break;  /* BIC */
        case 0xF: res = ~bv;      break;  /* MVN */
        default:  break;
        }
        /* Flags change only when S is set (TST/TEQ/CMP/CMN always have it).
           Writing them unconditionally corrupts the flags a following
           conditional branch depends on: e.g. the BIOS decompressor's
           MOV-with-shift loops then branch the wrong way and never
           terminate. */
        if (s) switch (opc) {
        case 0x0: case 0x1: case 0x8: case 0x9:
        case 0xC: case 0xD: case 0xE: case 0xF:
            flags_logic(g, res, carry); break;
        case 0x2: flags_sub3(g, a, bv, res, 1); break;
        case 0x3: flags_sub3(g, bv, a, res, 1); break;
        case 0x4: flags_add3(g, a, bv, res, 0); break;
        case 0x5: flags_add3(g, a, bv, res, (cpsr & CPSR_C) ? 1 : 0); break;
        case 0x6: flags_sub3(g, a, bv, res, (cpsr & CPSR_C) ? 1 : 0); break;
        case 0x7: flags_sub3(g, bv, a, res, (cpsr & CPSR_C) ? 1 : 0); break;
        case 0xA: flags_sub3(g, a, bv, res, 1); break;
        case 0xB: flags_add3(g, a, bv, res, 0); break;
        default:  break;
        }
        /* writeback */
        bool write = !(opc == 0x8 || opc == 0x9 || opc == 0xA || opc == 0xB);
        if (write) {
            if (rd == 15 && s) {
                /* MOVS pc, lr */
                uint32_t spsr = g->cpu.spsr[bank_of(get_cpsr(g))];
                change_mode(g, spsr);
                set_cpsr(g, spsr);
                set_pc(g, res & ~3);
                add_cycles(g, 1);
                return;
            }
            if (rd == 15) { set_pc(g, res & ~3); add_cycles(g, 1); return; }
            else write_reg(g, rd, res);
        }
        set_pc(g, r15(g) + 4);
        add_cycles(g, 1);
        return;
    }

    set_pc(g, r15(g) + 4);
    add_cycles(g, 1);
}

void arm_exec(GBA *g) {
    arm_exec_at(g, gba_read32(g, r15(g)));
}

/* ===================================================================== */
/* Thumb mode interpreter                                                */
/* ===================================================================== */
/* ===================================================================== */
/* Thumb mode interpreter (ARMv4T)                                        */
/* Decoded by the top nibble (bits 15-12) into the 19 Thumb formats.      */
/* ===================================================================== */
static void thumb_exec_at(GBA *g, uint16_t insn) {
    uint32_t cpsr = get_cpsr(g);
    uint32_t op = insn >> 12;
    uint32_t rd = insn & 7, rs = (insn >> 3) & 7, rn = (insn >> 6) & 7;
    uint32_t next = r15(g) + 2;

    /* format 2: add/subtract (00011) 0x1800-0x1FFF */
    if ((insn & 0xF800) == 0x1800) {
        bool immf = insn & 0x0400;
        bool sub  = insn & 0x0200;
        uint32_t b = immf ? ((insn >> 6) & 7) : read_reg(g, rn);
        uint32_t a = read_reg(g, rs);
        uint32_t r = sub ? a - b : a + b;
        if (sub) flags_sub3(g, a, b, r, 1); else flags_add3(g, a, b, r, 0);
        write_reg(g, rd, r);
        set_pc(g, next); add_cycles(g, 1); return;
    }

    /* format 1: move shifted register (0000/0001) 0x0000-0x17FF */
    if (op == 0x0 || op == 0x1) {
        uint32_t sh = (insn >> 6) & 0x1F;
        uint32_t kind = (insn >> 11) & 3;
        uint32_t v = read_reg(g, rs);
        uint32_t r = v, carry = (cpsr & CPSR_C) ? 1 : 0;
        if (kind == 0) {                 /* LSL */
            if (sh) { r = v << sh; carry = (v >> (32 - sh)) & 1; }
        } else if (kind == 1) {          /* LSR */
            if (sh) { r = v >> sh; carry = (v >> (sh - 1)) & 1; }
            else { r = 0; carry = (v >> 31) & 1; }
        } else {                         /* ASR */
            if (sh) { r = (uint32_t)((int32_t)v >> sh); carry = (v >> (sh - 1)) & 1; }
            else { r = (v & 0x80000000u) ? 0xFFFFFFFFu : 0; carry = v >> 31; }
        }
        flags_logic(g, r, carry);
        write_reg(g, rd, r);
        set_pc(g, next); add_cycles(g, 1); return;
    }

    /* format 3: MOV/CMP/ADD/SUB immediate (00100/00101/00110/00111) */
    if (op == 0x2 || op == 0x3) {
        uint32_t imm8 = insn & 0xFF;
        uint32_t rd8 = (insn >> 8) & 7;
        uint32_t subop = (insn >> 11) & 3;
        uint32_t a = read_reg(g, rd8);
        if (subop == 0) { flags_logic(g, imm8, (cpsr & CPSR_C) ? 1 : 0); write_reg(g, rd8, imm8); }
        else if (subop == 1) { flags_sub3(g, a, imm8, a - imm8, 1); }
        else if (subop == 2) { uint32_t r = a + imm8; flags_add3(g, a, imm8, r, 0); write_reg(g, rd8, r); }
        else { uint32_t r = a - imm8; flags_sub3(g, a, imm8, r, 1); write_reg(g, rd8, r); }
        set_pc(g, next); add_cycles(g, 1); return;
    }

    /* op == 0100: format 6 (PC-rel load), 5 (hi reg/BX) or 4 (ALU) */
    if (op == 0x4) {
        if (insn & 0x0800) {   /* format 6: LDR Rd,[PC,#imm] */
            uint32_t rd8 = (insn >> 8) & 7;
            uint32_t addr = ((r15(g) + 4) & ~3u) + ((insn & 0xFF) << 2);
            write_reg(g, rd8, gba_read32(g, addr));
            set_pc(g, next); add_cycles(g, 1); return;
        }
        if (insn & 0x0400) {   /* format 5: hi register ops / BX / BLX */
            uint32_t h1 = (insn >> 7) & 1, h2 = (insn >> 6) & 1;
            uint32_t rd2 = (insn & 7) | (h1 << 3);
            uint32_t rs2 = ((insn >> 3) & 7) | (h2 << 3);
            switch ((insn >> 8) & 3) {
            case 0x0: write_reg(g, rd2, read_reg(g, rd2) + read_reg(g, rs2)); break;
            case 0x1: flags_sub3(g, read_reg(g, rd2), read_reg(g, rs2), read_reg(g, rd2) - read_reg(g, rs2), 1); break;
            case 0x2: write_reg(g, rd2, read_reg(g, rs2)); break;
            case 0x3: {
                uint32_t target = read_reg(g, rs2);
                if (insn & 0x0080) {   /* BLX */
                    write_reg(g, 14, (r15(g) + 2) | 1);
                    set_cpsr(g, cpsr & ~CPSR_T);
                    set_pc(g, target & ~3u);
                } else {
                    if (target & 1) set_cpsr(g, cpsr | CPSR_T);
                    else set_cpsr(g, cpsr & ~CPSR_T);
                    set_pc(g, target & ~1u);
                }
                add_cycles(g, 2); return;
            }
            }
            /* ADD/MOV with Rd=PC is a branch: the write above already set
             * the PC, so do not overwrite it with the fall-through value. */
            if (rd2 == 15 && ((insn >> 8) & 3) != 1) { add_cycles(g, 3); return; }
            set_pc(g, next); add_cycles(g, 1); return;
        }
        /* format 4: ALU operations */
        {
            uint32_t aop = (insn >> 6) & 0xF;
            uint32_t a = read_reg(g, rd), b = read_reg(g, rs);
            uint32_t r = 0; bool wr = true;
            switch (aop) {
            case 0x0: r = a & b;  flags_logic(g, r, (cpsr & CPSR_C) ? 1 : 0); break;
            case 0x1: r = a ^ b;  flags_logic(g, r, (cpsr & CPSR_C) ? 1 : 0); break;
            case 0x2: { uint32_t sh = b & 0xFF;
                        if (sh == 0) { r = a; flags_logic(g, r, (cpsr & CPSR_C) ? 1 : 0); }
                        else if (sh < 32) { r = a << sh; flags_logic(g, r, (a >> (32 - sh)) & 1); }
                        else if (sh == 32) { r = 0; flags_logic(g, r, a & 1); }
                        else { r = 0; flags_logic(g, r, 0); } break; }
            case 0x3: { uint32_t sh = b & 0xFF;
                        if (sh == 0) { r = a; flags_logic(g, r, (cpsr & CPSR_C) ? 1 : 0); }
                        else if (sh < 32) { r = a >> sh; flags_logic(g, r, (a >> (sh - 1)) & 1); }
                        else if (sh == 32) { r = 0; flags_logic(g, r, a >> 31); }
                        else { r = 0; flags_logic(g, r, 0); } break; }
            case 0x4: { uint32_t sh = b & 0xFF;
                        if (sh == 0) { r = a; flags_logic(g, r, (cpsr & CPSR_C) ? 1 : 0); }
                        else if (sh < 32) { r = (uint32_t)((int32_t)a >> sh); flags_logic(g, r, (a >> (sh - 1)) & 1); }
                        else { r = (a & 0x80000000u) ? 0xFFFFFFFFu : 0; flags_logic(g, r, a >> 31); } break; }
            case 0x5: { uint32_t c = (cpsr & CPSR_C) ? 1 : 0; r = a + b + c; flags_add3(g, a, b, r, c); break; }
            case 0x6: { uint32_t c = (cpsr & CPSR_C) ? 1 : 0; r = a - b - (1 - c); flags_sub3(g, a, b, r, c); break; }
            case 0x7: { uint32_t sh = b & 0xFF;
                        if (sh == 0) { uint32_t nc = a & 1; r = (a >> 1) | ((cpsr & CPSR_C) ? 0x80000000u : 0); flags_logic(g, r, nc); }
                        else { uint32_t shm = sh & 31; if (shm == 0) { r = a; flags_logic(g, r, a >> 31); }
                               else { r = (a >> shm) | (a << (32 - shm)); flags_logic(g, r, (a >> (shm - 1)) & 1); } } break; }
            case 0x8: flags_logic(g, a & b, (cpsr & CPSR_C) ? 1 : 0); wr = false; break;
            case 0x9: r = 0 - b; flags_sub3(g, 0, b, r, 1); break;
            case 0xA: flags_sub3(g, a, b, a - b, 1); wr = false; break;
            case 0xB: flags_add3(g, a, b, a + b, 0); wr = false; break;
            case 0xC: r = a | b;  flags_logic(g, r, (cpsr & CPSR_C) ? 1 : 0); break;
            case 0xD: r = a * b;  flags_logic(g, r, (cpsr & CPSR_C) ? 1 : 0); break;
            case 0xE: r = a & ~b; flags_logic(g, r, (cpsr & CPSR_C) ? 1 : 0); break;
            case 0xF: r = ~b;     flags_logic(g, r, (cpsr & CPSR_C) ? 1 : 0); break;
            }
            if (wr) write_reg(g, rd, r);
            set_pc(g, next); add_cycles(g, 1); return;
        }
    }

    /* format 7/8: load/store with register offset (0101) */
    if (op == 0x5) {
        bool l = insn & 0x0800, b = insn & 0x0400, s = insn & 0x0200;
        uint32_t addr = read_reg(g, rn) + read_reg(g, rs);
        if (l) {
            if (b) write_reg(g, rd, s ? (uint32_t)(int8_t)gba_read8(g, addr) : gba_read8(g, addr));
            else   write_reg(g, rd, s ? (uint32_t)(int16_t)gba_read16(g, addr) : gba_read32(g, addr));
        } else {
            if (b) gba_write8(g, addr, read_reg(g, rd));
            else if (s) gba_write16(g, addr, read_reg(g, rd));
            else gba_write32(g, addr, read_reg(g, rd));
        }
        set_pc(g, next); add_cycles(g, 1); return;
    }

    /* format 9: load/store with immediate offset (0110/0111) */
    if (op == 0x6 || op == 0x7) {
        bool b = op == 0x7;
        bool l = insn & 0x0800;
        uint32_t off = (insn >> 6) & 0x1F;
        uint32_t addr = read_reg(g, rs) + (b ? off : (off << 2));
        if (l) write_reg(g, rd, b ? gba_read8(g, addr) : gba_read32(g, addr));
        else { if (b) gba_write8(g, addr, read_reg(g, rd)); else gba_write32(g, addr, read_reg(g, rd)); }
        set_pc(g, next); add_cycles(g, 1); return;
    }

    /* format 10: load/store halfword (1000) */
    if (op == 0x8) {
        bool l = insn & 0x0800;
        uint32_t addr = read_reg(g, rs) + (((insn >> 6) & 0x1F) << 1);
        if (l) write_reg(g, rd, gba_read16(g, addr));
        else gba_write16(g, addr, read_reg(g, rd));
        set_pc(g, next); add_cycles(g, 1); return;
    }

    /* format 11: SP-relative load/store (1001) */
    if (op == 0x9) {
        uint32_t rd8 = (insn >> 8) & 7;
        uint32_t addr = read_reg(g, 13) + ((insn & 0xFF) << 2);
        if (insn & 0x0800) write_reg(g, rd8, gba_read32(g, addr));
        else gba_write32(g, addr, read_reg(g, rd8));
        set_pc(g, next); add_cycles(g, 1); return;
    }

    /* format 12: load address (1010) */
    if (op == 0xA) {
        uint32_t rd8 = (insn >> 8) & 7;
        uint32_t off = (insn & 0xFF) << 2;
        if (insn & 0x0800) write_reg(g, rd8, read_reg(g, 13) + off);
        else write_reg(g, rd8, ((r15(g) + 4) & ~3u) + off);
        set_pc(g, next); add_cycles(g, 1); return;
    }

    /* op == 1011: format 13 (SP adjust) or format 14 (push/pop) */
    if (op == 0xB) {
        if (insn & 0x0400) {   /* format 14 */
            if (insn & 0x0800) {   /* POP {..,pc} */
                uint32_t sp = read_reg(g, 13);
                for (int i = 0; i < 8; i++) if (insn & (1 << i)) { write_reg(g, i, gba_read32(g, sp)); sp += 4; }
                if (insn & 0x0100) { uint32_t v = gba_read32(g, sp); sp += 4; write_reg(g, 13, sp); set_pc(g, v & ~1u); add_cycles(g, 3); return; }
                write_reg(g, 13, sp);
            } else {               /* PUSH {..,lr} */
                int n = __builtin_popcount(insn & 0xFF) + ((insn >> 8) & 1);
                uint32_t sp = read_reg(g, 13) - (uint32_t)n * 4;
                write_reg(g, 13, sp);
                for (int i = 0; i < 8; i++) if (insn & (1 << i)) { gba_write32(g, sp, read_reg(g, i)); sp += 4; }
                if (insn & 0x0100) gba_write32(g, sp, read_reg(g, 14));
            }
        } else {               /* format 13: add/sub SP */
            uint32_t off = (insn & 0x7F) << 2;
            if (insn & 0x0080) write_reg(g, 13, read_reg(g, 13) - off);
            else write_reg(g, 13, read_reg(g, 13) + off);
        }
        set_pc(g, next); add_cycles(g, 1); return;
    }

    /* format 15: multiple load/store (1100) */
    if (op == 0xC) {
        uint32_t rn8 = (insn >> 8) & 7;
        uint32_t base = read_reg(g, rn8);
        if (insn & 0x0800) {
            uint32_t addr = base;
            for (int i = 0; i < 8; i++) if (insn & (1 << i)) { write_reg(g, i, gba_read32(g, addr)); addr += 4; }
            if (insn & 0xFF) write_reg(g, rn8, base + (uint32_t)__builtin_popcount(insn & 0xFF) * 4);
        } else {
            uint32_t addr = base;
            for (int i = 0; i < 8; i++) if (insn & (1 << i)) { gba_write32(g, addr, read_reg(g, i)); addr += 4; }
            if (insn & 0xFF) write_reg(g, rn8, base + (uint32_t)__builtin_popcount(insn & 0xFF) * 4);
        }
        set_pc(g, next); add_cycles(g, 1); return;
    }

    /* op == 1101: format 16 (cond branch) or format 17 (SWI) */
    if (op == 0xD) {
        if ((insn & 0x0F00) == 0x0F00) {          /* format 17: SWI */
            gba_swi(g, insn & 0xFF);
            return;
        }
        int cc = (insn >> 8) & 0xF;
        if (cc == 0xF) { set_pc(g, next); add_cycles(g, 1); return; }   /* undefined */
        int32_t off = (int8_t)(insn & 0xFF);
        set_pc(g, cond_holds(cpsr, (uint32_t)cc) ? (r15(g) + 4 + off * 2) : next);
        add_cycles(g, 1); return;
    }

    /* format 18: unconditional branch (1110) */
    if (op == 0xE) {
        int32_t off = (int32_t)((insn & 0x7FF) << 21) >> 20;
        set_pc(g, r15(g) + 4 + off);
        add_cycles(g, 1); return;
    }

    /* format 19: long branch with link (1111) */
    if (op == 0xF) {
        if (insn & 0x0800) {   /* second half: BL */
            int32_t off = (insn & 0x7FF) << 1;
            uint32_t target = read_reg(g, 14) + off;
            write_reg(g, 14, (r15(g) + 2) | 1);
            set_pc(g, target & ~1u);
        } else {               /* first half: set LR */
            int32_t off = (int32_t)((insn & 0x7FF) << 21) >> 9;
            write_reg(g, 14, r15(g) + 4 + off);
            set_pc(g, next);
        }
        add_cycles(g, 2); return;
    }

    set_pc(g, next);
    add_cycles(g, 1);
}

void thumb_exec(GBA *g) {
    thumb_exec_at(g, gba_read16(g, r15(g)));
}

/* ------------------------------------------------------------------ */
/* public loop                                                         */
/* ------------------------------------------------------------------ */
void arm_execute(GBA *g, uint32_t max_cycles) {
    uint32_t start = g->cycle;
    while (g->cycle - start < max_cycles) {
        /* IRQ exception has priority over halt. IF is NOT acknowledged here:
         * the GBA leaves the flag set so the handler can read IE&IF to find
         * the source; the BIOS/handler clears it by writing 1 to IF. */
        if (g->ime && (g->if_reg & g->ie & 0x3FFF) && !(g->cpu.cpsr & CPSR_I)) {
            g->cst.halt = false;   /* an enabled IRQ wakes the CPU from Halt */
            enter_exception(g, 0x12, g->cpu.r[15] + (thumb_mode(g) ? 2 : 4), 0x18);
            continue;
        }
        if (g->cst.halt) {
            /* CPU asleep: just burn cycles until the scanline/frame budget ends */
            g->cycle = start + max_cycles;
            break;
        }
        if (thumb_mode(g)) thumb_exec(g);
        else arm_exec(g);
    }
}

void arm_raise_interrupt(GBA *g, int line) { (void)g; (void)line; }
void arm_set_irq_line(GBA *g, bool high) { g->cst.irq_pending = high; }
bool arm_is_halted(GBA *g) { return g->cst.halt; }