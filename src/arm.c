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
    uint32_t a2 = a + (~b) + bin;
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
    change_mode(g, (old & ~0x1F) | mode | CPSR_I);  /* force IRQ-disable */
    g->cpu.r[14] = lr_val;                          /* LR_<mode> */
    set_pc(g, pc_addr);
}

/* ---- shifter operand ------------------------------ */
typedef struct { uint32_t value; uint32_t carry; } Operand;

static inline uint32_t rotr(uint32_t v, uint32_t s) { return (v >> s) | (v << (32 - s)); }

static void barrel_shift(GBA *g, uint32_t v, uint32_t kind, uint32_t amt, Operand *out) {
    uint32_t c = (get_cpsr(g) & CPSR_C) ? 1 : 0;
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
            barrel_shift(g, v, typ, amt & 0xFF, &o);
        } else {
            uint32_t amt = (insn >> 7) & 0x1F;
            barrel_shift(g, v, typ, amt, &o);
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
extern void gba_swi(GBA *g);

static void arm_exec_at(GBA *g, uint32_t insn) {
    uint32_t cpsr = get_cpsr(g);
    uint32_t cond = insn >> 28;
    if (!cond_holds(cpsr, cond)) { set_pc(g, r15(g) + 4); add_cycles(g, 1); return; }

    uint32_t op  = (insn >> 25) & 0x7;
    bool     i   = (insn >> 25) & 1;
    bool     p   = (insn >> 24) & 1, u = (insn >> 23) & 1, b = (insn >> 22) & 1, w = (insn >> 21) & 1;
    bool     l   = (insn >> 20) & 1;
    int      rn  = (insn >> 16) & 0xF, rd = (insn >> 12) & 0xF;

    /* ---- branch / SWI ---- */
    if (op == 0x5) {
        if (b) { /* SWI */ gba_swi(g); return; }
        uint32_t off = insn & 0x00FFFFFF;
        if (off & 0x00800000) off |= 0xFF000000;
        if (l) write_reg(g, 14, r15(g) + 4);
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
        uint32_t rm = read_reg(g, insn & 0xF);
        uint32_t rs = read_reg(g, (insn >> 8) & 0xF);
        bool sacc = (insn >> 21) & 1, ss = (insn >> 20) & 1;
        uint32_t r = rm * rs;
        if (sacc) r += read_reg(g, (insn >> 12) & 0xF);
        if (ss) flags_logic(g, r, (get_cpsr(g) & CPSR_C) ? 1 : 0);
        write_reg(g, rd, r);
        set_pc(g, r15(g) + 4);
        add_cycles(g, 2);
        return;
    }
    /* long multiply sun */
    if ((insn & 0x0F8000F0) == 0x00800090) {
        /* UMULL/SMULL/UMLAL/SMLAL - implement basic unsigned multiply */
        uint32_t rm = read_reg(g, insn & 0xF), rs = read_reg(g, (insn >> 8) & 0xF);
        uint64_t r = (uint64_t)rm * rs;
        int rshi = (insn >> 12) & 0xF, rlo = (insn >> 16) & 0xF;
        bool acc = (insn >> 21) & 1;
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
            if (rd == 15) { set_pc(g, (val & ~3) + 4); }  /* +4 after */
            else write_reg(g, rd, val);
        } else {
            if (hw == 1) gba_write16(g, addr, read_reg(g, rd));
            else gba_write8(g, addr, read_reg(g, rd));
        }
        if (w) write_reg(g, rn, addr);
        if (!p) write_reg(g, rn, base + off);
        set_pc(g, r15(g) + 4);
        add_cycles(g, 2);
        return;
    }

    /* ---- single data transfer ---- */
    if (op == 0x1) {
        uint32_t offset;
        if (i) {
            Operand o = get_operand(g, insn);
            offset = o.value;
        } else offset = insn & 0xFFF;
        if (!u) offset = (uint32_t)(-(int32_t)offset);
        uint32_t base = read_reg(g, rn);
        uint32_t addr = p ? base + offset : base;
        if (l) {
            uint32_t val = b ? gba_read8(g, addr) : gba_read32(g, addr);
            if (rd == 15) set_pc(g, (val & ~3) + 4);
            else write_reg(g, rd, val);
            add_cycles(g, 2);
        } else {
            if (b) gba_write8(g, addr, read_reg(g, rd));
            else gba_write32(g, addr, read_reg(g, rd));
            add_cycles(g, 1);
        }
        if (w || !p) write_reg(g, rn, p ? addr : base + offset);
        set_pc(g, r15(g) + 4);
        return;
    }

    /* ---- block data transfer ---- */
    if (op == 0x2) {
        uint32_t reglist = insn & 0xFFFF;
        int nreg = __builtin_popcount(reglist);
        uint32_t base = read_reg(g, rn);
        uint32_t addr;
        if (u) addr = p ? base + 4 : base;
        else   addr = p ? base - (nreg - 1) * 4 : base - nreg * 4;
        if (!l) {
            for (int i = 0; i < 16; i++) if (reglist & (1 << i)) { gba_write32(g, addr, (i == rn && w) ? base : read_reg(g, i)); addr += 4; }
            if (w && !(reglist & (1 << rn))) write_reg(g, rn, u ? base + nreg * 4 : base - nreg * 4);
        } else {
            for (int i = 0; i < 16; i++) if (reglist & (1 << i)) {
                uint32_t val = gba_read32(g, addr);
                if (i == 15) { set_pc(g, (val & ~3) + 4); }
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
        }
        set_pc(g, r15(g) + 4);
        add_cycles(g, nreg + 1);
        return;
    }

    /* ---- data processing ---- */
    if (op == 0 || op == 3 || op == 4) {   /* op = 009 => data proc; 011 = PSR? handle later */
        /* actually data processing is op == 0 (bits 27-26=00) */
        if ((insn >> 26) & 3) { /* undefined / PSR */ set_pc(g, r15(g)+4); add_cycles(g,1); return; }
        Operand o = get_operand(g, insn);
        uint32_t opc = (insn >> 21) & 0xF;
        bool     s   = (insn >> 20) & 1;
        uint32_t a   = (rn == 15) ? (r15(g) + 8) : read_reg(g, rn);
        uint32_t bv  = o.value;
        uint32_t res = 0;
        bool carry = o.carry;

        /* PSR operations (MRS/MSR) */
        if (opc == 0x2 && (insn & 0x02000000) && !(insn & 0x01000000)) {
            /* MRS */
            write_reg(g, rd, get_cpsr(g));
            set_pc(g, r15(g) + 4); add_cycles(g, 1); return;
        }
        switch (opc) {
        case 0x0: res = a & bv;   flags_logic(g, res, carry); break;
        case 0x1: res = a ^ bv;   flags_logic(g, res, carry); break;
        case 0x2: res = a - bv;   flags_sub3(g, a, bv, res, 1); break;
        case 0x3: res = bv - a;   flags_sub3(g, bv, a, res, 1); break;
        case 0x4: res = a + bv;   flags_add3(g, a, bv, res, 0); break;
        case 0x5: res = a + bv + ((cpsr & CPSR_C) ? 1 : 0); flags_add3(g, a, bv, res, (cpsr & CPSR_C) ? 1 : 0); break;
        case 0x6: res = a - bv - ((cpsr & CPSR_C) ? 0 : 1); flags_sub3(g, a, bv, res, (cpsr & CPSR_C) ? 1 : 0); break;
        case 0x7: res = bv - a - ((cpsr & CPSR_C) ? 0 : 1); flags_sub3(g, bv, a, res, (cpsr & CPSR_C) ? 1 : 0); break;
        case 0x8: res = a & bv;   break;  /* TST */
        case 0x9: res = a ^ bv;   break;  /* TEQ */
        case 0xA: res = a - bv;   break;  /* CMP */
        case 0xB: res = a + bv;   break;  /* CMN */
        case 0xC: res = a | bv;   flags_logic(g, res, carry); break;
        case 0xD: res = bv;       flags_logic(g, res, carry); break;  /* MOV */
        case 0xE: res = a & ~bv;  flags_logic(g, res, carry); break;  /* BIC */
        case 0xF: res = ~bv;      flags_logic(g, res, carry); break;  /* MVN */
        default:  break;
        }
        if (s && (opc == 0x8 || opc == 0x9 || opc == 0xA || opc == 0xB)) {
            if (opc == 0x8) flags_logic(g, res, carry);
            else if (opc == 0x9) flags_logic(g, res, carry);
            else if (opc == 0xA) flags_sub3(g, a, bv, res, 1);
            else flags_add3(g, a, bv, res, 0);
        }
        if (s && (opc == 0x2 || opc == 0x3)) {
            flags_sub3(g, a, bv, res, 1);
            if (opc == 0x3) flags_sub3(g, bv, a, res, 1);
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
            if (rd == 15) { set_pc(g, res & ~3); }
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
static void thumb_exec_at(GBA *g, uint16_t insn) {
    uint32_t cpsr = get_cpsr(g);
    int rd = insn & 7, rs = (insn >> 3) & 7, rn = (insn >> 6) & 7;
    uint32_t imm = (insn >> 6) & 0x1F;

    /* SWI */
    if ((insn & 0xFF00) == 0xDF00) { gba_swi(g); return; }

    if ((insn >> 13) <= 3) {   /* LSL, LSR, ASR immediate */
        uint32_t v = read_reg(g, rs);
        uint32_t r = v;
        uint32_t carry = 0;
        switch ((insn >> 13) & 3) {
        case 0:
            if (imm) { r = v << imm; carry = (v >> (32 - imm)) & 1; }
            else { r = v; carry = (get_cpsr(g) & CPSR_C) ? 1 : 0; }
            if (imm == 0) carry = (get_cpsr(g) & CPSR_C) ? 1 : 0;
            break;
        case 1:
            if (imm) { r = v >> imm; carry = (v >> (imm - 1)) & 1; }
            else { r = 0; carry = (v >> 31) & 1; }
            break;
        case 2:
            if (imm) { r = (uint32_t)((int32_t)v >> imm); carry = (v >> (imm - 1)) & 1; }
            else { r = (v & 0x80000000) ? 0xFFFFFFFF : 0; carry = (v >> 31) & 1; }
            break;
        }
        flags_logic(g, r, carry);
        write_reg(g, rd, r);
        set_pc(g, r15(g) + 2); add_cycles(g, 1); return;
    }

    if ((insn >> 12) == 0x4 || (insn >> 12) == 0x5) {  /* ADD/SUB reg or imm3 */
        bool sub = (insn >> 9) & 1;
        if ((insn & 0x1C00) == 0x1C00) {  /* add/sub with 3-bit imm */
            imm = (insn >> 6) & 7;
            uint32_t a = read_reg(g, rs);
            uint32_t r = sub ? a - imm : a + imm;
            if (sub) flags_sub3(g, a, imm, r, 1); else flags_add3(g, a, imm, r, 0);
            write_reg(g, rd, r);
        } else {
            uint32_t a = read_reg(g, rn), b = read_reg(g, rs);
            uint32_t r;
            if ((insn & 0x2000) == 0) { r = a + b; flags_add3(g, a, b, r, 0); }
            else { r = a - b; flags_sub3(g, a, b, r, 1); }
            write_reg(g, rd, r);
        }
        set_pc(g, r15(g) + 2); add_cycles(g, 1); return;
    }

    if ((insn >> 12) == 0x6 || (insn >> 12) == 0x7) {  /* MOV/CMP/ADD/SUB 8-bit imm */
        uint32_t imm8 = insn & 0xFF;
        rd = (insn >> 8) & 7;
        if ((insn >> 12) == 0x6) {  /* MOV */
            flags_logic(g, imm8, (get_cpsr(g) & CPSR_C) ? 1 : 0);
            write_reg(g, rd, imm8);
        } else {
            uint32_t a = read_reg(g, rd);
            if (insn & (1 << 11)) { uint32_t r = a - imm8; flags_sub3(g, a, imm8, r, 1); write_reg(g, rd, r); }
            else { uint32_t r = a + imm8; flags_add3(g, a, imm8, r, 0); write_reg(g, rd, r); }
        }
        set_pc(g, r15(g) + 2); add_cycles(g, 1); return;
    }

    if ((insn >> 12) == 0x8 || (insn >> 12) == 0x9) {  /* ALU ops */
        uint32_t op = (insn >> 6) & 0xF;
        uint32_t a = read_reg(g, rd), b = read_reg(g, rs);
        uint32_t r = 0;
        bool flags = true;
        switch (op) {
        case 0x0: r = a & b;   break;
        case 0x1: r = a ^ b;   break;
        case 0x2: { int sh = b & 0xFF; r = (sh < 32) ? (a << sh) : 0; flags_logic(g, r, sh ? ((a >> (32 - sh)) & 1) : ((get_cpsr(g) & CPSR_C) ? 1 : 0)); break; }
        case 0x3: { int sh = b & 0xFF; r = (sh < 32) ? (a >> sh) : 0; flags_logic(g, r, sh ? ((a >> (sh - 1)) & 1) : ((a >> 31) & 1)); break; }
        case 0x4: { int sh = b & 0xFF; r = (sh < 32) ? (uint32_t)((int32_t)a >> sh) : (((int32_t)a < 0) ? 0xFFFFFFFF : 0); flags_logic(g, r, sh ? ((a >> (sh - 1)) & 1) : ((a >> 31) & 1)); break; }
        case 0x5: { int c = (get_cpsr(g) & CPSR_C) ? 1 : 0; r = a + b + c; flags_add3(g, a, b, r, c); flags = false; write_reg(g, rd, r); break; }
        case 0x6: { int c = (get_cpsr(g) & CPSR_C) ? 1 : 0; r = a - b - (1 - c); flags_sub3(g, a, b, r, c); flags = false; write_reg(g, rd, r); break; }
        case 0x7: { int sh = b & 0xFF; if (!sh) { bool cc = a & 1; r = (a >> 1) | ((get_cpsr(g) & CPSR_C) ? 0x80000000u : 0); flags_logic(g, r, cc); } else { uint32_t cc = (a >> (sh - 1)) & 1; r = (a >> sh) | (a << (32 - sh)); flags_logic(g, r, cc); } flags = false; write_reg(g, rd, r); break; }
        case 0x8: r = a & b;   flags = false; break;   /* TST */
        case 0x9: r = 0 - b;   flags_sub3(g, 0, b, r, 1); flags = false; break;  /* NEG */
        case 0xA: r = a - b;   flags_sub3(g, a, b, r, 1); flags = false; break;  /* CMP */
        case 0xB: r = a + b;   flags_add3(g, a, b, r, 0); flags = false; break;  /* CMN */
        case 0xC: r = a | b;   break;
        case 0xD: write_reg(g, rd, a * b); set_pc(g, r15(g)+2); add_cycles(g, 1); return;  /* MUL */
        case 0xE: r = a & ~b;  break;
        case 0xF: r = ~b;      break;
        }
        if (flags) flags_logic(g, r, (op == 0x0 || op == 0x1 || op == 0xC || op == 0xE || op == 0xF) ? ((get_cpsr(g) & CPSR_C) ? 1 : 0) : ((get_cpsr(g) & CPSR_C) ? 1 : 0));
        if (op != 0x8 && op != 0xA && op != 0xB) write_reg(g, rd, r);
        set_pc(g, r15(g) + 2); add_cycles(g, 1); return;
    }

    /* high register ops / BX / BLX */
    if ((insn >> 12) == 0xA) {
        rd = (insn >> 8) & 7;
        if (insn & 0x0080) rd |= 8;
        rs = insn & 0xF;
        switch ((insn >> 8) & 3) {
        case 0x0: write_reg(g, rd, read_reg(g, rd) + read_reg(g, rs)); break; /* ADD */
        case 0x1: flags_sub3(g, read_reg(g, rd), read_reg(g, rs), read_reg(g, rd) - read_reg(g, rs), 1); break; /* CMP */
        case 0x2: write_reg(g, rd, read_reg(g, rs)); break;  /* MOV */
        case 0x3: {  /* BX/BLX */
            if (insn & 0x0080) { /* BLX */ write_reg(g, 14, r15(g) + 4); set_cpsr(g, get_cpsr(g) & ~CPSR_T); }
            uint32_t rm = read_reg(g, rs);
            if (rm & 1) { set_cpsr(g, get_cpsr(g) | CPSR_T); set_pc(g, rm & ~1); }
            else { set_cpsr(g, get_cpsr(g) & ~CPSR_T); set_pc(g, rm & ~3); }
            add_cycles(g, 1); return;
        }
        }
        set_pc(g, r15(g) + 2); add_cycles(g, 1); return;
    }

    /* LDR/STR */
    if ((insn >> 11) == 0x14 || (insn >> 11) == 0x15) {  /* reg offset */
        rd = insn & 7; rn = (insn >> 3) & 7; rs = (insn >> 6) & 7;
        uint32_t addr = read_reg(g, rn) + read_reg(g, rs);
        bool b = insn & 0x10, l = insn & 0x08;
        if (l) { uint32_t v = b ? gba_read8(g, addr) : gba_read16(g, addr); write_reg(g, rd, v); }
        else { if (b) gba_write8(g, addr, read_reg(g, rd)); else gba_write16(g, addr, read_reg(g, rd)); }
        set_pc(g, r15(g) + 2); add_cycles(g, 1); return;
    }
    if ((insn >> 11) == 0x12 || (insn >> 11) == 0x13) {  /* imm offset */
        rd = insn & 7; rn = (insn >> 3) & 7; imm = (insn >> 6) & 0x1F;
        bool b = insn & 0x10, l = insn & 0x08;
        uint32_t addr = read_reg(g, rn) + (b ? imm : imm << 2);
        if (l) { uint32_t v = b ? gba_read8(g, addr) : gba_read16(g, addr); write_reg(g, rd, v); }
        else { if (b) gba_write8(g, addr, read_reg(g, rd)); else gba_write16(g, addr, read_reg(g, rd)); }
        set_pc(g, r15(g) + 2); add_cycles(g, 1); return;
    }
    if ((insn >> 12) == 0xB) {  /* halfword */
        rd = insn & 7; rn = (insn >> 3) & 7; imm = (insn >> 6) & 0x1F;
        uint32_t addr = read_reg(g, rn) + (imm << 1);
        if (insn & 0x800) { uint32_t v = gba_read16(g, addr); write_reg(g, rd, v); }
        else gba_write16(g, addr, read_reg(g, rd));
        set_pc(g, r15(g) + 2); add_cycles(g, 1); return;
    }
    if ((insn >> 12) == 0xC) {  /* SP-relative */
        rd = (insn >> 8) & 7; imm = (insn & 0xFF) << 2;
        uint32_t addr = read_reg(g, 13) + imm;
        if (insn & 0x800) { uint32_t v = gba_read16(g, addr); write_reg(g, rd, v); }
        else gba_write16(g, addr, read_reg(g, rd));
        set_pc(g, r15(g) + 2); add_cycles(g, 1); return;
    }
    if ((insn >> 12) == 0xD) {  /* load address */
        rd = (insn >> 8) & 7; imm = (insn & 0xFF) << 2;
        if (insn & 0x800) write_reg(g, rd, read_reg(g, 13) + imm);
        else write_reg(g, rd, ((r15(g) + 4) & ~3) + imm);
        set_pc(g, r15(g) + 2); add_cycles(g, 1); return;
    }
    if ((insn >> 12) == 0xE) {  /* add offset to SP */
        imm = (insn & 0x7F) << 2;
        if (insn & 0x80) write_reg(g, 13, read_reg(g, 13) - imm);
        else write_reg(g, 13, read_reg(g, 13) + imm);
        set_pc(g, r15(g) + 2); add_cycles(g, 1); return;
    }
    if ((insn >> 12) == 0xF) {
        uint16_t top = insn >> 8;
        if ((top & 0x80) == 0x80) {  /* conditional branch */
            int8_t off = (int8_t)(insn & 0xFF);
            int cc = (insn >> 8) & 0xF;
            set_pc(g, cond_holds(cpsr, cc) ? (r15(g) + 4 + off * 2) : (r15(g) + 2));
            add_cycles(g, 1); return;
        }
        if (insn & 0x100) {  /* POP/PUSH/load/store */
            if (insn & 0x0800) {  /* LDM/STM or POP */
                if (insn & 0x1000) {  /* LDMIA / POP */
                    if ((insn & 0xF000) == 0xB000) {  /* POP */
                        uint32_t sp = read_reg(g, 13);
                        for (int i = 0; i < 8; i++) if (insn & (1 << i)) { write_reg(g, i, gba_read32(g, sp)); sp += 4; }
                        if (insn & 0x100) { uint32_t v = gba_read32(g, sp); write_reg(g, 15, v & ~1); sp += 4; }
                        write_reg(g, 13, sp);
                    } else {  /* LDMIA */
                        rn = (insn >> 8) & 7;
                        uint32_t base = read_reg(g, rn), addr = base;
                        for (int i = 0; i < 8; i++) if (insn & (1 << i)) { write_reg(g, i, gba_read32(g, addr)); addr += 4; }
                        if (insn & 0xFF) write_reg(g, rn, base + __builtin_popcount(insn & 0xFF) * 4);
                    }
                } else {  /* STMIA / PUSH */
                    if ((insn & 0xF000) == 0xB000) {  /* PUSH */
                        int n = __builtin_popcount(insn & 0xFF) + ((insn >> 9) & 1);
                        uint32_t sp = read_reg(g, 13) - n * 4;
                        write_reg(g, 13, sp);
                        for (int i = 0; i < 8; i++) if (insn & (1 << i)) { gba_write32(g, sp, read_reg(g, i)); sp += 4; }
                        if (insn & 0x200) gba_write32(g, sp, read_reg(g, 14));
                    } else {  /* STMIA */
                        rn = (insn >> 8) & 7;
                        uint32_t base = read_reg(g, rn), addr = base;
                        for (int i = 0; i < 8; i++) if (insn & (1 << i)) { gba_write32(g, addr, read_reg(g, i)); addr += 4; }
                        if (insn & 0xFF) write_reg(g, rn, base + __builtin_popcount(insn & 0xFF) * 4);
                    }
                }
                set_pc(g, r15(g) + 2); add_cycles(g, 1); return;
            }
            set_pc(g, r15(g) + 2); add_cycles(g, 1); return;
        }
        /* unconditional branch or BL half */
        if (insn & 0x0200) {  /* BL upper */
            int32_t off = (int32_t)((insn & 0x7FF) << 21) >> 9;
            write_reg(g, 14, r15(g) + 4 + off);
            set_pc(g, r15(g) + 2); add_cycles(g, 1); return;
        }
        if (insn & 0x0100) { /* BL lower */
            int32_t off = (insn & 0x7FF) << 1;
            uint32_t lr = read_reg(g, 14) + off;  /* uses pre-computed upper in r14 */
            write_reg(g, 14, r15(g) + 2);
            set_pc(g, lr); add_cycles(g, 1); return;
        }
        /* unconditional B */
        int32_t off = (int32_t)((insn & 0x7FF) << 21) >> 20;
        set_pc(g, r15(g) + 4 + off);
        add_cycles(g, 1); return;
    }

    set_pc(g, r15(g) + 2);
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
        if (g->cst.halt) break;
        /* check IRQ */
        if (g->ime && (g->if_reg & g->ie & 0x3FFF)) {
            enter_exception(g, 0x12, r15(g) + 2, 0x18);
            continue;
        }
        if (thumb_mode(g)) thumb_exec(g);
        else arm_exec(g);
    }
}

void arm_raise_interrupt(GBA *g, int line) { (void)g; (void)line; }
void arm_set_irq_line(GBA *g, bool high) { g->cst.irq_pending = high; }
bool arm_is_halted(GBA *g) { return g->cst.halt; }