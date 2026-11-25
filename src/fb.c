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

double fb_fit_scale(int aw, int ah, int snap) {
    double sx = (double)aw / SCREEN_W, sy = (double)ah / SCREEN_H;
    double s = sx < sy ? sx : sy;
    /* Whole multiples keep every emulator pixel the same width; a fractional
       factor makes columns/rows of uneven width, which shimmers when
       scrolling. Below 1x there is no integer factor left, so keep it. */
    if (snap && s >= 1.0) s = (double)(int)s;
    return s > 0.0 ? s : 0.0;
}

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

/* font8x8_basic only covers the first 128 code points, but the frontends hand
 * us UTF-8 file names and raw ROM header bytes: indexing the table with a byte
 * >= 0x80 read past its end and drew whatever global came next, which looked
 * like random CJK glyphs.  Decode UTF-8 (or a stray Latin-1 byte), fold the
 * accented letters down to ASCII and draw '?' for the rest. */
static unsigned fold_ascii(unsigned cp) {
    static const struct { unsigned cp; char ascii; } k_fold[] = {
        {0xC0,'A'},{0xC1,'A'},{0xC2,'A'},{0xC3,'A'},{0xC4,'A'},{0xC5,'A'},
        {0xC7,'C'},{0xC8,'E'},{0xC9,'E'},{0xCA,'E'},{0xCB,'E'},{0xCC,'I'},
        {0xCD,'I'},{0xCE,'I'},{0xCF,'I'},{0xD1,'N'},{0xD2,'O'},{0xD3,'O'},
        {0xD4,'O'},{0xD5,'O'},{0xD6,'O'},{0xD9,'U'},{0xDA,'U'},{0xDB,'U'},
        {0xDC,'U'},{0xDD,'Y'},{0xE0,'a'},{0xE1,'a'},{0xE2,'a'},{0xE3,'a'},
        {0xE4,'a'},{0xE5,'a'},{0xE7,'c'},{0xE8,'e'},{0xE9,'e'},{0xEA,'e'},
        {0xEB,'e'},{0xEC,'i'},{0xED,'i'},{0xEE,'i'},{0xEF,'i'},{0xF1,'n'},
        {0xF2,'o'},{0xF3,'o'},{0xF4,'o'},{0xF5,'o'},{0xF6,'o'},{0xF9,'u'},
        {0xFA,'u'},{0xFB,'u'},{0xFC,'u'},{0xFD,'y'},{0xFF,'y'},
    };
    if (cp >= 0x20 && cp < 0x7F) return cp;
    for (size_t i = 0; i < sizeof k_fold / sizeof k_fold[0]; i++)
        if (k_fold[i].cp == cp) return (unsigned char)k_fold[i].ascii;
    return '?';
}

static unsigned next_cp(const char **ps) {
    const unsigned char *s = (const unsigned char *)*ps;
    unsigned c = *s, n = 0, cp;
    if (c < 0x80) { *ps = (const char *)s + 1; return c; }
    if ((c & 0xE0) == 0xC0) { n = 1; cp = c & 0x1F; }
    else if ((c & 0xF0) == 0xE0) { n = 2; cp = c & 0x0F; }
    else if ((c & 0xF8) == 0xF0) { n = 3; cp = c & 0x07; }
    else { *ps = (const char *)s + 1; return c; }   /* stray Latin-1 byte */
    for (unsigned i = 1; i <= n; i++) {
        if ((s[i] & 0xC0) != 0x80) { *ps = (const char *)s + 1; return c; }
        cp = (cp << 6) | (s[i] & 0x3F);
    }
    *ps = (const char *)s + n + 1;
    return cp;
}

void fb_text(FB *fb, int x, int y, const char *s, uint32_t argb) {
    for (; *s; x += 8) {
        const unsigned char *glyph =
            (const unsigned char *)font8x8_basic[fold_ascii(next_cp(&s)) & 0x7F];
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

void fb_ascii_fold(const char *in, char *out, size_t n) {
    size_t o = 0;
    if (!out || n == 0) return;
    while (in && *in && o + 1 < n)
        out[o++] = (char)fold_ascii(next_cp(&in));
    out[o] = 0;
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
            int r5 = c & 0x1F, g5 = (c >> 5) & 0x1F, b5 = (c >> 10) & 0x1F;
            /* 5->8 bit expansion: replicate the top bits so white is 0xFF
               (a plain <<3 only reaches 0xF8). */
            int r = (r5 << 3) | (r5 >> 2);
            int gg = (g5 << 3) | (g5 >> 2);
            int b = (b5 << 3) | (b5 >> 2);
            if (dim) { r >>= 1; gg >>= 1; b >>= 1; }
            drow[px] = 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)gg << 8) | (uint32_t)b;
        }
    }
}
