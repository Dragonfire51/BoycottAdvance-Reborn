/*
 * arm.c - ARM7TDMI interpreter (ARM + Thumb) for BoycottAdvance Reborn.
 *
 * Revisado contra GBATEK ("ARM CPU Instruction Set", "THUMB Instruction Set",
 * "ARM CPU Exceptions", "ARM Pseudo Instructions and Directives").
 *
 * Correcciones respecto a v1.4.7 (ver AUDITORIA_GBATEK.md):
 *  - SWI en modo ARM (bits 27-24 = 1111) nunca se ejecutaba.
 *  - Entrada IRQ en Thumb guardaba LR = PC+2 (debe ser siempre PC+4).
 *  - LDR/LDRH/LDRSH desalineados (rotacion), SWP/SWPB, flags de UMULL/SMULL.
 *  - STM con writeback si Rn esta en la lista, lista vacia, base en la lista.
 *  - Thumb formato 8 (STRH/LDSB/LDRH/LDSH) estaba mal decodificado.
 *  - Thumb ROR por registro con cuenta 0, BX Thumb->ARM (alineacion), LDMIA con Rb en lista.
 *  - Condicion NV (0xF) = nunca; ROR por registro multiplo de 32.
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
    if (n == 15) v &= ~1u;
    g->cpu.r[n] = v;
}
static inline uint32_t r15(GBA *g) { return g->cpu.r[15]; }
static inline void set_pc(GBA *g, uint32_t v) { g->cpu.r[15] = v & ~1u; }
static inline uint32_t get_cpsr(GBA *g) { return g->cpu.cpsr; }
static inline void set_cpsr(GBA *g, uint32_t v) { g->cpu.cpsr = v; }

/* GBATEK: al salir de la BIOS, el prefetcher retiene la palabra que la BIOS
   iba a ejecutar a continuacion (PC_BIOS + 8). Una lectura posterior a la BIOS
   desde fuera de ella devuelve ese valor, no el contenido de la memoria. Se
   comprobo contra mGBA: al saltar desde 0x0DC se retiene [0x0E4], desde 0x188
   [0x190], desde 0x134 [0x13C] y desde 0x13C [0x144]. */
static inline void bios_track_fetch(GBA *g) {
    uint32_t pc = g->cpu.r[15];
    if ((pc >> 24) == 0) {
        g->bios_last_pc = pc;
    } else if (g->bios_last_pc != 0xFFFFFFFFu) {
        uint32_t src = (g->bios_last_pc + 8u) & 0x3FFCu;
        g->bios_prefetch = g->bios[src] | ((uint32_t)g->bios[src + 1] << 8) |
                           ((uint32_t)g->bios[src + 2] << 16) |
                           ((uint32_t)g->bios[src + 3] << 24);
        g->bios_last_pc = 0xFFFFFFFFu;
    }
}
static inline void add_cycles(GBA *g, uint32_t n) { g->cycle += n; }
void gba_add_cycles(GBA *g, uint32_t n) { add_cycles(g, n); }

static inline uint32_t rotr(uint32_t v, uint32_t s) {
    s &= 31;
    return s ? (v >> s) | (v << (32 - s)) : v;
}

/* GBATEK: LDR desalineado lee la palabra alineada y la rota (addr&3)*8 bits;
 * LDRH desalineado rota 8 bits; LDRSH desalineado se comporta como LDRSB. */
static uint32_t ldr32(GBA *g, uint32_t addr) {
    return rotr(gba_read32(g, addr & ~3u), (addr & 3) * 8);
}
static uint32_t ldrh16(GBA *g, uint32_t addr) {
    uint32_t v = gba_read16(g, addr & ~1u);
    return (addr & 1) ? rotr(v, 8) : v;
}
static uint32_t ldrsh16(GBA *g, uint32_t addr) {
    if (addr & 1) return (uint32_t)(int32_t)(int8_t)gba_read8(g, addr);
    return (uint32_t)(int32_t)(int16_t)gba_read16(g, addr);
}

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
    if ((uint64_t)a + (uint32_t)(~b) + bin >= 0x100000000ULL) c |= CPSR_C;
    if ((a ^ b) & (a ^ r) & 0x80000000) c |= CPSR_V;
    set_cpsr(g, c);
}

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

/* GBATEK: "Only the SPSR of the current mode can be accessed; In User and
   System modes no SPSR exists." */
static bool has_spsr(uint32_t cpsr) {
    uint32_t m = cpsr & 0x1F;
    return m == 0x11 || m == 0x12 || m == 0x13 || m == 0x17 || m == 0x1B;
}

