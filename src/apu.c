/*
 * apu.c - GBA sound: 4 PSG channels + Direct Sound FIFOs.
 *
 * The mixer runs one pass over the output buffer. Cycle counters are
 * fractional (<<16) so a channel can step several times per output sample,
 * which is required for high frequencies. PSG channels feed the four legacy
 * tone generators; channels A/B play the 8-bit DMA FIFOs.
 */
#include "gba.h"
#include "mem.h"

/* One output sample is ~380 CPU cycles at 44.1 kHz; keep the fraction. */
#define STEP_FP     ((uint32_t)(((uint64_t)16777216 << 16) / APU_SR))
static const uint32_t DUTY[4] = { 1, 2, 4, 6 };   /* duty step 0..7 */

static int16_t g_fifo_a[2];   /* last FIFO-A sample, L/R */
static int16_t g_fifo_b[2];

/* Pop one DAC sample from a FIFO; if it is empty the last value is held. */
static uint8_t fifo_pop(int32_t *buf, int *head, int *len, int32_t *held) {
    if (*len > 0) {
        *held = buf[*head];
        *head = (*head + 1) & 31;
        (*len)--;
    }
    return (uint8_t)((*held >> 24) & 0xFF);
}

void apu_mix(GBA *g, int16_t *stream, int nframes) {
    Apu *a = &g->apu;
    const int step = APU_CYCLES_PER_SAMPLE;

    for (int i = 0; i < nframes; i++) {
        /* --- envelope / length / sweep (256 Hz) --- */
        a->env_cc += step;
        while (a->env_cc >= 65536) {
            a->env_cc -= 65536;
            if (a->ch1_on && a->nr12 & 0xF8) {
                if (a->ch1_dir) { if (a->ch1_vol > 0) a->ch1_vol--; }
                else if (a->ch1_vol < 15) a->ch1_vol++;
            }
            if (a->ch2_on && a->nr22 & 0xF8) {
                if (a->ch2_dir) { if (a->ch2_vol > 0) a->ch2_vol--; }
                else if (a->ch2_vol < 15) a->ch2_vol++;
            }
            if (a->ch4_on && a->nr42 & 0xF8) {
                if (a->ch4_dir) { if (a->ch4_vol > 0) a->ch4_vol--; }
                else if (a->ch4_vol < 15) a->ch4_vol++;
            }
        }

        /* --- PSG channels 1/2 (square) --- */
        int32_t s = 0;
        if (a->ch1_on && a->ch1_vol) {
            uint32_t period = 2048 - (a->nr13 & 0x7FF);
            a->ch1_phase += ((uint32_t)step << 16) / (period * 8);
            if (((a->ch1_phase >> 16) & 7) < DUTY[(a->nr11 >> 6) & 3]) s += a->ch1_vol;
            else s -= a->ch1_vol;
        }
        if (a->ch2_on && a->ch2_vol) {
            uint32_t period = 2048 - (a->nr23 & 0x7FF);
            a->ch2_phase += ((uint32_t)step << 16) / (period * 8);
            if (((a->ch2_phase >> 16) & 7) < DUTY[(a->nr21 >> 6) & 3]) s += a->ch2_vol;
            else s -= a->ch2_vol;
        }

        /* --- channel 3 (wave RAM) --- */
        if (a->ch3_on && ((a->nr32 >> 5) & 3)) {
            uint32_t period = 2048 - (a->nr33 & 0x7FF);
            a->ch3_phase += ((uint32_t)step << 16) / period;
            uint32_t pos = (a->ch3_phase >> 16) & 31;
            uint8_t w = a->wave[pos >> 1];
            int nib = (pos & 1) ? (w >> 4) : (w & 0xF);
            int vol = (a->nr32 >> 5) & 3;
            s += (nib - 8) * (1 << vol);   /* roughly -8..8 * 2..8 */
        }

        /* --- channel 4 (noise) --- */
        if (a->ch4_on && a->ch4_vol) {
            uint32_t period = (a->nr43 & 7) ? ((a->nr43 & 7) << 4) << (a->nr43 >> 4)
                                            : 8 << (a->nr43 >> 4);
            if (period == 0) period = 1;
            a->ch4_cc += step;
            while (a->ch4_cc >= period) {
                a->ch4_cc -= period;
                int bit = (a->ch4_lfsr ^ (a->ch4_lfsr >> 1)) & 1;
                a->ch4_lfsr = (a->ch4_lfsr >> 1) | (bit << 14);
                if (a->nr43 & 8) a->ch4_lfsr = (a->ch4_lfsr & ~(1u << 6)) | (bit << 6);
            }
            if (a->ch4_lfsr & 1) s += a->ch4_vol; else s -= a->ch4_vol;
        }

        /* --- FIFO A / B (Direct Sound) ---
           Each FIFO is clocked by timer 0 or timer 1 at 16.78 MHz; a sample
           is popped every timer overflow (period = 65536 - reload). Both
           FIFOs are folded into the PSG master volume for stereo output. */
        int32_t ra = 0, rb = 0;
        uint16_t h = (uint16_t)a->sound_cnt[0];
        if (h & 0x0300) {                                      /* A enabled L/R */
            uint32_t per = 65536u - g->timer[(h >> 10) & 1].reload;
            if (!per) per = 1;
            a->fifo_acc_a += step;
            while (a->fifo_acc_a >= per) {
                a->fifo_acc_a -= per;
                uint8_t smp = fifo_pop(a->fifoa, &a->fifoa_head, &a->fifoa_len, &a->fifoa_held);
                g_fifo_a[0] = (int16_t)(((int)smp - 128) << 8);
            }
            ra = (h & 0x04) ? g_fifo_a[0] : (g_fifo_a[0] >> 1);   /* vol 100/50% */
        }
        if (h & 0x3000) {                                      /* B enabled L/R */
            uint32_t per = 65536u - g->timer[(h >> 14) & 1].reload;
            if (!per) per = 1;
            a->fifo_acc_b += step;
            while (a->fifo_acc_b >= per) {
                a->fifo_acc_b -= per;
                uint8_t smp = fifo_pop(a->fifob, &a->fifob_head, &a->fifob_len, &a->fifob_held);
                g_fifo_b[0] = (int16_t)(((int)smp - 128) << 8);
            }
            rb = (h & 0x08) ? g_fifo_b[0] : (g_fifo_b[0] >> 1);
        }

        /* PSG master volume (nr50): left in bits 0-2, right in bits 4-6. */
        s = s * 4;
        int32_t l = s + ra + rb;
        int32_t r = s + rb + ra;
        int vol_l = (a->nr50 & 7) + 1, vol_r = ((a->nr50 >> 4) & 7) + 1;
        l = l * vol_l / 8;
        r = r * vol_r / 8;
        if (l > 32767) l = 32767;
        if (l < -32768) l = -32768;
        if (r > 32767) r = 32767;
        if (r < -32768) r = -32768;
        stream[i*2]   = (int16_t)l;
        stream[i*2+1] = (int16_t)r;
    }
}

void apu_open(void) { }
void apu_close(void) { }

/* Push a DMA sound block into FIFO A or B. Called from the DMA engine.
   The GBA plays the low byte of each 32-bit word, in memory order. */
void apu_fifo_write(GBA *g, int ch, const uint8_t *bytes, int nbytes) {
    Apu *a = &g->apu;
    int32_t *buf = ch ? a->fifob : a->fifoa;
    int *head = ch ? &a->fifob_head : &a->fifoa_head;
    int *len  = ch ? &a->fifob_len  : &a->fifoa_len;
    for (int i = 0; i < nbytes; i++) {
        if (*len >= 32) break;                 /* FIFO is full */
        int idx = (*head + *len) & 31;
        buf[idx] = (int32_t)bytes[i] << 24;
        (*len)++;
    }
}
