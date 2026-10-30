/*
 * headless.c - dump video/audio artifacts from the SAME core the .exe uses.
 *
 *   headless <rom> <frames> <pngdir> <wav> [bios]
 *
 * Renders frames 60/300/600 to PNG (RGB888, 5->8 bit expansion identical to
 * fb.c) and writes <frames> of stereo s16le to a WAV, so the graphics and
 * audio can be compared against VBA-M / mGBA without a display.
 *
 * Built from the same sources as the application (gba/mem/arm/ppu/apu/bios/
 * eeprom/flash/fb), so any bug that shows here is present in the .exe.
 */
#include "gba.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

/* ---- PNG writer (RGB888, zlib deflate) --------------------------------- */
static void be32(uint8_t *p, uint32_t v) { p[0]=v>>24; p[1]=v>>16; p[2]=v>>8; p[3]=v; }
static uint32_t crc_tab[256];
static void crc_init(void) {
    for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crc_tab[n] = c;
    }
}
static uint32_t crc_buf(const uint8_t *b, size_t n, uint32_t c) {
    for (size_t i = 0; i < n; i++) c = crc_tab[(c ^ b[i]) & 0xFF] ^ (c >> 8);
    return c;
}
static void chunk(FILE *f, const char *type, const uint8_t *data, uint32_t len) {
    uint8_t hdr[8]; be32(hdr, len); memcpy(hdr + 4, type, 4);
    fwrite(hdr, 1, 8, f); fwrite(data, 1, len, f);
    uint32_t c = crc_buf((const uint8_t *)type, 4, 0xFFFFFFFFu);
    c = crc_buf(data, len, c) ^ 0xFFFFFFFFu;
    uint8_t cb[4]; be32(cb, c); fwrite(cb, 1, 4, f);
}
static int write_png(const char *path, const uint8_t *rgb, int w, int h) {
    FILE *f = fopen(path, "wb"); if (!f) return -1;
    static const uint8_t sig[8] = {137,80,78,71,13,10,26,10};
    fwrite(sig, 1, 8, f);
    uint8_t ihdr[13]; be32(ihdr, w); be32(ihdr + 4, h);
    ihdr[8]=8; ihdr[9]=2; ihdr[10]=0; ihdr[11]=0; ihdr[12]=0;   /* 8-bit RGB */
    chunk(f, "IHDR", ihdr, 13);
    /* raw scanlines: filter byte 0 + RGB triplets */
    size_t rawlen = (size_t)h * (1 + (size_t)w * 3);
    uint8_t *raw = malloc(rawlen);
    for (int y = 0; y < h; y++) {
        uint8_t *row = raw + (size_t)y * (1 + (size_t)w * 3);
        row[0] = 0;
        memcpy(row + 1, rgb + (size_t)y * w * 3, (size_t)w * 3);
    }
    uLongf clen = compressBound(rawlen);
    uint8_t *comp = malloc(clen);
    compress2(comp, &clen, raw, rawlen, 9);
    chunk(f, "IDAT", comp, (uint32_t)clen);
    chunk(f, "IEND", NULL, 0);
    free(raw); free(comp); fclose(f);
    return 0;
}

/* ---- RGB555 -> RGB888 with the same top-bit replication as fb.c -------- */
static void frame_to_rgb(const uint16_t *px, uint8_t *rgb) {
    for (int i = 0; i < SCREEN_W * SCREEN_H; i++) {
        uint16_t c = px[i];
        uint8_t r = (uint8_t)(c & 31), g = (uint8_t)((c >> 5) & 31), b = (uint8_t)((c >> 10) & 31);
        rgb[i*3+0] = (uint8_t)((r << 3) | (r >> 2));
        rgb[i*3+1] = (uint8_t)((g << 3) | (g >> 2));
        rgb[i*3+2] = (uint8_t)((b << 3) | (b >> 2));
    }
}

static void wr_le16(FILE *f, uint16_t v) { fputc(v & 0xFF, f); fputc(v >> 8, f); }
static void wr_le32(FILE *f, uint32_t v) { for (int i = 0; i < 4; i++) fputc((v >> (8*i)) & 0xFF, f); }

int main(int argc, char **argv) {
    if (argc < 5) { fprintf(stderr, "usage: %s <rom> <frames> <pngdir> <wav> [bios]\n", argv[0]); return 2; }
    const char *rom = argv[1];
    int nframes = atoi(argv[2]);
    const char *pngdir = argv[3];
    const char *wavpath = argv[4];

    GBA *g = calloc(1, sizeof(GBA));
    if (!gba_load_rom(g, rom)) { fprintf(stderr, "load rom fail\n"); return 1; }
    if (argc >= 6) gba_load_bios(g, argv[5]);
    g->bios_fastboot = false;
    gba_reset(g);
    apu_open();

    crc_init();

    /* WAV: 44100 Hz, stereo, s16le. Header patched at the end. */
    FILE *wav = fopen(wavpath, "wb");
    for (int i = 0; i < 44; i++) fputc(0, wav);
    uint32_t samples = 0;
    int16_t buf[4096];

    static const int shots[] = {60, 300, 600};
    uint8_t *rgb = malloc((size_t)SCREEN_W * SCREEN_H * 3);

    for (int f = 0; f < nframes; f++) {
        gba_run_frame(g);
        int n;
        while ((n = apu_drain(buf, 2048)) > 0) {
            fwrite(buf, sizeof(int16_t) * 2, (size_t)n, wav);
            samples += (uint32_t)n;
        }
        for (int s = 0; s < 3; s++) {
            if (f == shots[s]) {
                frame_to_rgb(g->ppu.frame, rgb);
                char p[512];
                snprintf(p, sizeof(p), "%s/frame_%d.png", pngdir, shots[s]);
                write_png(p, rgb, SCREEN_W, SCREEN_H);
                printf("wrote %s\n", p);
            }
        }
    }

    /* patch WAV header */
    fseek(wav, 0, SEEK_SET);
    uint32_t datalen = samples * 4;
    fwrite("RIFF", 1, 4, wav); wr_le32(wav, 36 + datalen); fwrite("WAVE", 1, 4, wav);
    fwrite("fmt ", 1, 4, wav); wr_le32(wav, 16); wr_le16(wav, 1); wr_le16(wav, 2);
    wr_le32(wav, 44100); wr_le32(wav, 44100 * 4); wr_le16(wav, 4); wr_le16(wav, 16);
    fwrite("data", 1, 4, wav); wr_le32(wav, datalen);
    fclose(wav);
    printf("wrote %s (%u frames, %.2f s)\n", wavpath, samples, samples / 44100.0);
    free(rgb); free(g);
    return 0;
}
