/* mGBA comparison harness: load ROM, run N frames, dump framebuffer (PPM),
 * audio IO, and V/O memory.
 *
 * Compile with the same -D macros the lib was built with so struct mCore matches:
 *   gcc -std=c11 -DENABLE_DEBUGGERS -DENABLE_DIRECTORIES -DENABLE_VFS \
 *       -D_GNU_SOURCE -I/workspace/mgba/include tools/mgbaharness.c \
 *       -L/workspace/mgba/build -lmgba -lm
 *
 * Usage:
 *   mgbaharness <rom> <frames> [audio.pcm] [frame.ppm]
 * Env:
 *   FASTBOOT=1  : write forced-blank + VBlank IRQ to mimic our fastboot boot
 *   DUMPDIR=<d> : dump vram.bin / pal.bin / oam.bin through the CPU bus
 */
#include <mgba/core/core.h>
#include <mgba/gba/core.h>
#include <mgba/core/interface.h>
#include <mgba/core/config.h>
#include <mgba-util/audio-buffer.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <mgba-util/vfs.h>

int main(int argc, char** argv) {
    if (argc < 3) { fprintf(stderr, "usage: %s <rom> <frames> [audio.pcm] [frame.ppm]\n", argv[0]); return 2; }
    const char* rom = argv[1];
    int nframes = atoi(argv[2]);

    struct mCore* core = mCoreFind(rom);
    if (!core) { fprintf(stderr, "no core for %s\n", rom); return 1; }
    core->init(core);

    mCoreConfigInit(&core->config, "core");
    const char* skipbios = getenv("SKIPBIOS");
    if (skipbios) mCoreConfigSetValue(&core->config, "skipBios", skipbios);

    static mColor fb[240 * 160];
    if (core->setVideoBuffer) {
        core->setVideoBuffer(core, fb, 240);
    }

    if (!mCoreLoadFile(core, rom)) { fprintf(stderr, "load fail\n"); return 1; }
    const char* biospath = getenv("BIOS");
    if (biospath) {
        struct VFile* vf = VFileOpen(biospath, O_RDONLY);
        if (vf) {
            core->loadBIOS(core, vf, 0);
            printf("loaded BIOS %s\n", biospath);
        } else {
            fprintf(stderr, "cannot open BIOS %s\n", biospath);
        }
    }
    core->reset(core);

    if (getenv("FASTBOOT")) {
        /* replicate our fastboot state: forced blank + VBlank IRQ armed */
        core->rawWrite16(core, 0x04000000, 0, 0x0080);
        core->rawWrite16(core, 0x04000004, 0, 0x0008);
        core->rawWrite16(core, 0x04000200, 0, 0x0001); /* IE */
        core->rawWrite16(core, 0x04000208, 0, 0x0001); /* IME */
    }

    long aenergy = 0; size_t atotal = 0;
    FILE* af = (argc > 3 && strcmp(argv[3], "/dev/null")) ? fopen(argv[3], "wb") : NULL;
    FILE* rec = NULL;
    if (getenv("RECORD")) {
        char rp[512];
        snprintf(rp, sizeof(rp), "%s.rgb", getenv("RECORD"));
        rec = fopen(rp, "wb");
    }
    const char* findshot = getenv("FIND_SHOT");
    const char* reglog = getenv("REGLOG");
    FILE* rf = reglog ? fopen(reglog, "w") : NULL;
    for (int f = 0; f < nframes; f++) {
        const char* wo = getenv("SETWINOUT");
        if (wo && f == atoi(getenv("SETWINOUT_FRAME") ? getenv("SETWINOUT_FRAME") : "0")) {
            core->rawWrite16(core, 0x04000048, 0, (uint16_t) strtol(wo, NULL, 16));
        }
        const char* oamz = getenv("OAMZERO");
        if (oamz && f == atoi(oamz)) {
            /* OAMZERO=<frame>: zero OAM entries 16..24 (0x07000080..0x070000C7)
               to test which OBJ-window sprites drive the compositor. */
            for (uint32_t a = 0x07000080; a < 0x07000080 + 9 * 8; a++) {
                core->rawWrite8(core, a, 0, 0);
            }
        }
        core->runFrame(core);
        if (rec) {
            const void* pix; size_t stride;
            core->getPixels(core, &pix, &stride);
            const uint32_t* p = pix;
            static unsigned char rgb[240 * 160 * 3];
            for (int i = 0; i < 240 * 160; i++) {
                uint32_t c = p[i];
                rgb[3 * i + 0] = c & 0xFF; rgb[3 * i + 1] = (c >> 8) & 0xFF; rgb[3 * i + 2] = (c >> 16) & 0xFF;
            }
            fwrite(rgb, 1, sizeof(rgb), rec);
        }
        if (rf) {
            fprintf(rf, "f%d dispcnt=%04X dispstat=%04X if=%04X ie=%04X ime=%d",
                    (int) core->frameCounter(core),
                    core->rawRead16(core, 0x04000000, 0),
                    core->rawRead16(core, 0x04000004, 0),
                    core->rawRead16(core, 0x04000202, 0),
                    core->rawRead16(core, 0x04000200, 0),
                    core->rawRead16(core, 0x04000208, 0) & 0x80 ? 1 : 0);
            fprintf(rf, " bgcnt=");
            for (uint32_t a = 0x04000008; a <= 0x0400000E; a += 2)
                fprintf(rf, "%04X,", core->rawRead16(core, a, 0));
            fprintf(rf, " scroll=");
            for (uint32_t a = 0x04000010; a <= 0x0400001E; a += 2)
                fprintf(rf, "%04X,", core->rawRead16(core, a, 0));
            fprintf(rf, " rot=");
            for (uint32_t a = 0x04000020; a <= 0x0400003E; a += 2)
                fprintf(rf, "%04X,", core->rawRead16(core, a, 0));
            fprintf(rf, " win=%04X,%04X,%04X,%04X,%04X,%04X,%04X,%04X,%04X",
                    core->rawRead16(core, 0x04000040, 0),
                    core->rawRead16(core, 0x04000042, 0),
                    core->rawRead16(core, 0x04000044, 0),
                    core->rawRead16(core, 0x04000046, 0),
                    core->rawRead16(core, 0x04000048, 0),
                    core->rawRead16(core, 0x0400004A, 0),
                    core->rawRead16(core, 0x0400004C, 0),
                    core->rawRead16(core, 0x04000050, 0),
                    core->rawRead16(core, 0x04000052, 0));
            fprintf(rf, " oam0=%04X%04X%04X",
                    core->rawRead16(core, 0x07000000, 0),
                    core->rawRead16(core, 0x07000002, 0),
                    core->rawRead16(core, 0x07000004, 0));
            fprintf(rf, "\n");
        }
        if (findshot && f > 0) {
            if (!(core->rawRead16(core, 0x04000000, 0) & 0x0080)) {
                const void* pix; size_t stride;
                core->getPixels(core, &pix, &stride);
                const uint32_t* p = pix;
                int nonblack = 0;
                for (int i = 0; i < 240 * 160; i++)
                    if (((const uint32_t*)p)[i]) nonblack++;
                if (nonblack > 1000) {
                    FILE* fo = fopen(argv[4], "wb");
                    if (fo) {
                        fprintf(fo, "P6\n240 160\n255\n");
                        for (int y = 0; y < 160; y++)
                            for (int x = 0; x < 240; x++) {
                                uint32_t c = p[y * (stride / 4) + x];
                                unsigned char rgb[3];
                                rgb[0] = c & 0xFF; rgb[1] = (c >> 8) & 0xFF; rgb[2] = (c >> 16) & 0xFF;
                                fwrite(rgb, 1, 3, fo);
                            }
                        fclose(fo);
                    }
                    printf("screen-on at frame %d\n", f);
                    break;
                }
            }
        }
        if (nframes <= 4000) {
            printf("mf%d %04X\n", f, core->rawRead16(core, 0x04000000, 0));
        }
        if (core->getAudioBuffer) {
            static int16_t pcm[4096 * 2];
            struct mAudioBuffer* ab = core->getAudioBuffer(core);
            size_t got;
            while ((got = mAudioBufferRead(ab, pcm, 4096)) > 0) {
                for (size_t i = 0; i < got * 2; i++) aenergy += (long)pcm[i] * pcm[i];
                atotal += got;
                if (af) fwrite(pcm, sizeof(int16_t) * 2, got, af);
            }
        }
    }
    if (af) fclose(af);
    if (rec) fclose(rec);
    printf("audio total=%zu sr=%u aenergy=%ld\n", atotal, core->audioSampleRate(core), aenergy);

    if (argc > 4 && core->getPixels) {
        FILE* f = fopen(argv[4], "wb");
        fprintf(f, "P6\n240 160\n255\n");
        if (getenv("PPM_FROM_FB")) {
            for (int i = 0; i < 240 * 160; i++) {
                uint32_t c = fb[i];
                unsigned char rgb[3];
                rgb[0] = c & 0xFF; rgb[1] = (c >> 8) & 0xFF; rgb[2] = (c >> 16) & 0xFF;
                fwrite(rgb, 1, 3, f);
            }
        } else {
            const void* pix; size_t stride;
            core->getPixels(core, &pix, &stride);
            const uint32_t* p = pix;
            for (int y = 0; y < 160; y++)
                for (int x = 0; x < 240; x++) {
                    uint32_t c = p[y * (stride / 4) + x];
                    unsigned char rgb[3];
                    rgb[0] = c & 0xFF; rgb[1] = (c >> 8) & 0xFF; rgb[2] = (c >> 16) & 0xFF;
                    fwrite(rgb, 1, 3, f);
                }
        }
        fclose(f);
        printf("wrote %s\n", argv[4]);
    }

    const char* dd = getenv("DUMPDIR");
    if (dd) {
        char p[512];
        snprintf(p, sizeof(p), "%s/vram.bin", dd);
        FILE* f = fopen(p, "wb");
        for (uint32_t a = 0x06000000; a < 0x06018000; a++) fputc(core->busRead8(core, a), f);
        fclose(f);
        snprintf(p, sizeof(p), "%s/pal.bin", dd);
        f = fopen(p, "wb");
        for (uint32_t a = 0x05000000; a < 0x05000400; a++) fputc(core->busRead8(core, a), f);
        fclose(f);
        snprintf(p, sizeof(p), "%s/oam.bin", dd);
        f = fopen(p, "wb");
        for (uint32_t a = 0x07000000; a < 0x07000400; a++) fputc(core->busRead8(core, a), f);
        fclose(f);
        snprintf(p, sizeof(p), "%s/ewram.bin", dd);
        f = fopen(p, "wb");
        for (uint32_t a = 0x02000000; a < 0x02040000; a++) fputc(core->busRead8(core, a), f);
        fclose(f);
        snprintf(p, sizeof(p), "%s/iwram.bin", dd);
        f = fopen(p, "wb");
        for (uint32_t a = 0x03000000; a < 0x03008000; a++) fputc(core->busRead8(core, a), f);
        fclose(f);
        puts("wrote dumps");
    }

    printf("frameCounter=%u frameCycles=%d freq=%d\n",
           core->frameCounter(core), core->frameCycles(core), core->frequency(core));
    printf("audio16 io:\n");
    for (uint32_t a = 0x04000060; a <= 0x040000A8; a += 2) {
        printf(" %03X=%04X", a & 0x3FF, core->rawRead16(core, a, 0));
        if (a == 0x040000A8) printf("\n");
    }
    {
        static const uint32_t bg_offs[8] = {0x008, 0x00A, 0x00C, 0x00E, 0x010, 0x012, 0x014, 0x016};
        printf("bgcfg:");
        for (int i = 0; i < 8; i++) printf(" %03X=%04X", bg_offs[i], core->rawRead16(core, 0x04000000 + bg_offs[i], 0));
        printf("\n");
        printf("screen regs:");
        for (uint32_t a = 0x04000040; a <= 0x0400005E; a += 2)
            printf(" %03X=%04X", a, core->rawRead16(core, a, 0));
        printf("\n");
    }
    if (core->busRead16) {
        printf("dispcnt=%04X dispstat=%04X if=%04X ie=%04X ime=%d key=%04X\n",
               core->rawRead16(core, 0x04000000, 0), core->rawRead16(core, 0x04000004, 0),
               core->rawRead16(core, 0x04000202, 0), core->rawRead16(core, 0x04000200, 0),
               core->rawRead16(core, 0x04000208, 0) & 0x80 ? 1 : 0,
               core->rawRead16(core, 0x04000130, 0));
    }

    if (0) {
        FILE* rf = fopen(getenv("REGLOG"), "a");
        if (rf) {
            fprintf(rf, "f%d dispcnt=%04X dispstat=%04X if=%04X ie=%04X ime=%d",
                    (int) core->frameCounter(core),
                    core->rawRead16(core, 0x04000000, 0),
                    core->rawRead16(core, 0x04000004, 0),
                    core->rawRead16(core, 0x04000202, 0),
                    core->rawRead16(core, 0x04000200, 0),
                    core->rawRead16(core, 0x04000208, 0) & 0x80 ? 1 : 0);
            fprintf(rf, " bgcnt=");
            for (uint32_t a = 0x04000008; a <= 0x0400000E; a += 2)
                fprintf(rf, "%04X,", core->rawRead16(core, a, 0));
            fprintf(rf, " scroll=");
            for (uint32_t a = 0x04000010; a <= 0x0400001E; a += 2)
                fprintf(rf, "%04X,", core->rawRead16(core, a, 0));
            fprintf(rf, " rot=");
            for (uint32_t a = 0x04000020; a <= 0x0400003E; a += 2)
                fprintf(rf, "%04X,", core->rawRead16(core, a, 0));
            fprintf(rf, " win=%04X,%04X,%04X,%04X,%04X,%04X,%04X,%04X,%04X",
                    core->rawRead16(core, 0x04000040, 0),
                    core->rawRead16(core, 0x04000042, 0),
                    core->rawRead16(core, 0x04000044, 0),
                    core->rawRead16(core, 0x04000046, 0),
                    core->rawRead16(core, 0x04000048, 0),
                    core->rawRead16(core, 0x0400004A, 0),
                    core->rawRead16(core, 0x0400004C, 0),
                    core->rawRead16(core, 0x04000050, 0),
                    core->rawRead16(core, 0x04000052, 0));
            fprintf(rf, " oam0=%04X%04X%04X",
                    core->rawRead16(core, 0x07000000, 0),
                    core->rawRead16(core, 0x07000002, 0),
                    core->rawRead16(core, 0x07000004, 0));
            fprintf(rf, "\n");
            fclose(rf);
        }
    }
    core->deinit(core);
    return 0;
}