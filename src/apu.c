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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CPU_HZ   16777216ULL
/* ciclos de CPU por muestra de salida en punto fijo 16.16 */
#define STEP_FP  ((CPU_HZ << 16) / SAMPLE_RATE)

/* Wave Duty (GBATEK 4000062h), leido del bit 7 (primer paso) al bit 0:
 *   0 = 12.5% "-_______"   1 = 25% "--______"
 *   2 = 50%   "----____"   3 = 75% "------__"
 * El pulso HIGH arranca en el primer paso de la ventana, tal como lo dibuja
 * GBATEK. La tabla de Pan Docs ({01,81,87,7E}) tiene el mismo ciclo pero
 * desfasado 4 pasos: el pulso cae al final de la ventana. Se usa la de GBATEK. */
static const uint8_t DUTY[4] = { 0x80, 0xC0, 0xF0, 0xFC };

/* ---------------------------------------------------------------- ring buffer */
#define RING_FRAMES 8192u                   /* potencia de 2 */
static int16_t ring[RING_FRAMES * 2];
static atomic_uint ring_w, ring_r;
static int16_t last_l, last_r;

#ifdef APUDBG
unsigned apu_dbg_ringdrop;
#endif

static void ring_push(int16_t l, int16_t r) {
    unsigned w = atomic_load_explicit(&ring_w, memory_order_relaxed);
    unsigned rd = atomic_load_explicit(&ring_r, memory_order_acquire);
    if (w - rd >= RING_FRAMES) {
#ifdef APUDBG
        apu_dbg_ringdrop++;
#endif
        return;                                             /* lleno: se descarta */
    }
    ring[(w & (RING_FRAMES - 1)) * 2]     = l;
    ring[(w & (RING_FRAMES - 1)) * 2 + 1] = r;
    atomic_store_explicit(&ring_w, w + 1, memory_order_release);
}

