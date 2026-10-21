/*
 * apu.c - GBA sound: 4 canales PSG + 2 FIFOs de Direct Sound (GBATEK "GBA Sound Controller").
 *
 * Diseno:
 *  - apu_step() lo llama el nucleo con los ciclos de CPU ejecutados y genera
 *    muestras a 44100 Hz en un ring buffer (productor = hilo de emulacion).
 *  - apu_mix() es el callback de SDL (consumidor): solo copia del ring buffer.
 *  - Los FIFOs avanzan con los desbordamientos de TIMER0/TIMER1 y piden datos
 *    por DMA (timing 3) cuando quedan <= 16 bytes, como el hardware.
 *  - Las envolventes/longitud/sweep van por un frame sequencer de 512 Hz.
 */
#include "gba.h"
#include "mem.h"
#include <stdatomic.h>
#include <string.h>

#define CPU_HZ   16777216ULL
/* ciclos de CPU por muestra de salida en punto fijo 16.16 */
#define STEP_FP  ((CPU_HZ << 16) / SAMPLE_RATE)

static const uint8_t DUTY[4] = { 0x01, 0x81, 0x87, 0x7E };   /* MSB = primer paso */

/* ---------------------------------------------------------------- ring buffer */
#define RING_FRAMES 8192u                   /* potencia de 2 */
static int16_t ring[RING_FRAMES * 2];
static atomic_uint ring_w, ring_r;
static int16_t last_l, last_r;

static void ring_push(int16_t l, int16_t r) {
    unsigned w = atomic_load_explicit(&ring_w, memory_order_relaxed);
    unsigned rd = atomic_load_explicit(&ring_r, memory_order_acquire);
    if (w - rd >= RING_FRAMES) return;                      /* lleno: se descarta */
    ring[(w & (RING_FRAMES - 1)) * 2]     = l;
    ring[(w & (RING_FRAMES - 1)) * 2 + 1] = r;
    atomic_store_explicit(&ring_w, w + 1, memory_order_release);
}

void apu_mix(GBA *g, int16_t *stream, int nframes) {
    (void)g;
    unsigned rd = atomic_load_explicit(&ring_r, memory_order_relaxed);
    unsigned w  = atomic_load_explicit(&ring_w, memory_order_acquire);
    for (int i = 0; i < nframes; i++) {
        if (rd != w) {
            last_l = ring[(rd & (RING_FRAMES - 1)) * 2];
            last_r = ring[(rd & (RING_FRAMES - 1)) * 2 + 1];
            rd++;
        }                                                   /* vacio: se repite la ultima muestra */
        stream[i * 2]     = last_l;
        stream[i * 2 + 1] = last_r;
    }
    atomic_store_explicit(&ring_r, rd, memory_order_release);
}

void apu_open(void) { }
void apu_close(void) { }

void apu_reset(GBA *g) {
    memset(&g->apu, 0, sizeof(g->apu));
    g->apu.bias = 0x0200;                                   /* SOUNDBIAS por defecto */
    g->apu.lfsr = 0x7FFF;
}

/* ---------------------------------------------------------------- FIFOs */
void apu_fifo_write(GBA *g, int ch, const uint8_t *bytes, int nbytes) {
    Apu *a = &g->apu;
    ch = ch ? 1 : 0;
    for (int i = 0; i < nbytes; i++) {
        if (a->fifo_len[ch] >= 32) break;                   /* FIFO lleno */
        a->fifo[ch][(a->fifo_head[ch] + a->fifo_len[ch]) & 31] = (int8_t)bytes[i];
        a->fifo_len[ch]++;
    }
}

/* Un timer desbordo n veces: los FIFOs que lo usan sacan n muestras. */
void apu_timer_overflow(GBA *g, int timer, uint32_t n) {
    Apu *a = &g->apu;
    for (int f = 0; f < 2; f++) {
        int sel = (a->cnt_h >> (f ? 14 : 10)) & 1;
        if (sel != timer) continue;
        for (uint32_t k = 0; k < n; k++) {
            if (a->fifo_len[f] > 0) {
                a->fifo_held[f] = a->fifo[f][a->fifo_head[f]];
                a->fifo_head[f] = (a->fifo_head[f] + 1) & 31;
                a->fifo_len[f]--;
            }
            if (a->fifo_len[f] <= 16) dma_fifo_request(g, f);
        }
    }
}

/* ---------------------------------------------------------------- registros */
static uint16_t ch_lenflag(const Apu *a, int i) {
    switch (i) {
    case 0: return a->r1x & 0x4000;
    case 1: return a->r2h & 0x4000;
    case 2: return a->r3x & 0x4000;
    default: return a->r4h & 0x4000;
    }
}

