/* Regression test for the output stage of the APU ring (src/apu.c).

   A dry ring must not be filled by holding the last sample for long: that is
   what cut the attack of a loud voice and left the "kjj" before it.  Short
   gaps still hold (fading on every one of them modulated the envelope and
   sounded like constant crackle), long gaps fade to silence smoothly.

   Build/run:
     gcc -std=c99 -Wall -Wextra -Isrc tools/apu_mix_test.c src/apu.c src/mem.c \
         src/gba.c src/arm.c src/ppu.c src/bios.c src/fb.c src/eeprom.c \
         src/flash.c src/rtc.c src/export.c src/gui.c -lm -o /tmp/apu_mix_test
     /tmp/apu_mix_test
*/
#include "gba.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

static void check(int cond, const char *what) {
    printf("%s  %s\n", cond ? "PASS" : "FAIL", what);
    if (!cond) failures++;
}

/* Fill the ring with a constant Direct Sound level (FIFO A, Timer 0). */
static void fill_ring(GBA *g, int frames) {
    uint8_t level[32];
    memset(level, 0x40, sizeof level);
    g->apu.master = true;
    g->apu.cnt_h = 0x0304;                       /* A a L+R, 100%, Timer 0 */
    g->timer[0].enabled = true;
    g->timer[0].reload = 0x10000 - 400;          /* ~42 kHz: una muestra por salida */
    apu_fifo_write(g, 0, level, sizeof level);
    apu_step(g, (uint32_t)((double)frames * 16777216.0 / SAMPLE_RATE) + 1);
}

int main(void) {
    GBA *g = calloc(1, sizeof(GBA));
    gba_init(g);
    gba_reset(g);
    apu_open();

    int16_t buf[4096];

    /* --- 1. short gap: sample-and-hold, no fade (no click) --- */
    fill_ring(g, 512);
    int avail = apu_available();
    check(avail > 100, "el ring tiene muestras tras apu_step");
    apu_mix(g, buf, avail);
    int16_t held = buf[avail - 1];
    check(held != 0, "la senal generada no es silencio");
    apu_mix(g, buf, 16);                                /* 16 muestras sin datos */
    check(buf[0] == held && buf[15] == held, "hueco corto: mantiene la ultima muestra");

    /* --- 2. long gap: fades to silence and stays there --- */
    apu_mix(g, buf, 2048);
    int last = -1;
    for (int i = 0; i < 2048; i++) if (buf[i] != 0) last = i;
    check(last >= 0 && last < 600, "hueco largo: se funde a silencio en < 600 muestras");
    check(buf[2047] == 0, "hueco largo: no deja DC al final del segmento");

    /* smooth: no step bigger than a quarter of the held level (a click) */
    int maxstep = 0;
    int prev = held;
    for (int i = 0; i < 1024; i++) {
        int d = abs((int)buf[i] - prev);
        if (d > maxstep) maxstep = d;
        prev = buf[i];
    }
    check(maxstep <= abs(held) / 4 + 1, "el fundido no hace escalones (sin clic)");

    /* --- contadores de diagnostico (ring de 8192 frames) --- */
    ApuStats st;
    apu_stats_reset();
    g->apu.master = false;                      /* solo llena el ring, sin DMA */
    apu_step(g, (uint32_t)(12000.0 * 16777216.0 / SAMPLE_RATE) + 1);
    apu_stats(&st);
    check(st.drops > 0, "ring lleno: cuenta los descartes");
    apu_stats_reset();
    for (int i = 0; i < 6; i++) apu_mix(g, buf, 2048);   /* 12288 > ring */
    apu_stats(&st);
    check(st.underruns >= 1, "ring vacio: cuenta el underrun");
    check(st.held > 0 && st.mixed == 12288, "cuenta sostenidas y mezcladas");
    g->apu.master = true;

    /* --- 3. data comes back: fades in and the samples are the real ones --- */
    fill_ring(g, 1024);
    avail = apu_available();
    apu_mix(g, buf, avail);
    check(buf[avail - 1] == held, "al volver los datos la salida vuelve a la senal");
    check(buf[avail / 2] != 0 || buf[avail - 1] != 0, "la senal vuelve con nivel");

    /* --- 4. FIFO reset: SOUNDCNT_H bit 11 clears the resampler state --- */
    g->apu.cnt_h = 0x0304;
    g->apu.fifo_len[0] = 8; g->apu.fifo_held[0] = 99; g->apu.fifo_prev[0] = -99;
    apu_write_reg(g, 0x082, 0x0304 | 0x0800);
    check(g->apu.fifo_len[0] == 0 && g->apu.fifo_held[0] == 0 &&
          g->apu.fifo_prev[0] == 0 && g->apu.fifo_interp[0] == 0,
          "reset del FIFO A limpia cola y estado del resampler");

    printf("\n%s (%d fallos)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
