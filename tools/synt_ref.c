/* Synthetic PPU render harness for mGBA: inject a controlled state into the
 * core and render one frame, to compare OBJ/blend behaviour pixel-exact.
 *
 * Usage: synt_ref <scenario> [rom]
 *   scenario: 1 = mode4 OBJ semi + brightness fade (the blend-test case)
 *             2 = mode3 OBJ semi + alpha blend
 * The board runs 0 extra frames on a donut ROM; state is injected by direct
 * bus writes so nothing can overwrite it mid-frame.
 */
#include <mgba/core/core.h>
#include <mgba/core/interface.h>
#include <mgba/core/config.h>
#include <mgba-util/audio-buffer.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Bufs { uint16_t* io; uint8_t* vram; uint8_t* oam; uint8_t* pal; };

static void getbufs(struct mCore* core, struct Bufs* b) {
    size_t sz = 0;
    b->io   = core->getMemoryBlock(core, 4, &sz);
    b->vram = core->getMemoryBlock(core, 6, &sz);
    b->oam  = core->getMemoryBlock(core, 7, &sz);
    b->pal  = core->getMemoryBlock(core, 5, &sz);
}

static void setpal16(struct Bufs* b, int idx, uint16_t c) {
    b->pal[idx * 2 + 0] = c & 0xFF;
    b->pal[idx * 2 + 1] = c >> 8;
}

static void dump_buffer(const mColor* fb, const char* path) {
    FILE* f = fopen(path, "wb");
    fprintf(f, "P6\n240 160\n255\n");
    for (int y = 0; y < 160; y++)
        for (int x = 0; x < 240; x++) {
            uint32_t c = fb[y * 240 + x];
            unsigned char rgb[3];
            rgb[0] = c & 0xFF; rgb[1] = (c >> 8) & 0xFF; rgb[2] = (c >> 16) & 0xFF;
            fwrite(rgb, 1, 3, f);
        }
    fclose(f);
}

static void dump_pixels(struct mCore* core, const char* path) {
    const void* pix; size_t stride;
    core->getPixels(core, &pix, &stride);
    const uint32_t* p = pix;
    FILE* f = fopen(path, "wb");
    fprintf(f, "P6\n240 160\n255\n");
    for (int y = 0; y < 160; y++)
        for (int x = 0; x < 240; x++) {
            uint32_t c = p[y * (stride / 4) + x];
            unsigned char rgb[3];
            rgb[0] = c & 0xFF; rgb[1] = (c >> 8) & 0xFF; rgb[2] = (c >> 16) & 0xFF;
            fwrite(rgb, 1, 3, f);
        }
    fclose(f);
}

static void inject_scenario1(struct Bufs* b) {
    /* mode 4: bitmap page0 uses 8-bit palette indices; our donut sets DISPCNT=0x1404.
       Here we only fill data memory (VRAM/OAM/PAL). */
    for (uint32_t i = 0; i < 240 * 160; i++)
        b->vram[i] = i % 240 < 120 ? 3 : 4;
    setpal16(b, 0, 0x7FFF);
    setpal16(b, 3, 0x001F);   /* red */
    setpal16(b, 4, 0x03E0);   /* green */
    /* OBJ 8x8 at (100,60), tile 0, semi (attr0 bits10-11=1), shape=0 */
    b->oam[0] = 60; b->oam[1] = 0x04;
    b->oam[2] = 100; b->oam[3] = 0;
    b->oam[4] = 0; b->oam[5] = 0;
    for (int y = 0; y < 8; y++) {
        uint16_t row = 0x1111;
        b->vram[0x10000 + y * 4 + 0] = row & 0xFF;
        b->vram[0x10000 + y * 4 + 1] = row >> 8;
        b->vram[0x10000 + y * 4 + 2] = row & 0xFF;
        b->vram[0x10000 + y * 4 + 3] = row >> 8;
    }
    setpal16(b, 0x100 + 0, 0x0000);
    setpal16(b, 0x100 + 1, 0x001F);   /* blue OBJ */
}