static void trigger_env(ApuCh *c, uint16_t envreg) {
    c->vol = (envreg >> 12) & 0xF;
    c->env_t = (envreg >> 8) & 7;
    if (!c->env_t) c->env_t = 8;
}

static void trigger_sq1(Apu *a) {
    ApuCh *c = &a->ch[0];
    c->on = (a->r1h & 0xF800) != 0;                         /* DAC apagado si vol=0 y dir=decrece */
    trigger_env(c, a->r1h);
    if (c->len == 0) c->len = 64;
    c->pos = 0; c->cc = 0;
    a->shadow = a->r1x & 0x7FF;
    int per = (a->r1l >> 4) & 7, sh = a->r1l & 7;
    a->sweep_t = per ? per : 8;
    a->sweep_on = per || sh;
    if (sh) {
        int d = a->shadow >> sh;
        int next = (a->r1l & 8) ? a->shadow - d : a->shadow + d;
        if (next > 2047) c->on = false;
    }
}
static void trigger_sq2(Apu *a) {
    ApuCh *c = &a->ch[1];
    c->on = (a->r2l & 0xF800) != 0;
    trigger_env(c, a->r2l);
    if (c->len == 0) c->len = 64;
    c->pos = 0; c->cc = 0;
}
static void trigger_wave(Apu *a) {
    ApuCh *c = &a->ch[2];
    c->on = (a->r3l & 0x80) != 0;
    if (c->len == 0) c->len = 256;
    c->pos = 0; c->cc = 0;
}
static void trigger_noise(Apu *a) {
    ApuCh *c = &a->ch[3];
    c->on = (a->r4l & 0xF800) != 0;
    trigger_env(c, a->r4l);
    if (c->len == 0) c->len = 64;
    c->cc = 0;
    a->lfsr = 0x7FFF;
}

uint16_t apu_read_reg(GBA *g, uint32_t reg) {
    Apu *a = &g->apu;
    switch (reg) {
    case 0x060: return a->r1l & 0x007F;
    case 0x062: return a->r1h & 0xFFC0;          /* longitud (bits 0-5) es solo escritura */
    case 0x064: return a->r1x & 0x4000;          /* solo el flag de longitud se lee */
    case 0x068: return a->r2l & 0xFFC0;
    case 0x06C: return a->r2h & 0x4000;
    case 0x070: return a->r3l & 0x00E0;
    case 0x072: return a->r3h & 0xE000;
    case 0x074: return a->r3x & 0x4000;
    case 0x078: return a->r4l & 0xFF00;
    case 0x07C: return a->r4h & 0x40FF;
    case 0x080: return a->cnt_l & 0xFF77;
    case 0x082: return a->cnt_h & 0x770F;        /* los bits de reset de FIFO leen 0 */
    case 0x084: {                                /* SOUNDCNT_X: bit 7 maestro, bits 0-3 canal activo */
        uint16_t v = a->master ? 0x80 : 0;
        for (int i = 0; i < 4; i++) if (a->ch[i].on) v |= (uint16_t)(1 << i);
        return v;
    }
    case 0x088: return a->bias;
    default:
        if (reg >= 0x090 && reg <= 0x09E) {      /* WAVE_RAM: el CPU ve el banco que NO suena */
            int bank = ((a->r3l >> 6) & 1) ^ 1;
            int i = (int)((reg - 0x090) & 0xE);
            return (uint16_t)(a->wave[bank][i] | (a->wave[bank][i + 1] << 8));
        }
        return 0;
    }
}

