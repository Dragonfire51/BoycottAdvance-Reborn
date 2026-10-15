/*
 * fb.c - software framebuffer compositor (see fb.h).
 *
 * Overwrite drawing only: a pixel is written exactly once per frame, so no
 * blending and no dirty tracking are needed. That is what lets every
 * presentation backend treat the framebuffer as an opaque image.
 */
#include "fb.h"
#include "gba.h"
#include "font8x8.h"
#include <stdlib.h>
#include <string.h>

void fb_init(FB *fb, int w, int h) {
    fb->w = w; fb->h = h;
    fb->px = (uint32_t *)malloc((size_t)w * h * 4);
    if (fb->px) memset(fb->px, 0, (size_t)w * h * 4);
}
void fb_free(FB *fb) { free(fb->px); fb->px = NULL; fb->w = fb->h = 0; }

void fb_fill(FB *fb, uint32_t argb) {
    for (int i = 0; i < fb->w * fb->h; i++) fb->px[i] = argb;
}
void fb_rect(FB *fb, int x, int y, int w, int h, uint32_t argb) {
    if (w <= 0 || h <= 0) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > fb->w) w = fb->w - x;
    if (y + h > fb->h) h = fb->h - y;
    for (int j = 0; j < h; j++) {
        uint32_t *row = fb->px + (size_t)(y + j) * fb->w + x;
        for (int i = 0; i < w; i++) row[i] = argb;
    }
}
void fb_hline(FB *fb, int x, int y, int w, uint32_t argb) {
    if (y < 0 || y >= fb->h) return;
    if (x < 0) { w += x; x = 0; }
    if (x + w > fb->w) w = fb->w - x;
    uint32_t *row = fb->px + (size_t)y * fb->w + x;
    for (int i = 0; i < w; i++) row[i] = argb;
}
void fb_rect_outline(FB *fb, int x, int y, int w, int h, uint32_t argb) {
    fb_hline(fb, x, y, w, argb);
    fb_hline(fb, x, y + h - 1, w, argb);
    for (int j = 0; j < h; j++) {
        if (x >= 0 && x < fb->w && y + j >= 0 && y + j < fb->h) fb->px[(size_t)(y + j) * fb->w + x] = argb;
        int xr = x + w - 1;
        if (xr >= 0 && xr < fb->w && y + j >= 0 && y + j < fb->h) fb->px[(size_t)(y + j) * fb->w + xr] = argb;
    }
}

void fb_text(FB *fb, int x, int y, const char *s, uint32_t argb) {
    for (; *s; ++s, x += 8) {
        const unsigned char *glyph = (const unsigned char *)font8x8_basic[(unsigned char)*s];
        for (int i = 0; i < 8; i++) {
            unsigned row = glyph[i];
            for (int j = 0; j < 8; j++)
                if (row & (1u << j)) {
                    int px = x + j, py = y + i;
                    if (px >= 0 && px < fb->w && py >= 0 && py < fb->h)
                        fb->px[(size_t)py * fb->w + px] = argb;
                }
        }
    }
}

void fb_blit_frame(FB *fb, int dx, int dy, int dw, int dh,
                   const uint16_t *frame555, int scanline) {
    if (dw <= 0 || dh <= 0) return;
    for (int j = 0; j < dh; j++) {
        int py = dy + j;
        if (py < 0 || py >= fb->h) continue;
        int sy = j * SCREEN_H / dh;
        if (sy >= SCREEN_H) sy = SCREEN_H - 1;
        int dim = scanline && (sy & 1);
        const uint16_t *srow = frame555 + sy * SCREEN_W;
        uint32_t *drow = fb->px + (size_t)py * fb->w;
        for (int i = 0; i < dw; i++) {
            int px = dx + i;
            if (px < 0 || px >= fb->w) continue;
            int sx = i * SCREEN_W / dw;
            if (sx >= SCREEN_W) sx = SCREEN_W - 1;
            uint16_t c = srow[sx];
            int r = (c & 0x1F) << 3, gg = ((c >> 5) & 0x1F) << 3, b = ((c >> 10) & 0x1F) << 3;
            if (dim) { r >>= 1; gg >>= 1; b >>= 1; }
            drow[px] = 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)gg << 8) | (uint32_t)b;
        }
    }
}
