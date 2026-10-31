/* Replays the mGBA suite's io-read test (mgba-emu/suite src/io-read.c): write
   0xFFFF to a register, read it back, compare with the expected value.  This
   exercises the read-back masking of io_read16, which is boot- and timing-
   independent, so it isolates register bugs from the (unfinished) bus model.
   The suite's own expectations for write-only registers are open-bus
   (0xDEAD = its prefetched instruction), which need the bus model; those rows
   are reported separately and are not counted as masking bugs. */
#include "gba.h"
#include "mem.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1] : "tools/ioread_table.txt";
    FILE* f = fopen(path, "r");
    if (!f) { perror(path); return 1; }
    GBA* g = calloc(1, sizeof(GBA));
    gba_init(g);
    gba_reset(g);
    io_write16(g, 0x084, 0x80);            /* SOUNDCNT_X: master on (suite does this first) */
    char line[256];
    int pass = 0, total = 0, openbus = 0, maskfail = 0;
    while (fgets(line, sizeof line, f)) {
        char* fields[6]; int k = 0;
        for (char* tok = strtok(line, "|\n"); tok && k < 6; tok = strtok(NULL, "|\n"))
            fields[k++] = tok;
        if (k < 6) continue;
        const char* name = fields[0];
        unsigned reg = (unsigned)strtol(fields[1], 0, 0) & 0x3FE;
        int store = !strcmp(fields[2], "true");
        unsigned exp = (unsigned)strtol(fields[4], 0, 0);
        unsigned wr = 0xFFFF;
        if (reg == 0x128) wr = (io_read16(g, 0x128) & 0x3000) | 0xCFFF;
        else if (reg == 0x134) wr = (io_read16(g, 0x134) & 0xC000) | 0x3FFF;
        if (store) io_write16(g, reg, wr);
        unsigned got = io_read16(g, reg);
        total++;
        if (got == exp) { pass++; }
        else if (exp == 0xDEAD) { openbus++; }          /* write-only: open-bus, not masking */
        else { maskfail++; printf("FAIL %-14s @%03X got=%04X exp=%04X\n", name, reg, got, exp); }
        io_write16(g, reg, 0);
    }
    printf("IO-READ: %d/%d pass; %d masking failures; %d open-bus (needs bus model)\n",
           pass, total, maskfail, openbus);
    fclose(f); free(g); return maskfail != 0;
}