static void change_mode(GBA *g, uint32_t new_cpsr) {
    /* ARM7TDMI: mode field bit 4 reads back as 1 for the invalid 0xxnnb
       encodings (gba-tests psr: writing 0x03 lands in Supervisor). 0 keeps
       the CPU in its privileged "old" mode rather than dropping to User. */
    if ((new_cpsr & 0x1F) != 0) new_cpsr |= 0x10;
    int ob = bank_of(get_cpsr(g)), nb = bank_of(new_cpsr);
    if (ob == nb) { set_cpsr(g, new_cpsr); return; }
    /* FIQ banks r8-r12 as well as r13/r14 (GBATEK "CPU Registers"). */
    if (ob == BANK_FIQ || nb == BANK_FIQ) {
        for (int i = 0; i < 5; i++) {
            uint32_t t = g->cpu.r[8 + i];
            g->cpu.r[8 + i] = g->cpu.bank[nb][i];
            g->cpu.bank[ob][i] = t;
        }
    }
    g->cpu.bank[ob][5] = g->cpu.r[13];
    g->cpu.bank[ob][6] = g->cpu.r[14];
    set_cpsr(g, new_cpsr);
    g->cpu.r[13] = g->cpu.bank[nb][5];
    g->cpu.r[14] = g->cpu.bank[nb][6];
}

void arm_set_mode(GBA *g, uint32_t mode) {
    change_mode(g, (get_cpsr(g) & ~0x1Fu) | mode);
}

static void enter_exception(GBA *g, uint32_t mode, uint32_t lr_val, uint32_t pc_addr) {
    uint32_t old = get_cpsr(g);
    g->cpu.spsr[bank_of(mode)] = old;               /* SPSR_<mode> = CPSR */
    /* Los vectores se ejecutan en estado ARM con IRQ deshabilitadas. */
    change_mode(g, (old & ~0x3Fu) | mode | CPSR_I);
    g->cpu.r[14] = lr_val;                          /* LR_<mode> */
    set_pc(g, pc_addr);
}

/* Tras restaurar el CPSR desde SPSR el PC debe alinearse segun el estado resultante. */
/* User-bank view of a register, for LDM/STM with the S bit and no PC in the
 * list: the transfer uses the User mode registers, not the current bank
 * (GBATEK "ARM Block Data Transfer"; gba-tests #511/#512). */
static uint32_t user_reg(GBA *g, int k) {
    if (bank_of(get_cpsr(g)) != BANK_USR && k >= 8 && k <= 14)
        return g->cpu.bank[BANK_USR][k - 8];
    return k == 15 ? r15(g) + 12 : g->cpu.r[k];
}
static void set_user_reg(GBA *g, int k, uint32_t v) {
    if (bank_of(get_cpsr(g)) != BANK_USR && k >= 8 && k <= 14) {
        g->cpu.bank[BANK_USR][k - 8] = v;
        return;
    }
    if (k == 15) set_pc(g, v & ~3u);
    else         g->cpu.r[k] = v;
}

static void branch_restore(GBA *g, uint32_t target) {
    set_pc(g, target & (thumb_mode(g) ? ~1u : ~3u));
}

/* ---- shifter operand ------------------------------ */
typedef struct { uint32_t value; uint32_t carry; } Operand;

