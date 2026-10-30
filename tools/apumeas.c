#include "gba.h"
#include <stdio.h>
#include <stdlib.h>

#ifdef APUDBG
extern unsigned apu_dbg_pops[2], apu_dbg_underrun[2], apu_dbg_maxlen[2], apu_dbg_ringdrop;
#endif

/* Measure the audio production rate and whether the ring would starve if the
   consumer drained exactly at 44100 Hz. */
int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: %s <rom> <frames>\n", argv[0]); return 2; }
    GBA *g = calloc(1, sizeof(GBA));
    if (!gba_load_rom(g, argv[1])) { fprintf(stderr, "load fail\n"); return 1; }
    g->bios_fastboot = true; gba_reset(g);
    int frames = atoi(argv[2]);
    long produced = 0, consumed = 0;
    int starve = 0;
    int16_t buf[8192];
    const double per_frame = 44100.0 * 1001.0 / 59727.0;   /* 59.7275 fps */
    for (int f = 0; f < frames; f++) {
        gba_run_frame(g);
        int avail = apu_available();
        produced += avail;
        int take = (int)(per_frame + 0.5);
        if (avail < take) { starve++; take = avail; }
        consumed += apu_drain(buf, take);
    }
    printf("frames=%d produced=%ld (%.2f/frame) consumed=%ld starve=%d "
           "ringdrop=%u underrunA=%u underrunB=%u maxlenA=%u maxlenB=%u\n",
           frames, produced, (double)produced / frames, consumed, starve,
#ifdef APUDBG
           apu_dbg_ringdrop, apu_dbg_underrun[0], apu_dbg_underrun[1],
           apu_dbg_maxlen[0], apu_dbg_maxlen[1]
#else
           0u, 0u, 0u, 0u, 0u
#endif
    );
    return 0;
}
