/* Checks the user volume (apu_set_volume): 0 % must be silence, 50 %/25 %
   must be exactly half/quarter of 100 % (Q8 gain, floored) and 200 % must be
   the 100 % signal doubled and clipped, never wrapped.  The emulation is
   deterministic, so every volume is rendered from a fresh reset and the
   samples are compared one by one.

   gcc -std=c99 -O2 -Isrc tools/volume_test.c src/gba.c src/mem.c src/arm.c \
       src/ppu.c src/apu.c src/bios.c src/eeprom.c src/flash.c src/rtc.c \
       -lm -o /tmp/volume_test && /tmp/volume_test <rom> */
#include "gba.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SKIP_FRAMES 1200           /* past the boot jingle, into steady music */
#define TAKE_FRAMES 240
#define MAXS (TAKE_FRAMES * 1024)

static int16_t *render(const char *rom, int pct, int *out_n) {
    GBA *g = calloc(1, sizeof(GBA));
    int16_t *buf = calloc(MAXS, 2 * sizeof(int16_t));
    if (!g || !buf) { fprintf(stderr, "oom\n"); exit(2); }
    gba_init(g);
    if (!gba_load_rom(g, rom)) { fprintf(stderr, "load fail\n"); exit(1); }
    g->bios_fastboot = true;
    gba_reset(g);
    apu_set_volume(pct);
    int16_t tmp[8192];
    for (int f = 0; f < SKIP_FRAMES; f++) { gba_run_frame(g); while (apu_drain(tmp, 4096) > 0) {} }
    int n = 0;
    for (int f = 0; f < TAKE_FRAMES; f++) {
        gba_run_frame(g);
        int got;
        while (n + 4096 <= MAXS && (got = apu_drain(buf + n * 2, 4096)) > 0) n += got;
    }
    *out_n = n;
    free(g);
    return buf;
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s <rom>\n", argv[0]); return 2; }
    int n100, n50, n25, n200, n0;
    int16_t *b100 = render(argv[1], 100, &n100);
    int16_t *b50  = render(argv[1],  50, &n50);
    int16_t *b25  = render(argv[1],  25, &n25);
    int16_t *b200 = render(argv[1], 200, &n200);
    int16_t *b0   = render(argv[1],   0, &n0);
    int fail = 0;
    if (n100 < 1000 || n50 != n100 || n25 != n100 || n200 != n100 || n0 != n100) {
        printf("FAIL length mismatch %d %d %d %d %d\n", n100, n50, n25, n200, n0);
        return 1;
    }
    long loud = 0, bad0 = 0, bad50 = 0, bad25 = 0, bad200 = 0, clipped = 0;
    for (int i = 0; i < n100 * 2; i++) {
        int s = b100[i];
        if (s > 200 || s < -200) loud++;
        if (b0[i] != 0) bad0++;
        int h = s >> 1;                                  /* Q8 50 %: floor */
        if (b50[i] != h) bad50++;
        if (b25[i] != (s >> 2)) bad25++;
        int d = s * 2, cl = d > 32767 ? 32767 : (d < -32768 ? -32768 : d);
        if (b200[i] != cl) bad200++;
        if (cl != d) clipped++;
    }
    printf("samples=%d loud=%ld clipped@200%%=%ld\n", n100 * 2, loud, clipped);
    if (loud < 1000) { printf("FAIL no audio produced\n"); fail = 1; }
    if (bad0)   { printf("FAIL 0%% not silent (%ld samples)\n", bad0); fail = 1; }
    if (bad50)  { printf("FAIL 50%% not half (%ld samples)\n", bad50); fail = 1; }
    if (bad25)  { printf("FAIL 25%% not quarter (%ld samples)\n", bad25); fail = 1; }
    if (bad200) { printf("FAIL 200%% not doubled+clipped (%ld samples)\n", bad200); fail = 1; }
    printf("%s\n", fail ? "RESULT: FAIL" : "RESULT: PASS");
    return fail;
}
