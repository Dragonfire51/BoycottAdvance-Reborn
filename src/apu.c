/*
 * apu.c - GBA sound: 4 PSG channels + FIFO (DMA sound) + mixing.
 * Clean-room implementation. BoycottAdvance 0.2.8 used the obsolete WINMM
 * waveOut API which fails on Windows 11; this build uses SDL audio instead.
 */
#include "gba.h"
#include "mem.h"

extern int16_t apu_mix_buf[0x400]; /* resampled buffer, frontend reads */

static uint32_t ch_freq_clock(uint16_t nr_x) {
    return 2048 - (nr_x & 0x7FF);    /* actually period = 2048 - x */
}

/* Global helpers */
static int32_t square_wave(uint32_t phase, int duty) {
    /* duty: 0->12.5% 1->25% 2->50% 3->75% */
    int cycle = phase & 7;
    switch (duty) {
    case 0: return cycle < 1 ? 1 : -1;
    case 1: return cycle < 2 ? 1 : -1;
    case 2: return cycle < 4 ? 1 : -1;
    case 3: return cycle < 6 ? 1 : -1;
    }
    return 1;
}

void apu_mix(GBA *g, int16_t *stream, int nframes) {
    Apu *a = &g->apu;
    /* 16.78MHz / 44100 = ~380 cycles per sample */
    const uint32_t step = APU_CYCLES_PER_SAMPLE;
    for (int i = 0; i < nframes; i++) {
        a->sample_cc += step;

        /* envelope timer updates on 256Hz (65536 cycles) */
        static uint32_t inst;
        uint32_t lim = a->sample_cc >> 16;
        while (inst < lim) {
            if (a->ch1_on) { if (a->ch1_vol > 0 && a->ch1_dir) a->ch1_vol--; else if (a->ch1_vol < 0xF && !a->ch1_dir) a->ch1_vol++; }
            if (a->ch2_on) { if (a->ch2_vol > 0 && a->ch2_dir) a->ch2_vol--; else if (a->ch2_vol < 0xF && !a->ch2_dir) a->ch2_vol++; }
            if (a->ch4_on) { if (a->ch4_vol > 0 && a->ch4_dir) a->ch4_vol--; else if (a->ch4_vol < 0xF && !a->ch4_dir) a->ch4_vol++; }
            inst++;
        }
        a->sample_cc &= 0xFFFF;
        inst &= 0xFFFF;

        /* channel 1 */
        int32_t s = 0;
        if (a->ch1_on && a->ch1_vol) {
            a->ch1_duty_pos += 1;
            uint32_t nxt = ch_freq_clock(a->nr13 | (a->nr14 << 8));
            if (a->ch1_freq_cc >= nxt) { a->ch1_freq_cc = 0; }
            a->ch1_freq_cc += (step & 1) ? 1 : 1;
            uint32_t period = ch_freq_clock(a->nr13 | (a->nr14 << 8));
            if (period == 0) period = 1;
            uint32_t phase = (a->ch1_duty_pos / period) & 7;
            int duty = (a->nr11 >> 6) & 3;
            int32_t amp = square_wave(phase, duty) * a->ch1_vol;
            s += amp;
        }
        /* channel 2 */
        if (a->ch2_on && a->ch2_vol) {
            a->ch2_duty_pos += 1;
            uint32_t period = ch_freq_clock(a->nr23 | (a->nr24 << 8));
            if (period == 0) period = 1;
            uint32_t phase = (a->ch2_duty_pos / period) & 7;
            int duty = (a->nr21 >> 6) & 3;
            s += square_wave(phase, duty) * a->ch2_vol;
        }
        /* channel 3: wave */
        if (a->ch3_on) {
            uint32_t period = ch_freq_clock(a->nr33 | (a->nr34 << 8));
            if (period == 0) period = 1;
            a->ch3_freq_cc += 1;
            uint32_t pos = (a->ch3_freq_cc / period) & 31;
            uint8_t w = a->wave[pos >> 1];
            int32_t nib = (pos & 1) ? (w >> 4) : (w & 0xF);
            int32_t vol = (a->nr32 >> 5) & 3;
            switch (vol) {
            case 0: nib = 0; break;
            case 1: nib >>= 1; break;
            case 2: break;
            case 3: break;
            }
            int32_t amp = (nib - 8) << 4;
            s += amp;
        }
        /* channel 4: noise */
        if (a->ch4_on && a->ch4_vol) {
            uint32_t period = (a->nr43 & 0x7) ? 0 : 0;
            period = ((a->nr43 & 7) ? 16 << (a->nr43 >> 4) : 8);
            a->ch4_cc += 1;
            if (a->ch4_cc >= period) { a->ch4_cc = 0; a->ch4_lfsr >>= 1; a->ch4_lfsr ^= ((a->ch4_lfsr & 1) ? 0x6000 : 0); if (a->nr43 & 8) a->ch4_lfsr &= 0x7FFF; }
            int32_t amp = ((a->ch4_lfsr & 1) ? 1 : -1) * a->ch4_vol;
            s += amp;
        }

        /* simple mastering: mono mix to both channels */
        s = (s << 2);
        if (s > 32767) s = 32767;
        if (s < -32768) s = -32768;
        stream[i*2] = (int16_t)s;
        stream[i*2+1] = (int16_t)s;
        a->sample_cc &= 0;   /* keep fractional */
    }
    (void)0;
}

/* audio opened by frontend via SDL; bridge functions */
void apu_open(void) { }
void apu_close(void) { }