static void barrel_shift(GBA *g, uint32_t v, uint32_t kind, uint32_t amt, bool reg, Operand *out) {
    uint32_t c = (get_cpsr(g) & CPSR_C) ? 1 : 0;
    /* Desplazamiento por registro con cuenta 0: sin cambios y carry intacto. */
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
        if (amt == 0) {                                   /* solo inmediato: RRX */
            uint32_t newc = v & 1;
            out->value = (v >> 1) | (c ? 0x80000000u : 0);
            out->carry = newc;
            return;
        }
        if ((amt & 31) == 0) { out->value = v; out->carry = v >> 31; }   /* ROR por 32, 64, ... */
        else { amt &= 31; out->value = rotr(v, amt); out->carry = (v >> (amt - 1)) & 1; }
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
            if (rm == 15) v += 4;                    /* con shift por registro PC se lee como +12 */
            uint32_t amt = read_reg(g, rs);
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
    case 0xE: return true;             /* AL */
    default:  return false;            /* NV: reservado, nunca se ejecuta */
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

    /* ---- SWI: bits 27-24 = 1111 (comentario de 24 bit; la BIOS GBA usa bits 23-16) ---- */
    if ((insn & 0x0F000000u) == 0x0F000000u) {
        gba_swi(g, (insn >> 16) & 0xFF);
        return;
    }
    /* ---- branch / branch with link (bits 27-25 = 101) ---- */
    if (op == 0x5) {
        uint32_t off = insn & 0x00FFFFFF;
        if (off & 0x00800000) off |= 0xFF000000;
        if (insn & (1u << 24)) write_reg(g, 14, r15(g) + 4);   /* BL */
        set_pc(g, r15(g) + 8 + (uint32_t)((int32_t)off * 4));
        bus_prefetch_branch(g, r15(g));
        add_cycles(g, 1);
        return;
    }
    /* ---- BX ---- */
    if ((insn & 0x0FFFFFF0) == 0x012FFF10) {
        uint32_t rm = read_reg(g, insn & 0xF);
        if (rm & 1) { set_cpsr(g, get_cpsr(g) | CPSR_T); set_pc(g, rm & ~1u); }
        else { set_cpsr(g, get_cpsr(g) & ~CPSR_T); set_pc(g, rm & ~3u); }
        bus_prefetch_branch(g, r15(g));
        add_cycles(g, 1);
        return;
    }

    /* ---- multiply ---- */
    if ((insn & 0x0FC000F0) == 0x00000090) {   /* MUL/MLA */
        uint32_t rm = read_reg(g, insn & 0xF);
        uint32_t rs = read_reg(g, (insn >> 8) & 0xF);
        bool sacc = (insn >> 21) & 1, ss = (insn >> 20) & 1;
        int mul_rd = (insn >> 16) & 0xF;
        uint32_t r = rm * rs;
        if (sacc) r += read_reg(g, (insn >> 12) & 0xF);
        if (ss) {                          /* MUL{S}/MLA{S}: solo N y Z; C y V intactas (GBATEK) */
            uint32_t c = get_cpsr(g) & ~(CPSR_N | CPSR_Z);
            if (r & 0x80000000) c |= CPSR_N;
            if (r == 0) c |= CPSR_Z;
            set_cpsr(g, c);
        }
        write_reg(g, mul_rd, r);
        set_pc(g, r15(g) + 4);
        add_cycles(g, 2);
        return;
    }
    if ((insn & 0x0F8000F0) == 0x00800090) {   /* UMULL/UMLAL/SMULL/SMLAL */
        uint32_t rm = read_reg(g, insn & 0xF), rs = read_reg(g, (insn >> 8) & 0xF);
        int rshi = (insn >> 16) & 0xF, rlo = (insn >> 12) & 0xF;
        bool acc = (insn >> 21) & 1, sgn = (insn >> 22) & 1;
        uint64_t r = sgn ? (uint64_t)((int64_t)(int32_t)rm * (int64_t)(int32_t)rs)
                         : (uint64_t)rm * rs;
        if (acc) r += ((uint64_t)read_reg(g, rlo)) | ((uint64_t)read_reg(g, rshi) << 32);
        if ((insn >> 20) & 1) {                 /* S: N = bit 63, Z = resultado == 0; C/V no cambian */
            uint32_t c = get_cpsr(g) & ~(CPSR_N | CPSR_Z);
            if (r >> 63) c |= CPSR_N;
            if (r == 0) c |= CPSR_Z;
            set_cpsr(g, c);
        }
        write_reg(g, rlo, (uint32_t)r);
        write_reg(g, rshi, (uint32_t)(r >> 32));
        set_pc(g, r15(g) + 4);
        add_cycles(g, 3);
        return;
    }

    /* ---- SWP / SWPB: 0001 0B00 Rn Rd 0000 1001 Rm ---- */
    if ((insn & 0x0FB00FF0) == 0x01000090) {
        uint32_t addr = read_reg(g, rn);
        uint32_t rm = read_reg(g, insn & 0xF);
        if (b) { uint8_t t = gba_read8(g, addr); gba_write8(g, addr, (uint8_t)rm); write_reg(g, rd, t); }
        else   { uint32_t t = ldr32(g, addr);    gba_write32(g, addr, rm);          write_reg(g, rd, t); }
        set_pc(g, r15(g) + 4);
        add_cycles(g, 4);
        return;
    }

    /* ---- halfword / signed byte transfer ---- */
    if ((insn & 0x0E000090) == 0x00000090) {
        int      hw  = (insn >> 5) & 3;     /* 1 = H, 2 = SB, 3 = SH */
        uint32_t off;
        if (insn & (1 << 22)) off = (((insn >> 8) & 0xF) << 4) | (insn & 0xF);
        else                  off = read_reg(g, insn & 0xF);
        if (!u) off = (uint32_t)(-(int32_t)off);
        uint32_t base = read_reg(g, rn);
        uint32_t addr = p ? base + off : base;
        if (l) {
            uint32_t val;
            if (hw == 1) val = ldrh16(g, addr);
            else if (hw == 2) val = (uint32_t)(int32_t)(int8_t)gba_read8(g, addr);
            else val = ldrsh16(g, addr);
            if (w || !p) write_reg(g, rn, p ? addr : base + off);
            if (rd == 15) { set_pc(g, val & ~3u); add_cycles(g, 3); return; }
            write_reg(g, rd, val);                 /* el valor cargado gana si Rd == Rn */
            set_pc(g, r15(g) + 4);
            add_cycles(g, 2);
            return;
        }
        uint32_t sv = read_reg(g, rd) + (rd == 15 ? 4 : 0);   /* STRH Rd=PC guarda PC+12 */
        if (hw == 1) gba_write16(g, addr, (uint16_t)sv);
        else gba_write8(g, addr, (uint8_t)sv);
        if (w || !p) write_reg(g, rn, p ? addr : base + off);
        set_pc(g, r15(g) + 4);
        add_cycles(g, 2);
        return;
    }

    /* ---- single data transfer (bits 27-26 = 01) ---- */
    if (op == 0x2 || op == 0x3) {
        uint32_t offset;
        if (i) {                               /* I=1: offset en registro desplazado */
            uint32_t rm = insn & 0xF, typ = (insn >> 5) & 3, amt = (insn >> 7) & 0x1F;
            Operand o; barrel_shift(g, read_reg(g, rm), typ, amt, false, &o);
            offset = o.value;
        } else offset = insn & 0xFFF;
        if (!u) offset = (uint32_t)(-(int32_t)offset);
        uint32_t base = read_reg(g, rn);
        uint32_t addr = p ? base + offset : base;
        if (l) {
            uint32_t val = b ? gba_read8(g, addr) : ldr32(g, addr);
            if (w || !p) write_reg(g, rn, p ? addr : base + offset);
            if (rd == 15) { set_pc(g, val & ~3u); add_cycles(g, 3); return; }
            write_reg(g, rd, val);
            add_cycles(g, 2);
            set_pc(g, r15(g) + 4);
            return;
        } else {
            uint32_t sv = read_reg(g, rd) + (rd == 15 ? 4 : 0);   /* STR Rd=PC guarda PC+12 */
            if (b) gba_write8(g, addr, (uint8_t)sv);
            else gba_write32(g, addr, sv);
            add_cycles(g, 2);
        }
        if (w || !p) write_reg(g, rn, p ? addr : base + offset);
        set_pc(g, r15(g) + 4);
        return;
    }

    /* ---- block data transfer (bits 27-25 = 100) ---- */
    if (op == 0x4) {
        uint32_t reglist = insn & 0xFFFF;
        bool sbit = (insn >> 22) & 1;
        bool empty = (reglist == 0);
        int nreg = empty ? 16 : __builtin_popcount(reglist);   /* lista vacia: se transfiere R15 y Rb +/-= 0x40 */
        if (empty) reglist = 1u << 15;
        uint32_t base = read_reg(g, rn);
        uint32_t span = (uint32_t)nreg * 4;
        uint32_t addr;
        /* IA: base, IB: base+4, DA: base-span+4, DB: base-span */
        if (u) addr = p ? base + 4 : base;
        else   addr = p ? base - span : base - span + 4;
        uint32_t newbase = u ? base + span : base - span;
        if (!l) {
            bool first = true;
            for (int k = 0; k < 16; k++) if (reglist & (1u << k)) {
                uint32_t val;
                if (k == rn && w)  val = first ? base : newbase;    /* Rn en la lista: viejo si es el primero, si no el nuevo */
                else if (k == 15)  val = r15(g) + 12;
                else               val = sbit ? user_reg(g, k) : g->cpu.r[k];
                gba_write32(g, addr, val);
                addr += 4; first = false;
            }
            if (w) write_reg(g, rn, newbase);                       /* STM siempre hace writeback */
        } else {
            bool loaded_pc = false; uint32_t pcval = 0;
            for (int k = 0; k < 16; k++) if (reglist & (1u << k)) {
                uint32_t val = gba_read32(g, addr & ~3u);
                if (k == 15) { pcval = val; loaded_pc = true; }
                else if (sbit) set_user_reg(g, k, val);
                else g->cpu.r[k] = val;
                addr += 4;
            }
            if (w && !(reglist & (1u << rn))) write_reg(g, rn, newbase);   /* si Rn esta en la lista gana lo cargado */
            if (loaded_pc) {
                if (sbit) change_mode(g, g->cpu.spsr[bank_of(get_cpsr(g))]);   /* LDM {..pc}^: CPSR = SPSR */
                branch_restore(g, pcval);
                add_cycles(g, (uint32_t)nreg + 3);
                return;
            }
        }
        set_pc(g, r15(g) + 4);
        add_cycles(g, (uint32_t)nreg + 1);
        return;
    }

    /* ---- data processing (bits 27-26 = 00) ---- */
    if (((insn >> 26) & 3) == 0) {
        Operand o = get_operand(g, insn);
        uint32_t opc = (insn >> 21) & 0xF;
        bool     s   = (insn >> 20) & 1;
        uint32_t a   = read_reg(g, rn);
        if (rn == 15 && !(insn & (1 << 25)) && (insn & (1 << 4))) a += 4;   /* Rn=PC con shift por registro: +12 */
        uint32_t bv  = o.value;
        uint32_t res = 0;
        bool carry = o.carry;

        /* PSR transfers (MRS/MSR) */
        if ((insn & 0x0FBF0FFF) == 0x010F0000) {          /* MRS Rd, CPSR/SPSR */
            {
                uint32_t psr = get_cpsr(g);
                if ((insn & 0x00400000) && has_spsr(psr)) psr = g->cpu.spsr[bank_of(psr)];
                write_reg(g, rd, psr);
            }
            set_pc(g, r15(g) + 4); add_cycles(g, 1); return;
        }
        if ((insn & 0x0FB0F000) == 0x0120F000 ||          /* MSR reg: 00010 R 10 */
            (insn & 0x0FB0F000) == 0x0320F000) {          /* MSR imm: 00110 R 10 */
            uint32_t val = o.value;
            uint32_t mask = 0;
            if (insn & 0x00010000) mask |= 0x000000FF;    /* control */
            if (insn & 0x00020000) mask |= 0x0000FF00;    /* extension */
            if (insn & 0x00040000) mask |= 0x00FF0000;    /* status */
            if (insn & 0x00080000) mask |= 0xFF000000;    /* flags */
            if (insn & 0x00400000) {                      /* to SPSR */
                if (has_spsr(get_cpsr(g))) {   /* GBATEK: USR/SYS no tiene SPSR */
                    int bk = bank_of(get_cpsr(g));
                    g->cpu.spsr[bk] = (g->cpu.spsr[bk] & ~mask) | (val & mask);
                }
            } else {                                      /* to CPSR */
                if ((get_cpsr(g) & 0x1F) == MODE_USR) mask &= 0xFF000000u;   /* USR solo escribe flags */
                uint32_t nc = (get_cpsr(g) & ~mask) | (val & mask);
                if (mask & 0xFF) change_mode(g, nc);      /* puede cambiar de modo: banquear r13/r14 */
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
        bool write = !(opc >= 0x8 && opc <= 0xB);
        if (s && rd == 15) {
            /* Rd=PC con S: CPSR = SPSR del modo actual (retorno de excepcion);
               los flags aritmeticos normales no se tocan.  Las formas "malas"
               CMP/CMN/TST/TEQ con Rd=PC (gba-tests #234) tambien cambian de
               modo en el ARM7TDMI, pero no escriben PC. */
            change_mode(g, g->cpu.spsr[bank_of(get_cpsr(g))]);
            if (write) branch_restore(g, res);
            else       set_pc(g, r15(g) + 4);
            add_cycles(g, 3);
            return;
        }
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
        if (write) {
            if (rd == 15) { set_pc(g, res & ~3u); add_cycles(g, 3); return; }
            write_reg(g, rd, res);
        }
        set_pc(g, r15(g) + 4);
        add_cycles(g, (insn & (1 << 4)) && !(insn & (1 << 25)) ? 2 : 1);
        return;
    }

    set_pc(g, r15(g) + 4);
    add_cycles(g, 1);
}

void arm_exec(GBA *g) {
    uint32_t pc = g->cpu.r[15];
    bios_track_fetch(g);
    /* Instruction fetch: on-chip regions cost one cycle (already in the
       per-opcode total below), GamePak ROM is charged its region timing here
       so the prefetch buffer can serve sequential fetches for free and make a
       branch pay the non-sequential cost (GBATEK "GamePak Prefetch"). */
    add_cycles(g, (uint32_t)(bus_fetch_cycles(g, pc) - 1));
    arm_exec_at(g, gba_read32(g, pc));
}

/* ===================================================================== */
/* Thumb mode interpreter (ARMv4T), decodificado por los bits 15-12      */
/* ===================================================================== */
static void thumb_exec_at(GBA *g, uint16_t insn) {
    uint32_t cpsr = get_cpsr(g);
    uint32_t op = insn >> 12;
    uint32_t rd = insn & 7, rs = (insn >> 3) & 7, rn = (insn >> 6) & 7;
    uint32_t next = r15(g) + 2;

    /* formato 2: add/subtract (00011) */
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

    /* formato 1: move shifted register */
    if (op == 0x0 || op == 0x1) {
        uint32_t sh = (insn >> 6) & 0x1F;
        uint32_t kind = (insn >> 11) & 3;
        uint32_t v = read_reg(g, rs);
        uint32_t r = v, carry = (cpsr & CPSR_C) ? 1 : 0;
        if (kind == 0) {                 /* LSL */
            if (sh) { r = v << sh; carry = (v >> (32 - sh)) & 1; }
        } else if (kind == 1) {          /* LSR (#0 = #32) */
            if (sh) { r = v >> sh; carry = (v >> (sh - 1)) & 1; }
            else { r = 0; carry = (v >> 31) & 1; }
        } else {                         /* ASR (#0 = #32) */
            if (sh) { r = (uint32_t)((int32_t)v >> sh); carry = (v >> (sh - 1)) & 1; }
            else { r = (v & 0x80000000u) ? 0xFFFFFFFFu : 0; carry = v >> 31; }
        }
        flags_logic(g, r, carry);
        write_reg(g, rd, r);
        set_pc(g, next); add_cycles(g, 1); return;
    }

    /* formato 3: MOV/CMP/ADD/SUB inmediato */
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

    /* op == 0100: formato 6 (PC-rel), 5 (hi reg/BX) o 4 (ALU) */
    if (op == 0x4) {
        if (insn & 0x0800) {   /* formato 6: LDR Rd,[PC,#imm] */
            uint32_t rd8 = (insn >> 8) & 7;
            uint32_t addr = ((r15(g) + 4) & ~3u) + ((insn & 0xFF) << 2);
            write_reg(g, rd8, gba_read32(g, addr));
            set_pc(g, next); add_cycles(g, 3); return;
        }
        if (insn & 0x0400) {   /* formato 5: hi register ops / BX */
            uint32_t h1 = (insn >> 7) & 1, h2 = (insn >> 6) & 1;
            uint32_t rd2 = (insn & 7) | (h1 << 3);
            uint32_t rs2 = ((insn >> 3) & 7) | (h2 << 3);
            switch ((insn >> 8) & 3) {
            case 0x0: write_reg(g, rd2, read_reg(g, rd2) + read_reg(g, rs2)); break;
            case 0x1: flags_sub3(g, read_reg(g, rd2), read_reg(g, rs2), read_reg(g, rd2) - read_reg(g, rs2), 1); break;
            case 0x2: write_reg(g, rd2, read_reg(g, rs2)); break;
            case 0x3: {
                uint32_t target = read_reg(g, rs2);
                if (target & 1) { set_cpsr(g, cpsr | CPSR_T); set_pc(g, target & ~1u); }
                else            { set_cpsr(g, cpsr & ~CPSR_T); set_pc(g, target & ~3u); }   /* ARM: alinear a palabra */
                add_cycles(g, 3); return;
            }
            }
            /* ADD/MOV con Rd=PC es un salto: write_reg ya fijo el PC */
            if (rd2 == 15 && ((insn >> 8) & 3) != 1) { add_cycles(g, 3); return; }
            set_pc(g, next); add_cycles(g, 1); return;
        }
        /* formato 4: operaciones ALU */
        {
            uint32_t aop = (insn >> 6) & 0xF;
            uint32_t a = read_reg(g, rd), b = read_reg(g, rs);
            uint32_t r = 0; bool wr = true;
            uint32_t cin = (cpsr & CPSR_C) ? 1 : 0;
            switch (aop) {
            case 0x0: r = a & b;  flags_logic(g, r, cin); break;
            case 0x1: r = a ^ b;  flags_logic(g, r, cin); break;
            case 0x2: { uint32_t sh = b & 0xFF;
                        if (sh == 0) { r = a; flags_logic(g, r, cin); }
                        else if (sh < 32) { r = a << sh; flags_logic(g, r, (a >> (32 - sh)) & 1); }
                        else if (sh == 32) { r = 0; flags_logic(g, r, a & 1); }
                        else { r = 0; flags_logic(g, r, 0); } break; }
            case 0x3: { uint32_t sh = b & 0xFF;
                        if (sh == 0) { r = a; flags_logic(g, r, cin); }
                        else if (sh < 32) { r = a >> sh; flags_logic(g, r, (a >> (sh - 1)) & 1); }
                        else if (sh == 32) { r = 0; flags_logic(g, r, a >> 31); }
                        else { r = 0; flags_logic(g, r, 0); } break; }
            case 0x4: { uint32_t sh = b & 0xFF;
                        if (sh == 0) { r = a; flags_logic(g, r, cin); }
                        else if (sh < 32) { r = (uint32_t)((int32_t)a >> sh); flags_logic(g, r, (a >> (sh - 1)) & 1); }
                        else { r = (a & 0x80000000u) ? 0xFFFFFFFFu : 0; flags_logic(g, r, a >> 31); } break; }
            case 0x5: r = a + b + cin; flags_add3(g, a, b, r, cin); break;
            case 0x6: r = a - b - (1 - cin); flags_sub3(g, a, b, r, cin); break;
            case 0x7: { uint32_t sh = b & 0xFF;      /* ROR por registro: cuenta 0 = sin cambios (NO es RRX) */
                        if (sh == 0) { r = a; flags_logic(g, r, cin); }
                        else if ((sh & 31) == 0) { r = a; flags_logic(g, r, a >> 31); }
                        else { uint32_t shm = sh & 31; r = rotr(a, shm); flags_logic(g, r, (a >> (shm - 1)) & 1); } break; }
            case 0x8: flags_logic(g, a & b, cin); wr = false; break;
            case 0x9: r = 0 - b; flags_sub3(g, 0, b, r, 1); break;
            case 0xA: flags_sub3(g, a, b, a - b, 1); wr = false; break;
            case 0xB: flags_add3(g, a, b, a + b, 0); wr = false; break;
            case 0xC: r = a | b;  flags_logic(g, r, cin); break;
            case 0xD: {                                   /* MULS: solo N y Z; C y V sin tocar (GBATEK) */
                        uint32_t mul_r = a * b;
                        uint32_t c = get_cpsr(g) & ~(CPSR_N | CPSR_Z);
                        if (mul_r & 0x80000000) c |= CPSR_N;
                        if (mul_r == 0) c |= CPSR_Z;
                        set_cpsr(g, c);
                        r = mul_r;
                        break; }
            case 0xE: r = a & ~b; flags_logic(g, r, cin); break;
            case 0xF: r = ~b;     flags_logic(g, r, cin); break;
            }
            if (wr) write_reg(g, rd, r);
            set_pc(g, next); add_cycles(g, 1); return;
        }
    }

    /* op == 0101: formato 7 (STR/STRB/LDR/LDRB [Rb,Ro]) y 8 (STRH/LDSB/LDRH/LDSH) */
    if (op == 0x5) {
        uint32_t addr = read_reg(g, rs) + read_reg(g, rn);        /* Rb + Ro */
        if (insn & 0x0200) {                                       /* formato 8: bits 11,10 = H,S */
            switch ((insn >> 10) & 3) {
            case 0: gba_write16(g, addr, (uint16_t)read_reg(g, rd)); break;                       /* STRH */
            case 1: write_reg(g, rd, (uint32_t)(int32_t)(int8_t)gba_read8(g, addr)); break;       /* LDSB */
            case 2: write_reg(g, rd, ldrh16(g, addr)); break;                                     /* LDRH */
            default: write_reg(g, rd, ldrsh16(g, addr)); break;                                   /* LDSH */
            }
        } else {                                                   /* formato 7: bit 11 = L, bit 10 = B */
            bool l = insn & 0x0800, b = insn & 0x0400;
            if (l) write_reg(g, rd, b ? (uint32_t)gba_read8(g, addr) : ldr32(g, addr));
            else if (b) gba_write8(g, addr, (uint8_t)read_reg(g, rd));
            else gba_write32(g, addr, read_reg(g, rd));
        }
        set_pc(g, next); add_cycles(g, 2); return;
    }

    /* formato 9: load/store con offset inmediato (0110/0111) */
    if (op == 0x6 || op == 0x7) {
        bool b = op == 0x7;
        bool l = insn & 0x0800;
        uint32_t off = (insn >> 6) & 0x1F;
        uint32_t addr = read_reg(g, rs) + (b ? off : (off << 2));
        if (l) write_reg(g, rd, b ? (uint32_t)gba_read8(g, addr) : ldr32(g, addr));
        else { if (b) gba_write8(g, addr, (uint8_t)read_reg(g, rd)); else gba_write32(g, addr, read_reg(g, rd)); }
        set_pc(g, next); add_cycles(g, 2); return;
    }

    /* formato 10: load/store halfword (1000) */
    if (op == 0x8) {
        bool l = insn & 0x0800;
        uint32_t addr = read_reg(g, rs) + (((insn >> 6) & 0x1F) << 1);
        if (l) write_reg(g, rd, ldrh16(g, addr));
        else gba_write16(g, addr, (uint16_t)read_reg(g, rd));
        set_pc(g, next); add_cycles(g, 2); return;
    }

    /* formato 11: SP-relative load/store (1001) */
    if (op == 0x9) {
        uint32_t rd8 = (insn >> 8) & 7;
        uint32_t addr = read_reg(g, 13) + ((insn & 0xFF) << 2);
        if (insn & 0x0800) write_reg(g, rd8, ldr32(g, addr));
        else gba_write32(g, addr, read_reg(g, rd8));
        set_pc(g, next); add_cycles(g, 2); return;
    }

    /* formato 12: load address (1010) */
    if (op == 0xA) {
        uint32_t rd8 = (insn >> 8) & 7;
        uint32_t off = (insn & 0xFF) << 2;
        if (insn & 0x0800) write_reg(g, rd8, read_reg(g, 13) + off);
        else write_reg(g, rd8, ((r15(g) + 4) & ~3u) + off);
        set_pc(g, next); add_cycles(g, 1); return;
    }

    /* op == 1011: formato 13 (SP adjust) o 14 (push/pop) */
    if (op == 0xB) {
        if (insn & 0x0400) {   /* formato 14 */
            if (insn & 0x0800) {   /* POP {..,pc} */
                uint32_t sp = read_reg(g, 13);
                if ((insn & 0xFF) == 0 && !(insn & 0x0100)) {
                    /* ARM7TDMI "empty reglist" quirk: POP {} transfiere R15 y
                       suma 0x40 a SP (equivale a LDMIA SP!,{R15} con base += 0x40). */
                    uint32_t v = gba_read32(g, sp);
                    write_reg(g, 13, sp + 0x40);
                    set_pc(g, v & ~1u);
                    add_cycles(g, 4); return;
                }
                for (int k = 0; k < 8; k++) if (insn & (1 << k)) { write_reg(g, k, gba_read32(g, sp)); sp += 4; }
                if (insn & 0x0100) { uint32_t v = gba_read32(g, sp); sp += 4; write_reg(g, 13, sp); set_pc(g, v & ~1u); add_cycles(g, 4); return; }
                write_reg(g, 13, sp);
            } else {               /* PUSH {..,lr} */
                int n = __builtin_popcount(insn & 0xFF) + ((insn >> 8) & 1);
                uint32_t sp = read_reg(g, 13) - (uint32_t)n * 4;
                write_reg(g, 13, sp);
                for (int k = 0; k < 8; k++) if (insn & (1 << k)) { gba_write32(g, sp, read_reg(g, k)); sp += 4; }
                if (insn & 0x0100) gba_write32(g, sp, read_reg(g, 14));
            }
        } else {               /* formato 13: add/sub SP */
            uint32_t off = (insn & 0x7F) << 2;
            if (insn & 0x0080) write_reg(g, 13, read_reg(g, 13) - off);
            else write_reg(g, 13, read_reg(g, 13) + off);
        }
        set_pc(g, next); add_cycles(g, 2); return;
    }

    /* formato 15: multiple load/store (1100) */
    if (op == 0xC) {
        uint32_t rn8 = (insn >> 8) & 7;
        uint32_t base = read_reg(g, rn8);
        uint32_t list = insn & 0xFF;
        if (list == 0) {                        /* lista vacia: se transfiere R15 y Rb += 0x40 */
            if (insn & 0x0800) { uint32_t v = gba_read32(g, base); write_reg(g, rn8, base + 0x40); set_pc(g, v & ~1u); add_cycles(g, 4); return; }
            gba_write32(g, base, r15(g) + 6);
            write_reg(g, rn8, base + 0x40);
            set_pc(g, next); add_cycles(g, 2); return;
        }
        uint32_t newbase = base + (uint32_t)__builtin_popcount(list) * 4;
        uint32_t addr = base;
        if (insn & 0x0800) {                    /* LDMIA */
            for (int k = 0; k < 8; k++) if (list & (1u << k)) { write_reg(g, k, gba_read32(g, addr & ~3u)); addr += 4; }
            if (!(list & (1u << rn8))) write_reg(g, rn8, newbase);   /* si Rb esta en la lista gana lo cargado */
        } else {                                /* STMIA */
            bool first = true;
            for (int k = 0; k < 8; k++) if (list & (1u << k)) {
                uint32_t val = (k == (int)rn8) ? (first ? base : newbase) : read_reg(g, k);
                gba_write32(g, addr, val); addr += 4; first = false;
            }
            write_reg(g, rn8, newbase);
        }
        set_pc(g, next); add_cycles(g, 2); return;
    }

    /* op == 1101: formato 16 (salto condicional) o 17 (SWI) */
    if (op == 0xD) {
        if ((insn & 0x0F00) == 0x0F00) {          /* formato 17: SWI */
            gba_swi(g, insn & 0xFF);
            return;
        }
        int cc = (insn >> 8) & 0xF;
        if (cc == 0xE) { set_pc(g, next); add_cycles(g, 1); return; }   /* condicion 1110: indefinida */
        int32_t off = (int8_t)(insn & 0xFF);
        set_pc(g, cond_holds(cpsr, (uint32_t)cc) ? (r15(g) + 4 + (uint32_t)(off * 2)) : next);
        add_cycles(g, 2); return;
    }

    /* formato 18: salto incondicional (1110) */
    if (op == 0xE) {
        int32_t off = (int32_t)((uint32_t)(insn & 0x7FF) << 21) >> 20;
        set_pc(g, r15(g) + 4 + (uint32_t)off);
        add_cycles(g, 3); return;
    }

    /* formato 19: BL largo (1111) */
    if (op == 0xF) {
        if (insn & 0x0800) {   /* segunda mitad: BL */
            uint32_t off = (uint32_t)(insn & 0x7FF) << 1;
            uint32_t target = read_reg(g, 14) + off;
            write_reg(g, 14, (r15(g) + 2) | 1);
            set_pc(g, target & ~1u);
        } else {               /* primera mitad: LR = PC + 4 + (off << 12) */
            int32_t off = (int32_t)((uint32_t)(insn & 0x7FF) << 21) >> 9;
            write_reg(g, 14, r15(g) + 4 + (uint32_t)off);
            set_pc(g, next);
        }
        add_cycles(g, 3); return;
    }

    set_pc(g, next);
    add_cycles(g, 1);
}

void thumb_exec(GBA *g) {
    uint32_t pc = g->cpu.r[15];
    bios_track_fetch(g);
    add_cycles(g, (uint32_t)(bus_fetch_cycles(g, pc) - 1));
    thumb_exec_at(g, gba_read16(g, pc));
}

/* ------------------------------------------------------------------ */
/* bucle publico                                                       */
void arm_execute(GBA *g, uint32_t max_cycles) {
    uint32_t start = g->cycle;
    while (g->cycle - start < max_cycles) {
        /* SWI 04h/05h (IntrWait/VBlankIntrWait): only the awaited IRQ releases
           the halt.  Unrelated IRQs are ignored by the BIOS wait loop, so the
           game handler must not run while we are waiting (GBATEK SWI 04h/05h). */
        if (g->cst.halt && g->cst.wait_mask) {
            if (g->if_reg & g->cst.wait_mask) {
                uint16_t done = g->cst.wait_mask;
                g->cst.wait_mask = 0;
                g->cst.halt = false;
                /* Leave the awaited flag set: the game's own IRQ/VBlank
                   handler (running after the SWI returns) clears it. */
                (void)done;
            } else {
                g->cycle = start + max_cycles;      /* duerme hasta la IRQ esperada */
                break;
            }
        }

        /* Una CPU en halt despierta en cuanto hay una IRQ habilitada (IE & IF),
           sin importar IME ni el bit I del CPSR; IME/I solo gobiernan el salto al vector. */
        if (g->cst.halt && (g->if_reg & g->ie & 0x3FFF)) g->cst.halt = false;

        /* La IRQ tiene prioridad. IF NO se confirma aqui: el handler lo borra escribiendo 1. */
        if (g->ime && (g->if_reg & g->ie & 0x3FFF) && !(g->cpu.cpsr & CPSR_I)) {
            g->cst.halt = false;
            /* HLE (no BIOS dump): the BIOS dispatcher reads the user handler
               from [0x03FFFFFC] and jumps to it. A ROM that arms an interrupt
               before installing a handler would jump to address 0 and run
               garbage. Acknowledge the pending sources instead. */
            if (!g->bios_loaded && gba_read32(g, 0x03FFFFFC) == 0) {
                g->if_reg = 0;
                continue;
            }
            /* GBATEK (ARM CPU Exceptions): LR_irq = direccion de la siguiente instruccion + 4,
               tambien en estado Thumb (se vuelve con SUBS PC,LR,#4). */
            enter_exception(g, 0x12, g->cpu.r[15] + 4, 0x18);
            add_cycles(g, 3);
            continue;
        }
        if (g->cst.halt) {
            g->cycle = start + max_cycles;       /* CPU dormida: se consume el presupuesto */
            break;
        }
        if (thumb_mode(g)) thumb_exec(g);
        else arm_exec(g);
    }
}

void arm_raise_interrupt(GBA *g, int line) { (void)g; (void)line; }
void arm_set_irq_line(GBA *g, bool high) { g->cst.irq_pending = high; }
bool arm_is_halted(GBA *g) { return g->cst.halt; }
