#include "gba.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* A4: an 8-bit write to a sound register must merge with the RAW last-written
   halfword, not with io_read16 (which masks write-only bits). M4A writes the
   frequency low byte, then the high byte with the trigger bit. */
int main(void) {
    GBA *g = calloc(1, sizeof(GBA));
    gba_reset(g);
    /* Master sound enable (SOUNDCNT_X bit 7) gates all PSG writes. */
    gba_write16(g, 0x04000084, 0x0080);

    /* SOUND1CNT_L = 0x04000060: sweep (write-only). Write both bytes separately. */
    gba_write8(g, 0x04000060, 0x34);
    gba_write8(g, 0x04000061, 0x12);
    printf("r1l=%04X (expect 0034: bits 0-6 only)\n", g->apu.r1l);

    /* SOUND1CNT_H = 0x04000062: duty/len/env (write-only bits). */
    gba_write8(g, 0x04000062, 0xAB);
    gba_write8(g, 0x04000063, 0xF0);
    printf("r1h=%04X (expect F0AB)\n", g->apu.r1h);

    /* SOUND1CNT_X = 0x04000064: frequency (write-only) + trigger bit15. */
    gba_write8(g, 0x04000064, 0xCD);      /* freq low */
    printf("r1x after low byte: %04X (expect 00CD)\n", g->apu.r1x);
    gba_write8(g, 0x04000065, 0x84);      /* freq high 0x4 + trigger */
    printf("r1x=%04X (expect 04CD) freq=%d\n", g->apu.r1x, g->apu.r1x & 0x7FF);

    int ok = (g->apu.r1l == 0x0034) &&
             (g->apu.r1h == 0xF0AB) &&
             ((g->apu.r1x & 0x7FF) == 0x4CD);
    printf("A4 %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
