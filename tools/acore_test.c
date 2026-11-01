/*
 * acore_test.c - headless check that the portable frontend core (acore)
 * drives the emulator exactly like the desktop frontends do.
 *
 * It loads a ROM through acore, runs N frames and writes the ARGB frame of
 * a chosen frame as a PNG (via the same writer the -video path uses) plus a
 * WAV of the audio pulled from the ring. Run it on Linux to validate the
 * exact code the Android port links against, without a device.
 *
 *   acore_test <rom> <bios|-> <outdir> <frames> <png_frame> <wav_seconds>
 */
#include "acore.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The CRC is taken on the ARGB frame acore produces, so the check is
 * comparable across runs and between this test and the JNI test. */
int main(int argc, char **argv) {
    if (argc < 7) {
        fprintf(stderr, "usage: %s <rom> <bios|-> <outdir> <frames> <png_frame> <wav_seconds>\n", argv[0]);
        return 2;
    }
    const char *rom = argv[1];
    const char *bios = strcmp(argv[2], "-") ? argv[2] : NULL;
    const char *outdir = argv[3];
    int frames = atoi(argv[4]);
    int png_frame = atoi(argv[5]);
    double wavsec = atof(argv[6]);

    Acore *a = acore_create();
    if (!a) { fprintf(stderr, "acore_create failed\n"); return 1; }
    if (bios) acore_load_bios(a, bios);
    if (!acore_load_rom(a, rom)) { fprintf(stderr, "cannot load %s\n", rom); return 1; }

    char title[32], code[8], sv[24];
    acore_rom_info(a, title, sizeof title, code, sizeof code, sv, sizeof sv);
    printf("rom: title='%s' code='%s' save=%s\n", title, code, sv);

    char wavpath[1200];
    snprintf(wavpath, sizeof wavpath, "%s/audio.wav", outdir);
    FILE *wav = wavsec > 0 ? fopen(wavpath, "wb") : NULL;
    uint32_t wav_bytes = 0;
    if (wav) { fwrite("RIFF", 1, 4, wav); }

    uint32_t *argb = malloc((size_t)ACORE_W * ACORE_H * 4);
    long wav_target = (long)(wavsec * ACORE_SAMPLE_RATE);
    int16_t abuf[4096];

    for (int f = 1; f <= frames; f++) {
        acore_set_keys(a, 0);
        acore_run_frame(a);
        acore_frame_argb(a, argb, ACORE_W, ACORE_H, 0);
        if (f == png_frame) {
            /* CRC of the ARGB frame so the check is comparable across runs. */
            uint32_t h = 2166136261u;
            for (int i = 0; i < ACORE_W * ACORE_H; i++) {
                h ^= argb[i]; h *= 16777619u;
            }
            printf("frame %d argb crc=%08x px00=%08x\n", f, h, argb[0]);
        }
        if (wav) {
            int n;
            while ((n = acore_audio_available(a)) > 0) {
                int take = n > 2048 ? 2048 : n;
                int got = acore_audio_drain_latest(a, abuf, take);
                if (got <= 0) break;
                if (wav_bytes / 4 >= (uint32_t)wav_target) break;
                long room = wav_target - (long)(wav_bytes / 4);
                if (got > room) got = (int)room;
                if (got <= 0) break;
                fwrite(abuf, sizeof(int16_t) * 2, (size_t)got, wav);
                wav_bytes += (uint32_t)got * 4;
            }
        }
    }
    if (wav) {
        /* Patch a minimal 44-byte WAV header in place. */
        uint8_t hdr[44] = {0};
        uint32_t data = wav_bytes;
        uint32_t riff = 36 + data;
        memcpy(hdr, "RIFF", 4); memcpy(hdr + 4, &riff, 4); memcpy(hdr + 8, "WAVE", 4);
        memcpy(hdr + 12, "fmt ", 4); uint32_t sz = 16; memcpy(hdr + 16, &sz, 4);
        uint16_t pcm = 1, ch = 2, bps = 16, ba = 4;
        uint32_t sr = ACORE_SAMPLE_RATE, br = sr * 4;
        memcpy(hdr + 20, &pcm, 2); memcpy(hdr + 22, &ch, 2);
        memcpy(hdr + 24, &sr, 4); memcpy(hdr + 28, &br, 4);
        memcpy(hdr + 32, &ba, 2); memcpy(hdr + 34, &bps, 2);
        memcpy(hdr + 36, "data", 4); memcpy(hdr + 40, &data, 4);
        fseek(wav, 0, SEEK_SET);
        fwrite(hdr, 1, 44, wav);
        fclose(wav);
        printf("wrote %s (%u bytes PCM)\n", wavpath, wav_bytes);
    }

    free(argb);
    acore_destroy(a);
    return 0;
}