void apu_write_reg(GBA *g, uint32_t reg, uint16_t v) {
    Apu *a = &g->apu;

    if (reg >= 0x090 && reg <= 0x09E) {
        int bank = ((a->r3l >> 6) & 1) ^ 1;
        int i = (int)((reg - 0x090) & 0xE);
        a->wave[bank][i] = (uint8_t)v; a->wave[bank][i + 1] = (uint8_t)(v >> 8);
        return;
    }
    switch (reg) {
    case 0x082:
        a->cnt_h = v & 0x770F;
        if (v & 0x0800) { a->fifo_head[0] = a->fifo_len[0] = 0; a->fifo_held[0] = 0; }   /* reset FIFO A */
        if (v & 0x8000) { a->fifo_head[1] = a->fifo_len[1] = 0; a->fifo_held[1] = 0; }   /* reset FIFO B */
        return;
    case 0x084:
        a->master = (v & 0x80) != 0;
        if (!a->master) {                        /* apagar el chip borra los registros PSG */
            a->r1l = a->r1h = a->r1x = a->r2l = a->r2h = 0;
            a->r3l = a->r3h = a->r3x = a->r4l = a->r4h = 0;
            a->cnt_l = 0;
            for (int i = 0; i < 4; i++) a->ch[i].on = false;
        }
        return;
    case 0x088: a->bias = v & 0xC3FE; return;
    }
    if (!a->master) return;                      /* con el maestro apagado los registros PSG ignoran escrituras */

    switch (reg) {
    case 0x060: a->r1l = v & 0x7F; break;
    case 0x062: a->r1h = v; a->ch[0].len = 64 - (v & 63); break;
    case 0x064: a->r1x = v & 0x47FF; if (v & 0x8000) trigger_sq1(a); break;
    case 0x068: a->r2l = v; a->ch[1].len = 64 - (v & 63); break;
    case 0x06C: a->r2h = v & 0x47FF; if (v & 0x8000) trigger_sq2(a); break;
    case 0x070:
        a->r3l = v & 0xE0;
        if (!(v & 0x80)) a->ch[2].on = false;
        break;
    case 0x072: a->r3h = v & 0xE0FF; a->ch[2].len = 256 - (v & 0xFF); break;
    case 0x074: a->r3x = v & 0x47FF; if (v & 0x8000) trigger_wave(a); break;
    case 0x078: a->r4l = v; a->ch[3].len = 64 - (v & 63); break;
    case 0x07C: a->r4h = v & 0xC0FF; if (v & 0x8000) trigger_noise(a); break;
    case 0x080: a->cnt_l = v & 0xFF77; break;
    default: break;
    }
}

/* ---------------------------------------------------------------- frame sequencer */
static void clock_length(Apu *a) {
    for (int i = 0; i < 4; i++) {
        ApuCh *c = &a->ch[i];
        if (ch_lenflag(a, i) && c->len > 0 && --c->len == 0) c->on = false;
    }
}

static void clock_env_one(ApuCh *c, uint16_t envreg) {
    int period = (envreg >> 8) & 7;
    if (!c->on || !period) return;
    if (--c->env_t > 0) return;
    c->env_t = period;
    if (envreg & 0x0800) { if (c->vol < 15) c->vol++; }      /* bit 11: 1 = sube */
    else                 { if (c->vol > 0)  c->vol--; }
}

static void clock_env(Apu *a) {
    clock_env_one(&a->ch[0], a->r1h);
    clock_env_one(&a->ch[1], a->r2l);
    clock_env_one(&a->ch[3], a->r4l);
}

static void clock_sweep(Apu *a) {
    ApuCh *c = &a->ch[0];
    if (!a->sweep_on || !c->on) return;
    int per = (a->r1l >> 4) & 7;
    if (--a->sweep_t > 0) return;
    a->sweep_t = per ? per : 8;
    if (!per) return;
    int sh = a->r1l & 7;
    int d = a->shadow >> sh;
    int next = (a->r1l & 8) ? a->shadow - d : a->shadow + d;   /* bit 3: 1 = decrece */
    if (next > 2047) { c->on = false; return; }
    if (sh) { a->shadow = next; a->r1x = (uint16_t)((a->r1x & ~0x7FF) | next); }
}