static void inject_scenario2(struct Bufs* b) {
    /* mode 3: VRAM direct RGB555; donut sets DISPCNT=0x1404; we override later? */
    for (uint32_t i = 0; i < 240 * 160; i++) {
        uint16_t c = (i % 240 < 120) ? 0x03FF : 0x7C1F;
        b->vram[i * 2 + 0] = c & 0xFF;
        b->vram[i * 2 + 1] = c >> 8;
    }
    /* OBJ 8x8 at (100,60), tile 0, semi */
    b->oam[0] = 60; b->oam[1] = 0x04;
    b->oam[2] = 100; b->oam[3] = 0;
    b->oam[4] = 0; b->oam[5] = 0;
    for (int y = 0; y < 8; y++) {
        uint16_t row = 0x1111;
        b->vram[0x10000 + y * 4 + 0] = row & 0xFF;
        b->vram[0x10000 + y * 4 + 1] = row >> 8;
        b->vram[0x10000 + y * 4 + 2] = row & 0xFF;
        b->vram[0x10000 + y * 4 + 3] = row >> 8;
    }
    setpal16(b, 0x100 + 0, 0x0000);
    setpal16(b, 0x100 + 1, 0x03FF);   /* yellow OBJ */
}

static void inject_no_blend(struct Bufs* b) {
    (void)b;
}

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s <1|2|3> [rom]\n", argv[0]); return 2; }
    int sc = atoi(argv[1]);
    const char* rom = NULL;
    if (sc == 1)      rom = "/workspace/BA/tmp_donut_m4.gba";
    else if (sc == 2) rom = "/workspace/BA/tmp_donut_m3.gba";
    else              rom = "/workspace/BA/tmp_donut_plain.gba";
    if (argc > 2) rom = argv[2];
    struct mCore* core = mCoreFind(rom);
    if (!core) { fprintf(stderr, "no core\n"); return 1; }
    core->init(core);
    mCoreConfigInit(&core->config, "core");
    mCoreConfigSetValue(&core->config, "skipBios", "1");
    static mColor fb[240 * 160];
    if (core->setVideoBuffer) core->setVideoBuffer(core, fb, 240);
    if (!mCoreLoadFile(core, rom)) { fprintf(stderr, "load fail\n"); return 1; }
    core->reset(core);

    {
        int32_t pcv = 0;
        if (core->readRegister(core, "r15", &pcv)) printf("after reset r15=%08X\n", (unsigned)pcv);
        else if (core->readRegister(core, "pc", &pcv)) printf("after reset pc=%08X\n", (unsigned)pcv);
        else printf("no pc register\n");
    }

    /* Inject VRAM/OAM/PAL (these persist; I/O regs are written by the ARM in ROM) */
    struct Bufs b;
    getbufs(core, &b);
    switch (sc) {
    case 1: inject_scenario1(&b); break;
    case 2: inject_scenario2(&b); break;
    case 3: inject_scenario2(&b); inject_no_blend(&b); break;
    default: fprintf(stderr, "unknown scenario %s\n", argv[1]); return 1;
    }

    /* The donut wrote its regs at boot; runFrame draws while the CPU spins in B . */
    for (int i = 0; i < 6; i++) core->runFrame(core);

    dump_buffer(fb, "/tmp/synt_ref.ppm");
    printf("pre-dump getpix=%08X fb0=%08X dispcnt=%04X bldcnt=%04X\n",
           *(const uint32_t*)&fb[0], (unsigned)fb[0],
           core->rawRead16(core, 0x04000000, 0),
           core->rawRead16(core, 0x04000050, 0));

    if (core->listMemoryBlocks) {
        const struct mCoreMemoryBlock* mb;
        size_t n = core->listMemoryBlocks(core, &mb);
        for (size_t i = 0; i < (n > 8 ? 8 : n); i++)
            printf("block %zu: %s @%08X len %08zX\n", i, mb[i].internalName ? mb[i].internalName : "?", mb[i].start, (size_t)mb[i].size);
    }

    {
        const void* pix; size_t stride;
        core->getPixels(core, &pix, &stride);
        printf("last getPixels=%08X\n",
               (unsigned)((const uint32_t*)pix)[0]);
    }
    printf("dispcnt=%04X bldcnt=%04X\n",
           core->rawRead16(core, 0x04000000, 0),
           core->rawRead16(core, 0x04000050, 0));
    printf("vram[0..3]=%04X %04X %04X %04X oam0=%04X pal3=%04X\n",
           core->busRead16(core, 0x06000000), core->busRead16(core, 0x06000002),
           core->busRead16(core, 0x06000004), core->busRead16(core, 0x06000006),
           core->busRead16(core, 0x07000000),
           core->busRead16(core, 0x05000000 + 3 * 2));
    core->deinit(core);
    return 0;
}