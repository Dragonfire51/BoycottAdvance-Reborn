#include "gba.h"
#include <stdio.h>
#include <stdlib.h>

/* A5: with the master sound enable off (SOUNDCNT_X bit 7 clear) the PSG
   registers 0x60-0x80 ignore writes and are cleared on the off transition
   (GBATEK 4000084h; mGBA GBAIOWrite). */
int main(void) {
    GBA *g = calloc(1, sizeof(GBA));
    gba_reset(g);

    gba_write16(g, 0x04000062, 0x1234);          /* sound off: must be ignored */
    int off_ignored = (g->apu.r1h == 0);

    gba_write16(g, 0x04000084, 0x0080);          /* master on */
    gba_write16(g, 0x04000062, 0x1234);          /* accepted */
    int on_accepted = (g->apu.r1h == 0x1234);

    gba_write16(g, 0x04000084, 0x0000);          /* master off: clears 0x60-0x81 */
    int off_clears = (g->apu.r1h == 0);

    printf("off_ignored=%d on_accepted=%d off_clears=%d\n",
           off_ignored, on_accepted, off_clears);
    int ok = off_ignored && on_accepted && off_clears;
    printf("A5 %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