/* ---------------------------------------------------------------- generacion */
static void gen_sample(Apu *a, int16_t *outl, int16_t *outr) {
    if (!a->master) { *outl = *outr = 0; return; }

    /* frame sequencer: 512 Hz = cada 32768 ciclos de CPU */
    a->fs_cc += STEP_FP;
    while (a->fs_cc >= (32768ULL << 16)) {
        a->fs_cc -= (32768ULL << 16);
        int s = a->fs_step;
        a->fs_step = (s + 1) & 7;
        if ((s & 1) == 0) clock_length(a);
        if (s == 2 || s == 6) clock_sweep(a);
        if (s == 7) clock_env(a);
    }

    int out[4] = { 0, 0, 0, 0 };

    /* canales 1 y 2: paso de duty cada 16*(2048-f) ciclos */
    for (int i = 0; i < 2; i++) {
        ApuCh *c = &a->ch[i];
        uint16_t freg = i ? a->r2h : a->r1x;
        uint16_t lreg = i ? a->r2l : a->r1h;
        uint64_t per = ((uint64_t)(2048 - (freg & 0x7FF)) * 16) << 16;
        c->cc += STEP_FP;
        while (c->cc >= per) { c->cc -= per; c->pos = (c->pos + 1) & 7; }
        if (c->on && ((DUTY[(lreg >> 6) & 3] >> (7 - c->pos)) & 1)) out[i] = c->vol;
    }

    /* canal 3: una muestra de la wave RAM cada 8*(2048-f) ciclos */
    {
        ApuCh *c = &a->ch[2];
        bool dim = (a->r3l & 0x20) != 0;
        int start = (a->r3l >> 6) & 1;
        uint64_t per = ((uint64_t)(2048 - (a->r3x & 0x7FF)) * 8) << 16;
        c->cc += STEP_FP;
        while (c->cc >= per) { c->cc -= per; c->pos = (c->pos + 1) & (dim ? 63 : 31); }
        if (c->on) {
            int bank = dim ? (((c->pos >> 5) & 1) ^ start) : start;
            int idx = c->pos & 31;
            uint8_t byte = a->wave[bank][idx >> 1];
            int nib = (idx & 1) ? (byte & 0xF) : (byte >> 4);       /* nibble alto primero */
            if (a->r3h & 0x8000)               out[2] = nib * 3 / 4;   /* fuerza 75% */
            else switch ((a->r3h >> 13) & 3) {
                case 0: out[2] = 0; break;
                case 1: out[2] = nib; break;
                case 2: out[2] = nib >> 1; break;
                default: out[2] = nib >> 2; break;
            }
        }
    }

    /* canal 4: LFSR; periodo = (r ? 64*r : 32) << s ciclos */
    {
        ApuCh *c = &a->ch[3];
        int r = a->r4h & 7, s = (a->r4h >> 4) & 15;
        uint64_t per = ((uint64_t)(r ? 64 * r : 32) << s) << 16;
        c->cc += STEP_FP;
        while (c->cc >= per) {
            c->cc -= per;
            uint32_t x = (a->lfsr ^ (a->lfsr >> 1)) & 1;
            a->lfsr = (a->lfsr >> 1) | (x << 14);
            if (a->r4h & 8) a->lfsr = (a->lfsr & ~0x40u) | (x << 6);    /* modo de 7 bit */
        }
        if (c->on && !(a->lfsr & 1)) out[3] = c->vol;
    }

    /* mezcla PSG: SOUNDCNT_L bits 0-2 = vol R, 4-6 = vol L, 8-11 = canales a R, 12-15 = a L */
    int lsum = 0, rsum = 0;
    for (int i = 0; i < 4; i++) {
        if ((a->cnt_l >> (8 + i)) & 1)  rsum += out[i];
        if ((a->cnt_l >> (12 + i)) & 1) lsum += out[i];
    }
    static const int PSGVOL[4] = { 1, 2, 4, 4 };                   /* 25% / 50% / 100% */
    int pv = PSGVOL[a->cnt_h & 3];
    int l = lsum * (((a->cnt_l >> 4) & 7) + 1) * pv / 4;
    int r = rsum * ((a->cnt_l & 7) + 1) * pv / 4;

    /* Direct Sound: SOUNDCNT_H bit 2/3 = volumen A/B (0 = 50%, 1 = 100%), bits 8/9 y 12/13 = R/L */
    int fa = a->fifo_held[0] * ((a->cnt_h & 0x04) ? 2 : 1);
    int fb = a->fifo_held[1] * ((a->cnt_h & 0x08) ? 2 : 1);
    if (a->cnt_h & 0x0100) r += fa;
    if (a->cnt_h & 0x0200) l += fa;
    if (a->cnt_h & 0x1000) r += fb;
    if (a->cnt_h & 0x2000) l += fb;

    if (l > 511) l = 511;
    if (l < -512) l = -512;
    if (r > 511) r = 511;
    if (r < -512) r = -512;

    /* filtro paso alto (~28 Hz): quita el DC de PSG/FIFO como el condensador de salida */
    int32_t in[2] = { l, r }, o[2];
    for (int k = 0; k < 2; k++) {
        a->hp_y[k] = in[k] - a->hp_x[k] + a->hp_y[k] - (a->hp_y[k] >> 8);
        a->hp_x[k] = in[k];
        int32_t y = a->hp_y[k] * 64;
        if (y > 32767) y = 32767;
        if (y < -32768) y = -32768;
        o[k] = y;
    }
    *outl = (int16_t)o[0];
    *outr = (int16_t)o[1];
}

void apu_step(GBA *g, uint32_t cycles) {
    Apu *a = &g->apu;
    a->sample_acc += (uint64_t)cycles * SAMPLE_RATE;
    while (a->sample_acc >= CPU_HZ) {
        a->sample_acc -= CPU_HZ;
        int16_t l, r;
        gen_sample(a, &l, &r);
        ring_push(l, r);
    }
}