static inline uint8_t read_wave(const Apu *a, int bank, int idx) {
    switch (idx >> 1) {
    case 0:  return a->wave[bank][0];
    case 1:  return a->wave[bank][1];
    case 2:  return a->wave[bank][2];
    case 3:  return a->wave[bank][3];
    case 4:  return a->wave[bank][4];
    case 5:  return a->wave[bank][5];
    case 6:  return a->wave[bank][6];
    case 7:  return a->wave[bank][7];
    case 8:  return a->wave[bank][8];
    case 9:  return a->wave[bank][9];
    case 10: return a->wave[bank][10];
    case 11: return a->wave[bank][11];
    case 12: return a->wave[bank][12];
    case 13: return a->wave[bank][13];
    case 14: return a->wave[bank][14];
    default: return a->wave[bank][15];
    }
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

/* Cuantas muestras hay listas para reproducir (utilidad de test). */
int apu_available(void) {
    unsigned w = atomic_load_explicit(&ring_w, memory_order_acquire);
    unsigned rd = atomic_load_explicit(&ring_r, memory_order_relaxed);
    return (int)(w - rd);
}

/* Vacia hasta maxframes muestras sin repetir la ultima si el ring se queda
 * corto; devuelve el numero de frames escritos. */
int apu_drain(int16_t *stream, int maxframes) {
    unsigned rd = atomic_load_explicit(&ring_r, memory_order_relaxed);
    unsigned w  = atomic_load_explicit(&ring_w, memory_order_acquire);
    int n = 0;
    while (n < maxframes && rd != w) {
        stream[n * 2]     = ring[(rd & (RING_FRAMES - 1)) * 2];
        stream[n * 2 + 1] = ring[(rd & (RING_FRAMES - 1)) * 2 + 1];
        rd++; n++;
    }
    atomic_store_explicit(&ring_r, rd, memory_order_release);
    return n;
}

/* Decimate the ring down to the newest nframes samples, so audio keeps playing
 * (compressed, instead of muted) while the emulator fast-forwards.  Used when
 * the emulation runs far ahead of real time: the ring fills much faster than
 * the device drains it, so taking the most recent window keeps the sound
 * roughly in sync without an ever-growing latency. */
int apu_drain_latest(int16_t *stream, int maxframes) {
    unsigned w = atomic_load_explicit(&ring_w, memory_order_acquire);
    unsigned rd = atomic_load_explicit(&ring_r, memory_order_relaxed);
    unsigned avail = w - rd;
    if (avail > RING_FRAMES) { rd = w - RING_FRAMES; avail = RING_FRAMES; }
    unsigned take = (avail > (unsigned)maxframes) ? (unsigned)maxframes : avail;
    unsigned start = w - take;              /* keep the newest `take` frames */
    int n = 0;
    for (unsigned i = 0; i < take; i++) {
        unsigned idx = (start + i) & (RING_FRAMES - 1);
        stream[n * 2]     = ring[idx * 2];
        stream[n * 2 + 1] = ring[idx * 2 + 1];
        n++;
    }
    atomic_store_explicit(&ring_r, w, memory_order_release);
    return n;
}

void apu_open(void) { }
void apu_close(void) { }

void apu_reset(GBA *g) {
    memset(&g->apu, 0, sizeof(g->apu));
    g->apu.bias = 0x0200;                                   /* SOUNDBIAS por defecto */
    g->apu.lfsr = 0x4000;      /* GBATEK: valor inicial 15 bit */
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

/* Periodo del timer que relojea un FIFO, en ciclos de CPU.  Cada desbordamiento
   saca UNA muestra del FIFO (GBATEK SOUNDCNT_H bits 10/14). */
static uint32_t fifo_timer_period(GBA *g, int t) {
    const Timer *tm = &g->timer[t];
    uint32_t ps = tm->prescaler ? (uint32_t)tm->prescaler : 1u;
    return (uint32_t)(0x10000 - tm->reload) * ps;
}

#ifdef APUDBG
/* Contadores de diagnostico: cuantas veces se pide una muestra al FIFO y
   cuantas de esas el FIFO estaba vacio (underrun -> se repite la ultima). */
unsigned apu_dbg_pops[2], apu_dbg_underrun[2], apu_dbg_maxlen[2];
/* FIFODUMP=<path>: vuelca cada muestra Direct Sound (fifo_held) que se consume,
   como s8 crudo, para comparar el flujo de muestras contra mGBA. */
static FILE *g_fifodump;
static int g_fifodump_init;
static void fifodump(int ch, int8_t v) {
    if (!g_fifodump_init) {
        const char *p = getenv("FIFODUMP");
        g_fifodump = p ? fopen(p, "wb") : NULL;
        g_fifodump_init = 1;
    }
    if (g_fifodump) { fputc((uint8_t)v, g_fifodump); fflush(g_fifodump); }
}
#endif

/* El reloj del FIFO lo lleva apu_fifo_tick() al ritmo del timer; el
   desbordamiento no necesita hacer nada (re-armar aqui duplicaria la cuenta,
   porque el tick ya descuenta el mismo periodo). */
void apu_timer_overflow(GBA *g, int timer, uint32_t n) {
    (void)g; (void)timer; (void)n;
}

/* Avanza los FIFOs `cycles_fp` ciclos de CPU (punto fijo 16.16).  Saca una
   muestra cada vez que vence el periodo del timer y pide DMA si quedan <= 16
   bytes.  Se llama por muestra de audio, asi el Direct Sound sale a la
   frecuencia del timer y no en rafagas por scanline. */
static void apu_fifo_tick(GBA *g) {
    Apu *a = &g->apu;
    for (int f = 0; f < 2; f++) {
        int sel = (a->cnt_h >> (f ? 14 : 10)) & 1;
        /* Sin timer no hay reloj de FIFO; al parar se reinicia la fase para
           que un arranque posterior empiece limpio. */
        if (!g->timer[sel].enabled) { a->fifo_frac[f] = 0; continue; }
        uint32_t period = fifo_timer_period(g, sel);
        /* El Direct Sound no se reproduce en escalones: su DAC de 8 bit a la
           frecuencia del timer se resamplea a la salida.  La fase avanza
           STEP_FP/period por muestra de salida y se interpola linealmente
           entre las dos ultimas muestras sacadas del FIFO, igual que el
           resampler de mGBA.  Sin esto cada escalon de 8 bit suena como un
           clic ("rayado") cuando el timer va por debajo de 44100 Hz.
           La fase es 32.32: STEP_FP/period trunca a 16.16 (p.ej. con el Timer0
           de Esmeralda a 13379 Hz el avance real es 3.2966 pero el truncado da
           3.2963), y ese error de ~0.01% acumula ~1 muestra de deriva por
           segundo.  En 32.32 el error es < 1e-6. */
        uint64_t inc = ((uint64_t)STEP_FP << 16) / period;   /* 32.32 */
        if (!inc) inc = 1;
        a->fifo_frac[f] += inc;
        while (a->fifo_frac[f] >= ((uint64_t)1 << 32)) {
            a->fifo_frac[f] -= ((uint64_t)1 << 32);
#ifdef APUDBG
            apu_dbg_pops[f]++;
            if (a->fifo_len[f] > apu_dbg_maxlen[f]) apu_dbg_maxlen[f] = a->fifo_len[f];
            if (a->fifo_len[f] == 0) apu_dbg_underrun[f]++;
#endif
            a->fifo_prev[f] = a->fifo_held[f];
            if (a->fifo_len[f] > 0) {
                a->fifo_held[f] = a->fifo[f][a->fifo_head[f]];
                a->fifo_head[f] = (a->fifo_head[f] + 1) & 31;
                a->fifo_len[f]--;
            }
#ifdef APUDBG
            if (f == 0) fifodump(f, a->fifo_held[f]);
#endif
            if (a->fifo_len[f] <= 16) dma_fifo_request(g, f);
        }
        int32_t s0 = a->fifo_prev[f], s1 = a->fifo_held[f];
        /* fifo_frac es 32.32: la parte fraccionaria en Q8 es frac >> 24. */
        a->fifo_interp[f] = (s0 << 8)
            + (int32_t)(((int64_t)(s1 - s0) * (int64_t)a->fifo_frac[f]) >> 24);
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

/* GBATEK 4000064h/4000078h: el DAC esta encendido si el volumen inicial es
   <>0 o el modo de envolvente es "subir" (bit 11); los tres canales usan el
   mismo criterio.  Sin esto los canales con volumen inicial 0 y envolvente
   creciente se apagaban al trigger. */
#define DAC_ON(reg) (((reg) & 0xF800u) != 0)

static void trigger_sq1(Apu *a) {
    ApuCh *c = &a->ch[0];
    c->on = DAC_ON(a->r1h);                          /* DAC encendido si volumen != 0 */
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
    c->on = DAC_ON(a->r2l);                          /* DAC encendido si volumen != 0 */
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
    c->on = DAC_ON(a->r4l);
    trigger_env(c, a->r4l);
    c->len = 64 - (a->r4l & 63);
    if (c->len == 0) c->len = 64;
    c->cc = 0;
    /* GBATEK 400007Ch: "The initial value when (re-)starting the sound is
       X=40h (7bit) or X=4000h (15bit)" */
    a->lfsr = (a->r4h & 0x08) ? 0x40 : 0x4000;
    a->lfsr_out = 0;
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
        if (reg >= 0x090 && reg <= 0x09E) {      /* WAVE_RAM: el CPU ve el banco que NO suena;
                                                   con el chip apagado el banco actuante es 1
                                                   (mGBA GBAAudioReadWaveRAM) */
            int bank = ((a->r3l >> 6) & 1) ^ 1;
            if (!a->master) bank = 1;
            int i = (int)((reg - 0x090) & 0xE);
            return (uint16_t)(a->wave[bank][i] | (a->wave[bank][i + 1] << 8));
        }
        return 0;
    }
}

void apu_write_reg(GBA *g, uint32_t reg, uint16_t v) {
    Apu *a = &g->apu;
    uint16_t wv = v;

    if (reg >= 0x090 && reg <= 0x09E) {
        int bank = ((a->r3l >> 6) & 1) ^ 1;
        if (!a->master) bank = 1;
        int i = (int)((reg - 0x090) & 0xE);
        a->wave[bank][i] = (uint8_t)wv; a->wave[bank][i + 1] = (uint8_t)(wv >> 8);
        return;
    }
    /* GBATEK 4000084h: apagar el maestro (bit 7) borra 0x60-0x81; las
       escrituras a los registros PSG (0x60-0x80) se ignoran con el chip
       apagado (mGBA GBAIOWrite: "Ignore writes to most audio registers if
       the hardware is off").  Los registros de FIFO/mix/BIAS si se aceptan. */
    switch (reg) {
    case 0x082:
        a->cnt_h = wv & 0x770F;
        if (wv & 0x0800) {   /* reset FIFO A (GBATEK): clear the whole resampler state */
            a->fifo_head[0] = a->fifo_len[0] = 0;
            a->fifo_held[0] = a->fifo_prev[0] = 0;
            a->fifo_frac[0] = 0; a->fifo_interp[0] = 0;
        }
        if (wv & 0x8000) {   /* reset FIFO B */
            a->fifo_head[1] = a->fifo_len[1] = 0;
            a->fifo_held[1] = a->fifo_prev[1] = 0;
            a->fifo_frac[1] = 0; a->fifo_interp[1] = 0;
        }
        return;
    case 0x084:
        a->master = (wv & 0x80) != 0;
        if (!a->master) {                        /* apagar el chip borra los registros PSG */
            a->r1l = a->r1h = a->r1x = a->r2l = a->r2h = 0;
            a->r3l = a->r3h = a->r3x = a->r4l = a->r4h = 0;
            a->cnt_l = 0;
            for (int i = 0; i < 4; i++) a->ch[i].on = false;
        }
        return;
    case 0x088: a->bias = wv & 0xC3FE; return;
    }
    if (!a->master) return;                      /* con el maestro apagado, los registros PSG ignoran escrituras */

    switch (reg) {
    case 0x060: a->r1l = wv & 0x7F; break;
    case 0x062: a->r1h = wv; a->ch[0].len = 64 - (wv & 63); break;
    case 0x064: a->r1x = wv & 0x47FF; if (wv & 0x8000) trigger_sq1(a); break;
    case 0x068: a->r2l = wv; a->ch[1].len = 64 - (wv & 63); break;
    case 0x06C: a->r2h = wv & 0x47FF; if (wv & 0x8000) trigger_sq2(a); break;
    case 0x070:
        a->r3l = wv & 0xE0;
        if (!(wv & 0x80)) a->ch[2].on = false;
        break;
    case 0x072: a->r3h = wv & 0xE0FF; a->ch[2].len = 256 - (wv & 0xFF); break;
    case 0x074: a->r3x = wv & 0x47FF; if (wv & 0x8000) trigger_wave(a); break;
    case 0x078: a->r4l = wv; a->ch[3].len = 64 - (wv & 63); break;
    case 0x07C: a->r4h = wv & 0xC0FF; if (wv & 0x8000) trigger_noise(a); break;
    case 0x080: a->cnt_l = wv & 0xFF77; break;
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
static void gen_sample(GBA *g, int16_t *outl, int16_t *outr) {
    Apu *a = &g->apu;
    if (!a->master) { *outl = *outr = 0; return; }

    /* Los FIFOs avanzan al ritmo de su timer, una muestra por periodo, en el
       mismo paso temporal que el resto del APU: asi el Direct Sound no se
       reproduce en escalones (sample-and-hold por scanline). */
    apu_fifo_tick(g);

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
        /* paso de duty: 16*(2048-f) ciclos por paso, acumulados por muestra */
        uint64_t per = ((uint64_t)(2048 - (freg & 0x7FF)) * 16) << 16;
        c->cc += STEP_FP;
        if (c->cc >= per) { uint64_t steps = c->cc / per; c->cc %= per; c->pos = (c->pos + (unsigned)steps) & 7; }
        if (c->on && ((DUTY[(lreg >> 6) & 3] >> (7 - c->pos)) & 1)) out[i] = c->vol;
    }

    /* canal 3: una muestra de la wave RAM cada 8*(2048-f) ciclos (GBATEK
       4000074h; mGBA GB_AUDIO_GBA: cycles = 2*(2048-rate)*4) */
    {
        ApuCh *c = &a->ch[2];
        bool dim = (a->r3l & 0x20) != 0;
        int start = (a->r3l >> 6) & 1;
        uint64_t per = ((uint64_t)(2048 - (a->r3x & 0x7FF)) * 8) << 16;
        c->cc += STEP_FP;
        if (c->cc >= per) { uint64_t steps = c->cc / per; c->cc %= per; c->pos = (c->pos + (unsigned)steps) & (dim ? 63 : 31); }
        if (c->on) {
            int bank = dim ? (((c->pos >> 5) & 1) ^ start) : start;
            int idx = c->pos & 31;
            uint8_t byte = read_wave(a, bank, idx);
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
        if (c->cc >= per) {
            uint64_t steps = c->cc / per;
            c->cc %= per;
            for (uint64_t k = 0; k < steps; k++) {
                /* GBATEK "Noise Random Generator":
                   15bit: X=X SHR 1, IF carry THEN Out=HIGH, X=X XOR 6000h
                    7bit: X=X SHR 1, IF carry THEN Out=HIGH, X=X XOR 60h   */
                uint32_t carry = a->lfsr & 1;
                a->lfsr >>= 1;
                if (carry) a->lfsr ^= (a->r4h & 0x08) ? 0x60u : 0x6000u;
                a->lfsr_out = (uint8_t)carry;
            }
        }
        if (c->on && a->lfsr_out) out[3] = c->vol;
    }

    /* Mezcla PSG: SOUNDCNT_L bits 0-2 = vol R, 4-6 = vol L, 8-11 = canales a R, 12-15 = a L */
    int lsum = 0, rsum = 0;
    for (int i = 0; i < 4; i++) {
        if ((a->cnt_l >> (8 + i)) & 1)  rsum += out[i];
        if ((a->cnt_l >> (12 + i)) & 1) lsum += out[i];
    }
    /* Volumen PSG (SOUNDCNT_H bits 0-1).  mGBA (GBAudioSamplePSG +
       GBAAudioSample) desplaza la suma de canales 3 bits y luego
       `>> (4 - volumen)`, o sea una ganancia de 2^(volumen-1) sobre la suma:
       25% => x0.5, 50% => x1, 100% => x2, 200% => x4.  Con la tabla anterior
       ({1,2,4,4} / 4) la ganancia era 2^volumen / 4 = la mitad de la
       referencia en 25/50/100% y un cuarto en 200%: el PSG (blips de menu,
       efectos) sonaba la mitad de fuerte que en mGBA frente al Direct Sound. */
    int pv = 1 << (a->cnt_h & 3);
    int l = lsum * (((a->cnt_l >> 4) & 7) + 1) * pv / 2;
    int r = rsum * ((a->cnt_l & 7) + 1) * pv / 2;
#ifdef APUDBG
    if (getenv("APUMUTE") && (atoi(getenv("APUMUTE")) & 1)) l = r = 0;   /* bit0: silencia PSG */
#endif

    /* Direct Sound: SOUNDCNT_H bit 2/3 = volumen A/B (0 = 50%, 1 = 100%),
       bits 8/9 y 12/13 = R/L.  La ganancia base es x4 (mGBA
       GBAAudioSample: `(sample << 2) >> !volumeChA`); 50% => x2.
       fifo_interp va en Q8 (muestra * 256), asi que la ganancia se aplica
       dividiendo por 256 al final. */
    int fa = a->fifo_interp[0] * ((a->cnt_h & 0x04) ? 4 : 2) / 256;
    int fb = a->fifo_interp[1] * ((a->cnt_h & 0x08) ? 4 : 2) / 256;
#ifdef APUDBG
    if (getenv("APUMUTE") && (atoi(getenv("APUMUTE")) & 2)) fa = fb = 0; /* bit1: silencia FIFO */
#endif
    if (a->cnt_h & 0x0100) r += fa;
    if (a->cnt_h & 0x0200) l += fa;
    if (a->cnt_h & 0x1000) r += fb;
    if (a->cnt_h & 0x2000) l += fb;

    if (l > 511) l = 511;
    if (l < -512) l = -512;
    if (r > 511) r = 511;
    if (r < -512) r = -512;

    /* Salida: clamp por SOUNDBIAS (mGBA _applyBias).  El hardware mezcla los
       canales en un nodo con la polarizacion de SOUNDBIAS y el ADC recorta a la
       resolucion configurada; el DAC/condensador de salida quita el nivel
       continuo.  Emular eso como un filtro paso alto digital era un error: el
       filtro que habia (polo 255/256) no filtraba el DC (|y| < 256 hacia
       `y -= y>>8` cero y degeneraba en acumulador), pero si diferenciaba la
       senal, con una ganancia de ~1000 a la frecuencia de la onda cuadrada.  El
       ataque de cada nota se hundia y decaia a lo largo de la nota, asi que el
       "clic" del menu sonaba apagado y con cola (amplitud 5856-6192 con la
       envolvente decreciente, frente a 5760 constante en mGBA).

       Ahora se replica el clamp de mGBA: sumar la polarizacion de SOUNDBIAS,
       recortar a la resolucion del ADC y restar la polarizacion, que deja la
       salida centrada en 0 y a escala identica a la referencia
       (x48 = 0x100 * 3 >> 4). */
    int32_t in[2] = { l, r }, o[2];
    int bias = (a->bias & 0x3FF);
    for (int k = 0; k < 2; k++) {
        int32_t s = in[k] + bias;
        if (s >= 0x400) s = 0x3FF;
        else if (s < 0) s = 0;
        int32_t y = (s - bias) * 48;
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
        gen_sample(g, &l, &r);
        ring_push(l, r);
    }
}
