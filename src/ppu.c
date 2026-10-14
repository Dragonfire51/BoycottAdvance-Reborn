/*
 * ppu.c - GBA background/sprite rendering (modes 0-5, OBJ).
 * Clean-room implementation of the GBA PPU.
 */
#include "gba.h"
#include "mem.h"

static inline uint16_t read_pal(GBA *g, int idx) {
    return (uint16_t)(g->pal[idx*2] | (g->pal[idx*2+1] << 8));
}

static inline uint32_t read_vram16(GBA *g, uint32_t addr) {
    return (uint16_t)(g->vram[addr] | (g->vram[addr+1] << 8));
}

static inline uint32_t oa(uint32_t addr) { return addr - 0x07000000; }

/* Render a full scanline y (0..159) */
static void render_line(GBA *g, int y) {
    Ppu *p = &g->ppu;
    uint16_t dispcnt = p->dispcnt;
    int bg_mode = dispcnt & 0x7;
    bool obj_enabled = dispcnt & 0x1000;
    uint32_t base = y * SCREEN_W;

    /* backdrop */
    uint16_t backdrop = read_pal(g, 0);
    for (int x = 0; x < SCREEN_W; x++) {
        p->frame[base + x] = backdrop;
        p->bgpri[base + x] = 4;   /* highest number = lowest priority (layer 4) */
    }

    /* ---- text modes 0-2: process BG0-3 ---- */
    for (int bgn = 0; bgn < 4; bgn++) {
        if (!(dispcnt & (1 << (8 + bgn)))) continue;   /* BG disabled */
        uint16_t bgcnt = p->bgcnt[bgn];
        int pri = bgcnt & 3;
        int cbb = (bgcnt >> 2) & 3;       /* char base block */
        uint32_t sb = (bgcnt >> 8) & 0x1F;   /* screenbase block */
        bool mosaic = bgcnt & 0x40;
        int palbank = (bgn < 2) ? ((bgcnt >> 4) & 0xF) : 0;
        bool is_affine = (bg_mode == 1 && bgn == 2) || (bg_mode == 2 && bgn >= 2);
        (void)mosaic;

        if (bg_mode == 0) {
            /* 4bpp / 8bpp tile mode */
            bool is8 = bgcnt & 0x80;
            int size = (bgcnt >> 14) & 3;
            int w = 256 << (size & 1), h = 256 << ((size >> 1) & 1);
            uint32_t hofs = p->bghofs[bgn] & 0x1FF, vofs = p->bgvofs[bgn] & 0x1FF;
            for (int x = 0; x < SCREEN_W; x++) {
                int tx = (x + hofs) & (w - 1);
                int ty = (y + vofs) & (h - 1);
                int tile = tx >> 3, tline = (ty & 7);
                int tb = (tx >> 4) / 32, tt = (ty >> 3);
                int tile_idx;
                if (size == 0) {          /* 32x32 tiles */
                    tile_idx = tt * 32 + tb;
                } else if (size == 1) {   /* 64x32 */
                    tile_idx = tt * 64 + tb;
                } else if (size == 2) {   /* 32x64 */
                    tile_idx = tt * 32 + tb;
                } else {                  /* 64x64 */
                    tile_idx = tt * 64 + tb;
                }
                (void)tile; (void)tt; (void)tb;
                /* screen entry */
                uint32_t se_addr = 0x06000000 + sb * 0x800 + tile_idx * 2;
                uint16_t se = read_vram16(g, se_addr & 0x17FFFF);
                int tno = se & 0x3FF;
                int flip_x = (se >> 10) & 1, flip_y = (se >> 11) & 1;
                (void)flip_y; (void)flip_x;
                int char_add = is8 ? tno * 0x20 : tno * 0x20;
                int px, py;
                px = flip_x ? (7 - (tx & 7)) : (tx & 7);
                py = flip_y ? (7 - tline) : tline;
                uint32_t ch_addr = 0x06000000 + cbb * 0x4000 + char_add + py * (is8 ? 8 : 4) + (px >> (is8 ? 0 : 1));
                ch_addr &= 0x17FFFF;
                int palidx;
                if (is8) palidx = g->vram[ch_addr];
                else {
                    uint8_t b = g->vram[ch_addr];
                    palidx = (px & 1) ? (b >> 4) : (b & 0xF);
                    palidx |= palbank << 4;
                }
                if (palidx == 0) continue;
                uint16_t col = palidx < (is8 ? 256 : 16) ? read_pal(g, palidx) : read_pal(g, palidx);
                if (pri < p->bgpri[base + x]) {
                    p->frame[base + x] = col;
                    p->bgpri[base + x] = pri;
                }
            }
        } else if (bg_mode == 1 && bgn < 2) {
            /* same as bg0 */
            bool is8 = bgcnt & 0x80;
            int size = (bgcnt >> 14) & 3;
            int w = 256 << (size & 1), h = 256 << ((size >> 1) & 1);
            uint32_t hofs = p->bghofs[bgn] & 0x1FF, vofs = p->bgvofs[bgn] & 0x1FF;
            for (int x = 0; x < SCREEN_W; x++) {
                int tx = (x + hofs) & (w - 1), ty = (y + vofs) & (h - 1);
                int tb = tx >> 4, tt = ty >> 3;
                int tile_idx = tt * (w / 16) + tb;
                uint32_t se_addr = 0x06000000 + sb * 0x800 + tile_idx * 2;
                uint16_t se = read_vram16(g, se_addr & 0x17FFFF);
                int tno = se & 0x3FF;
                int flip_x = (se >> 10) & 1, flip_y = (se >> 11) & 1;
                int px = flip_x ? (7 - (tx & 7)) : (tx & 7);
                int py = flip_y ? (7 - (ty & 7)) : (ty & 7);
                uint32_t ch_addr = 0x06000000 + cbb * 0x4000 + tno * 0x20 + py * (is8 ? 8 : 4) + (px >> (is8 ? 0 : 1));
                ch_addr &= 0x17FFFF;
                int palidx;
                if (is8) palidx = g->vram[ch_addr];
                else { uint8_t b = g->vram[ch_addr]; palidx = (px & 1) ? (b >> 4) : (b & 0xF); palidx |= palbank << 4; }
                if (palidx == 0) continue;
                uint16_t col = read_pal(g, palidx);
                if (pri < p->bgpri[base + x]) { p->frame[base + x] = col; p->bgpri[base + x] = pri; }
            }
        }
    }

    /* ---- bitmap modes ---- */
    if (bg_mode == 3) {    /* 320x240 bitmap, 32768 */
        if (!(dispcnt & (1 << 8))) {}   /* BG0 must be on */
        for (int x = 0; x < SCREEN_W; x++) {
            uint32_t addr = (y * 240 + x) * 2;
            uint16_t col = (uint16_t)(g->vram[addr] | (g->vram[addr+1] << 8));
            if (col != 0 || y > 0) {
                p->frame[base + x] = col;
                p->bgpri[base + x] = 0;
            }
        }
    } else if (bg_mode == 4) {   /* 256 color bitmap, 2 pages */
        uint32_t page = (dispcnt >> 4) & 1;
        for (int x = 0; x < SCREEN_W; x++) {
            uint32_t addr = page * 0xA000 + y * 240 + x;
            uint8_t idx = g->vram[addr];
            if (idx) {
                p->frame[base + x] = read_pal(g, idx);
                p->bgpri[base + x] = 0;
            }
        }
    } else if (bg_mode == 5) {   /* 160x128 bitmap, 2 pages */
        uint32_t page = (dispcnt >> 4) & 1;
        for (int x = 0; x < 160; x++) {
            if (y >= 128) break;
            uint32_t addr = page * 0xA000 + y * 160 + x;
            uint16_t col = (uint16_t)(g->vram[addr*2] | (g->vram[addr*2+1] << 8));
            p->frame[base + x] = col;
            p->bgpri[base + x] = 0;
        }
    }

    /* ---- OBJ (sprites) ---- */
    if (obj_enabled) {
        int obj_mapping = (dispcnt >> 6) & 1;   /* 0 = 2D, 1 = 1D */
        for (int o = 0; o < 128; o++) {
            uint32_t ea = o * 8;
            uint16_t a0 = (uint16_t)(g->oam[ea] | (g->oam[ea+1] << 8));
            uint16_t a1 = (uint16_t)(g->oam[ea+2] | (g->oam[ea+3] << 8));
            uint16_t a2 = (uint16_t)(g->oam[ea+4] | (g->oam[ea+5] << 8));

            bool disable = (a0 >> 10) & 1;      /* bit10: gfx disable */
            if (disable) continue;
            int mode = (a0 >> 8) & 3;
            if (mode == 3) continue;

            int ypos   = a0 & 0xFF;
            int xpos   = a1 & 0x1FF;
            int shape  = (a0 >> 12) & 3;
            int cmode  = (a0 >> 11) & 1;        /* 1 = 256 colors */
            bool hflip = a1 & 0x1000, vflip = a1 & 0x2000;
            int size   = (a1 >> 14) & 3;
            int pri    = (a2 >> 10) & 3;
            int tno    = a2 & 0x3FF;
            int palbank = (a2 >> 12) & 0xF;

            int w, h;
            static const int dim[3][4][2] = {
                {{8,8},{16,16},{32,32},{64,64}},
                {{16,8},{32,8},{32,16},{64,32}},
                {{8,16},{8,32},{16,32},{32,64}},
            };
            w = dim[shape][size][0];
            h = dim[shape][size][1];

            int rows = h >> 3, cols = w >> 3;
            int tile_stride = obj_mapping ? cols : 32;
            for (int ry = 0; ry < rows; ry++) {
                for (int rx = 0; rx < cols; rx++) {
                    int tile = (tno + ry * tile_stride + rx) & 0x3FF;
                    uint32_t tbase = (cmode ? 0x400u : 0x200u) + (uint32_t)tile * 0x20u;
                    for (int py = 0; py < 8; py++) {
                        for (int px = 0; px < 8; px++) {
                            int sx = rx*8 + px, sy = ry*8 + py;
                            int dx = hflip ? (w-1-sx) : sx;
                            int dy = vflip ? (h-1-sy) : sy;
                            int px2 = xpos + dx, py2 = ypos + dy;
                            if (px2 < 0 || px2 >= SCREEN_W || py2 < 0 || py2 >= SCREEN_H) continue;
                            uint32_t ch = 0x06010000u + tbase + (uint32_t)py * (cmode ? 8u : 4u) + (uint32_t)(px >> (cmode ? 0 : 1));
                            int palidx;
                            if (cmode) {
                                palidx = g->vram[(ch - 0x06000000u) & 0x17FFFu];
                            } else {
                                uint8_t b = g->vram[(ch - 0x06000000u) & 0x17FFFu];
                                palidx = (px & 1) ? (b >> 4) : (b & 0xF);
                                palidx |= palbank << 4;
                            }
                            if (palidx == 0) continue;
                            uint16_t col = read_pal(g, palidx);
                            if (pri <= p->bgpri[base + px2]) {
                                p->frame[base + px2] = col;
                                p->bgpri[base + px2] = pri;
                            }
                        }
                    }
                }
            }
        }
    }
}

void ppu_render_frame(GBA *g) {
    for (int y = 0; y < SCREEN_H; y++) {
        render_line(g, y);
    }
}