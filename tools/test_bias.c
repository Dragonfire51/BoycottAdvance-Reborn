#include "gba.h"
#include <stdio.h>
#include <stdlib.h>

/* Regression: gba_reset() must leave SOUNDBIAS at its power-on default
   (0x0200), not zero.

   gba_reset() used to memset(&g->apu, 0, ...) without calling apu_reset(),
   so g->apu.bias stayed 0. The output stage clamps `sample + bias` to the
   ADC range ([0, 0x3FF]) before subtracting the bias back out
   (GBATEK "4000088h - SOUNDBIAS"; mGBA _applyBias). With bias = 0 the clamp
   `if (s < 0) s = 0` rectified the whole mix: every negative half-cycle was
   flattened to zero, so the signal came out unipolar (min == 0). That is the
   "rayado" (harsh, distorted) audio - the waveform's lower half was gone.

   Checked against mGBA: a 600-frame Pokemon Emerald dump has min = -7920 and
   ~36% negative samples with the fix, against min = 0 and 0% before. */
int main(void) {
    GBA *g = calloc(1, sizeof(GBA));
    gba_reset(g);
    int bias_after_reset = g->apu.bias;

    /* A direct apu_reset() must agree. */
    apu_reset(g);
    int bias_after_apu_reset = g->apu.bias;

    /* An explicit SOUNDBIAS write must still stick (bits 0-9 = bias level). */
    gba_write16(g, 0x04000088, 0x0300);
    int bias_after_write = g->apu.bias;

    printf("bias after gba_reset  = %04X (expect 0200)\n", bias_after_reset);
    printf("bias after apu_reset  = %04X (expect 0200)\n", bias_after_apu_reset);
    printf("bias after write 0300 = %04X (expect 0300)\n", bias_after_write);

    int ok = (bias_after_reset == 0x0200) &&
             (bias_after_apu_reset == 0x0200) &&
             (bias_after_write == 0x0300);
    printf("%s\n", ok ? "BIAS PASS" : "BIAS FAIL");
    free(g);
    return ok ? 0 : 1;
}
