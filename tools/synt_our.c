/* Synthetic PPU render harness for the BoycottAdvance core: inject a
 * controlled state directly and render one frame, to compare OBJ/blend
 * behaviour pixel-exact against mGBA (see tools/synt_ref.c).
 *
 * Usage: synt_our <scenario> <outprefix>
 *   scenario: 1 = mode4 OBJ semi + brightness fade (the blend-test case)
 *             2 = mode3 OBJ semi + alpha blend
 */
#include "gba.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void setpal(GBA* g, int idx, int r5, int g5, int b5) {
    uint16_t c = (r5 & 31) | ((g5 & 31) << 5) | ((b5 & 31) << 10);
    g->pal[idx * 2 + 0] = c & 0xFF;
    g->pal[idx * 2 + 1] = c >> 8;
}

static void dump_screen(GBA* g, const char* prefix) {
    char p[512];
    snprintf(p, sizeof(p), "%s.screen.bin", prefix);
    FILE* f = fopen(p, "wb");
    fwrite(g->ppu.frame, 1, sizeof(g->ppu.frame), f);
    fclose(f);
}

static void inject_scenario1(GBA* g) {
    memset(g->vram, 0, sizeof(g->vram));
    memset(g->oam, 0, sizeof(g->oam));
    for (int i = 0; i < 256; i++) setpal(g, i, 0, 0, 0);
    g->ppu.dispcnt = 0x1404;
    g->ppu.dispstat = 0;
    /* mode 4 page0: 8-bit palette indices; left = 3 (red), right = 4 (green) */
    for (uint32_t i = 0; i < 240 * 160; i++)
        g->vram[i] = (i % 240 < 120) ? 3 : 4;
    setpal(g, 0, 31, 31, 31);
    setpal(g, 3, 31, 0, 0);
    setpal(g, 4, 0, 31, 0);
    /* OBJ 8x8 at (100,60), tile 0, semi (omode=1, shape=0, size=0) */
    g->oam[0] = 60; g->oam[1] = (1 << 2) | (0);      /* y=60, omode=1(semi bit2?), shape */
    /* GBA OAM attr0: y (0-7), rot/scale(8), size(10), 0, semitrans(12), window(13),
     * shape(14),  (15)=0 */
    uint16_t a0 = 60 | (1 << 10) | (0 << 14);        /* y=60, semitrans, shape=0(square) */
    uint16_t a1 = 100 | (0 << 14);                   /* x=100, size=0 (8x8) */
    uint16_t a2 = 0;                                 /* tile 0 */
    g->oam[0] = a0 & 0xFF; g->oam[1] = a0 >> 8;
    g->oam[2] = a1 & 0xFF; g->oam[3] = a1 >> 8;
    g->oam[4] = a2 & 0xFF; g->oam[5] = a2 >> 8;
    /* OBJ tile 0 in VRAM 0x10000: 8x8 4bpp, all pixels = colour 1 */
    for (int y = 0; y < 8; y++) {
        uint16_t row = 0x1111;
        g->vram[0x10000 + y * 4 + 0] = row & 0xFF;
        g->vram[0x10000 + y * 4 + 1] = row >> 8;
        g->vram[0x10000 + y * 4 + 2] = row & 0xFF;
        g->vram[0x10000 + y * 4 + 3] = row >> 8;
    }
    setpal(g, 0x100 + 0, 0, 0, 0);   /* transparent */
    setpal(g, 0x100 + 1, 0, 0, 31);  /* blue OBJ */
    /* brightness fade: first target BG2 (0x04), mode=2 (0x40) => 0x02C4 */
    g->ppu.bldcnt = 0x02C4;
    g->ppu.bldalpha = 0x0000;
    g->ppu.bldy = 0x000A;
}

static void inject_scenario2(GBA* g) {
    memset(g->vram, 0, sizeof(g->vram));
    memset(g->oam, 0, sizeof(g->oam));
    for (int i = 0; i < 256; i++) setpal(g, i, 0, 0, 0);
    g->ppu.dispcnt = 0x1F03;
    g->ppu.dispstat = 0;
    /* mode 3 VRAM direct RGB555: left cyan (0x03FF), right magenta (0x7C1F) */
    for (uint32_t i = 0; i < 240 * 160; i++) {
        uint16_t c = (i % 240 < 120) ? 0x03FF : 0x7C1F;
        g->vram[i * 2 + 0] = c & 0xFF;
        g->vram[i * 2 + 1] = c >> 8;
    }
    setpal(g, 0, 31, 31, 31);
    /* OBJ 8x8 at (100,60), tile 0, semi */
    uint16_t a0 = 60 | (1 << 10) | (0 << 14);
    uint16_t a1 = 100 | (0 << 14);
    uint16_t a2 = 0;
    g->oam[0] = a0 & 0xFF; g->oam[1] = a0 >> 8;
    g->oam[2] = a1 & 0xFF; g->oam[3] = a1 >> 8;
    g->oam[4] = a2 & 0xFF; g->oam[5] = a2 >> 8;
    for (int y = 0; y < 8; y++) {
        uint16_t row = 0x1111;
        g->vram[0x10000 + y * 4 + 0] = row & 0xFF;
        g->vram[0x10000 + y * 4 + 1] = row >> 8;
        g->vram[0x10000 + y * 4 + 2] = row & 0xFF;
        g->vram[0x10000 + y * 4 + 3] = row >> 8;
    }
    setpal(g, 0x100 + 0, 0, 0, 0);
    setpal(g, 0x100 + 1, 31, 31, 0);  /* yellow OBJ */
    /* alpha blend: mode=1 (0x40), targ1=OBJ (0x10), targ2=BG2 (0x04 -> <<8) => 0x0450 */
    g->ppu.bldcnt = 0x0450;
    g->ppu.bldalpha = (15) | (2 << 8);
    g->ppu.bldy = 0;
}

int main(int argc, char** argv) {
    if (argc < 3) { fprintf(stderr, "usage: %s <1|2> <outprefix>\n", argv[0]); return 2; }
    GBA* g = calloc(1, sizeof(GBA));
    gba_load_rom(g, "/workspace/BA/tmp_donut.gba");
    gba_reset(g);

    switch (atoi(argv[1])) {
    case 1: inject_scenario1(g); break;
    case 2: inject_scenario2(g); break;
    default: fprintf(stderr, "unknown scenario %s\n", argv[1]); return 1;
    }

    ppu_render_frame(g);   /* render full frame with injected state */
    dump_screen(g, argv[2]);
    printf("dispcnt=%04X bldcnt=%04X\n", g->ppu.dispcnt, g->ppu.bldcnt);
    return 0;
}