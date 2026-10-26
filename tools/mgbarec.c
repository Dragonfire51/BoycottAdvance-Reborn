/* mGBA reference video recorder: run a ROM for N frames and write each frame
 * as a raw RGB24 stream (240x160), for exact pixel comparison with our core.
 *
 * Build (same defines the lib was built with):
 *   gcc -std=c11 -DENABLE_DEBUGGERS -DENABLE_DIRECTORIES -DENABLE_VFS \
 *       -D_GNU_SOURCE -I/workspace/mgba/include tools/mgbarec.c \
 *       -L/workspace/mgba/build -lmgba -lm -o /tmp/mgbarec
 * Usage: mgbarec <rom> <frames> <out.rgb> [bios]
 */
#include <mgba/core/core.h>
#include <mgba/gba/core.h>
#include <mgba/core/interface.h>
#include <mgba/core/config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <mgba-util/vfs.h>
#include <mgba-util/audio-buffer.h>
#include <mgba/core/log.h>
#include <stdarg.h>

/* Silence mGBA's verbose logging (it defaults to stdout and floods output). */
static void quiet_log(struct mLogger* logger, int category, enum mLogLevel level,
                      const char* format, va_list args) {
    (void) logger; (void) category; (void) level; (void) format; (void) args;
}
static struct mLogger g_quiet = { .log = quiet_log };

int main(int argc, char** argv) {
    if (argc < 4) { fprintf(stderr, "usage: %s <rom> <frames> <out.rgb> [bios]\n", argv[0]); return 2; }
    mLogSetDefaultLogger(&g_quiet);
    const char* rom = argv[1];
    int nframes = atoi(argv[2]);
    FILE* vo = fopen(argv[3], "wb");
    if (!vo) { fprintf(stderr, "cannot open %s\n", argv[3]); return 1; }

    struct mCore* core = mCoreFind(rom);
    if (!core) { fprintf(stderr, "no core\n"); return 1; }
    core->init(core);
    mCoreConfigInit(&core->config, "core");

    static mColor fb[240 * 160];
    core->setVideoBuffer(core, fb, 240);

    if (!mCoreLoadFile(core, rom)) { fprintf(stderr, "load fail\n"); return 1; }
    if (argc > 4 && strcmp(argv[4], "-") != 0) {
        struct VFile* vf = VFileOpen(argv[4], O_RDONLY);
        if (vf) { core->loadBIOS(core, vf, 0); fprintf(stderr, "BIOS %s\n", argv[4]); }
    }
    core->reset(core);

    if (getenv("FASTBOOT")) {
        /* mirror our fastboot state: forced blank + VBlank IRQ armed */
        core->rawWrite16(core, 0x04000000, 0, 0x0080);
        core->rawWrite16(core, 0x04000004, 0, 0x0008);
        core->rawWrite16(core, 0x04000200, 0, 0x0001);
        core->rawWrite16(core, 0x04000208, 0, 0x0001);
    }

    unsigned char rgb[240 * 160 * 3];
    FILE *ao = getenv("AUDIO") ? fopen(getenv("AUDIO"), "wb") : NULL;
    for (int f = 0; f < nframes; f++) {
        core->runFrame(core);
        if (ao && core->getAudioBuffer) {
            static int16_t pcm[8192];
            struct mAudioBuffer *ab = core->getAudioBuffer(core);
            size_t got;
            while ((got = mAudioBufferRead(ab, pcm, 8192)) > 0)
                fwrite(pcm, sizeof(int16_t) * 2, got, ao);
        }
        const void* pix; size_t stride;
        core->getPixels(core, &pix, &stride);
        const uint32_t* p = pix;
        for (int y = 0; y < 160; y++)
            for (int x = 0; x < 240; x++) {
                uint32_t c = p[y * (stride / 4) + x];
                int i = (y * 240 + x) * 3;
                rgb[i + 0] = (unsigned char)(c & 0xFF);
                rgb[i + 1] = (unsigned char)((c >> 8) & 0xFF);
                rgb[i + 2] = (unsigned char)((c >> 16) & 0xFF);
            }
        fwrite(rgb, 1, sizeof(rgb), vo);
    }
    fclose(vo);
    if (ao) fclose(ao);
    fprintf(stderr, "wrote %d frames\n", nframes);
    return 0;
}
