/*
 * ppu.c - GBA background/sprite rendering (modes 0-5, OBJ).
 * Clean-room implementation of the GBA PPU, following the GBATEK
 * description of the display controller.
 */
#include "gba.h"
#include "mem.h"

static inline uint16_t read_pal(GBA *g, int idx) {
    idx &= 0x1FF;
    return (uint16_t)(g->pal[idx*2] | (g->pal[idx*2+1] << 8));
}
static inline uint16_t read_vram16(GBA *g, uint32_t off) {
    off &= 0x1FFFF;
    return (uint16_t)(g->vram[off] | (g->vram[off+1] << 8));
}
static inline uint8_t read_vram8(GBA *g, uint32_t off) {
    return g->vram[off & 0x1FFFF];
}

/* Render a full scanline y (0..159) into ppu.frame */
static void render_line(GBA *g, int y) {
    Ppu *p = &g->ppu;
    uint16_t dispcnt = p->dispcnt;
    int bg_mode = dispcnt & 0x7;
    bool obj_enabled = dispcnt & 0x1000;
    uint32_t base = (uint32_t)y * SCREEN_W;

    /* backdrop */
    uint16_t backdrop = read_pal(g, 0);
    for (int x = 0; x < SCREEN_W; x++) {
        p->frame[base + x] = backdrop;
        p->bgpri[base + x] = 4;         /* 4 = lowest priority */
    }

    /* ---- text modes 0-2: BG0-3 ---- */
    if (bg_mode <= 2) {
        int nbg = (bg_mode == 0) ? 4 : 2;   /* mode 1/2: BG0-1 are text */
        for (int bgn = nbg - 1; bgn >= 0; bgn--) {   /* draw low priority first */
            if (!(dispcnt & (1 << (8 + bgn)))) continue;
            uint16_t bgcnt = p->bgcnt[bgn];
            int pri = bgcnt & 3;
            uint32_t cbb = ((bgcnt >> 2) & 3) * 0x4000;   /* char base block */
            uint32_t sb  = ((bgcnt >> 8) & 0x1F) * 0x800;   /* screen base block */
            bool is8 = bgcnt & 0x80;
            int size = (bgcnt >> 14) & 3;
            static const int dimt[4][2] = {{32,32},{64,32},{32,64},{64,64}};
            int mw = dimt[size][0], mh = dimt[size][1];
            int palbank = (bgn < 2) ? ((bgcnt >> 4) & 0xF) : 0;
            uint32_t hofs = p->bghofs[bgn] & 0x1FF, vofs = p->bgvofs[bgn] & 0x1FF;

            for (int x = 0; x < SCREEN_W; x++) {
                int tx = (x + hofs) & (mw*8 - 1);
                int ty = (y + vofs) & (mh*8 - 1);
                int tile_x = tx >> 3, tile_y = ty >> 3;
                /* screen block position (256px = 32 tiles per block, blocks laid out) */
                int block_x = (tile_x >> 5) & (mw / 32 - 1 ? mw/32 - 1 : 0);
                int block_y = tile_y >> 5;
                int within_x = tile_x & 31, within_y = tile_y & 31;
                uint32_t map_off = sb + (uint32_t)(block_y * (mw/32) + block_x) * 0x800
                                 + (uint32_t)within_y * 64 + (uint32_t)within_x * 2;
                uint16_t se = read_vram16(g, map_off);
                int tno = se & 0x3FF;
                int flip_x = (se >> 10) & 1, flip_y = (se >> 11) & 1;
                int px = tx & 7, py = ty & 7;
                if (flip_x) px = 7 - px;
                if (flip_y) py = 7 - py;
                uint32_t ch = cbb + (uint32_t)tno * (is8 ? 64 : 32) + (uint32_t)py * (is8 ? 8 : 4);
                int palidx;
                if (is8) palidx = read_vram8(g, ch + px);
                else {
                    uint8_t b = read_vram8(g, ch + (px >> 1));
                    palidx = (px & 1) ? (b >> 4) : (b & 0xF);
                    palidx |= palbank << 4;
                }
                if (palidx == 0) continue;
                if (pri < p->bgpri[base + x]) {
                    p->frame[base + x] = read_pal(g, palidx);
                    p->bgpri[base + x] = pri;
                }
            }
        }
    }

    /* ---- mode 1/2 affine background BG2 ---- */
    if (bg_mode == 1 || bg_mode == 2) {
        int bgn = 2;
        if (dispcnt & (1 << (8 + bgn))) {
            uint16_t bgcnt = p->bgcnt[bgn];
            int pri = bgcnt & 3;
            uint32_t cbb = ((bgcnt >> 2) & 3) * 0x4000;
            uint32_t sb  = ((bgcnt >> 8) & 0x1F) * 0x800;
            int size = (bgcnt >> 14) & 3;
            int mw = 128 << (size & 1), mh = 128 << ((size >> 1) & 1);
            int32_t refx = p->bgaffx[0], refy = p->bgaffy[0];
            int32_t pA = p->bgaffpa[bgn-2], pB = p->bgaffpb[bgn-2];
            int32_t pC = p->bgaffpc[bgn-2], pD = p->bgaffpd[bgn-2];
            for (int x = 0; x < SCREEN_W; x++) {
                int32_t sx = (refx + pA * x + pB * y) >> 8;
                int32_t sy = (refy + pC * x + pD * y) >> 8;
                sx &= (mw - 1); sy &= (mh - 1);
                int tile_x = sx >> 3, tile_y = sy >> 3;
                uint32_t map_off = sb + (uint32_t)tile_y * (mw/8) + (uint32_t)tile_x;
                int tno = read_vram8(g, map_off);
                uint32_t ch = cbb + (uint32_t)tno * 64 + (uint32_t)(sy & 7) * 8 + (sx & 7);
                int palidx = read_vram8(g, ch);
                if (palidx == 0) continue;
                if (pri < p->bgpri[base + x]) {
                    p->frame[base + x] = read_pal(g, palidx);
                    p->bgpri[base + x] = pri;
                }
            }
        }
    }

    /* ---- bitmap modes ---- */
    if (bg_mode == 3) {   /* 240x160, 16bpp */
        if (dispcnt & (1 << 10)) {
            for (int x = 0; x < SCREEN_W; x++) {
                uint32_t off = ((uint32_t)y * 240 + x) * 2;
                p->frame[base + x] = read_vram16(g, off);
                p->bgpri[base + x] = 0;
            }
        }
    } else if (bg_mode == 4) {   /* 240x160, 8bpp, 2 pages */
        if (dispcnt & (1 << 10)) {
            uint32_t page = (dispcnt >> 4) & 1;
            for (int x = 0; x < SCREEN_W; x++) {
                uint32_t off = page * 0xA000 + (uint32_t)y * 240 + x;
                int idx = read_vram8(g, off);
                if (idx) { p->frame[base + x] = read_pal(g, idx); p->bgpri[base + x] = 0; }
            }
        }
    } else if (bg_mode == 5) {   /* 160x128, 16bpp, 2 pages */
        if (dispcnt & (1 << 10)) {
            uint32_t page = (dispcnt >> 4) & 1;
            if (y < 128) {
                for (int x = 0; x < 160; x++) {
                    uint32_t off = page * 0xA000 + ((uint32_t)y * 160 + x) * 2;
                    p->frame[base + x] = read_vram16(g, off);
                    p->bgpri[base + x] = 0;
                }
            }
        }
    }

    /* ---- OBJ (sprites) ---- */
    if (obj_enabled) {
        int obj_1d = (dispcnt >> 6) & 1;
        static const int dim[3][4][2] = {
            {{8,8},{16,16},{32,32},{64,64}},
            {{16,8},{32,8},{32,16},{64,32}},
            {{8,16},{8,32},{16,32},{32,64}},
        };
        uint32_t obj_vram = ((dispcnt >> 5) & 1) ? 0x14000 : 0x10000;  /* OBJ char base */
        /* draw from lowest priority (3) to highest (0): iterate 3..0 */
        for (int want = 3; want >= 0; want--) {
            for (int o = 0; o < 128; o++) {
                uint32_t ea = (uint32_t)o * 8;
                uint16_t a0 = (uint16_t)(g->oam[ea]   | (g->oam[ea+1] << 8));
                uint16_t a1 = (uint16_t)(g->oam[ea+2] | (g->oam[ea+3] << 8));
                uint16_t a2 = (uint16_t)(g->oam[ea+4] | (g->oam[ea+5] << 8));

                if (a0 & 0x0200) continue;             /* rotation/affine: skip */
                if (a0 & 0x0400) continue;             /* disabled */
                int shape = (a0 >> 14) & 3;
                if (shape == 3) continue;
                int size  = (a1 >> 14) & 3;
                int pri   = (a2 >> 10) & 3;
                if (pri != want) continue;

                int cmode = (a1 >> 13) & 1;            /* 0=16 colors, 1=256 */
                int w = dim[shape][size][0], h = dim[shape][size][1];
                int xpos = a0 & 0x1FF; if (xpos >= 240) xpos -= 512;
                int ypos = a0 & 0x0FF; if (ypos >= 160) ypos -= 256;
                bool hflip = a1 & 0x1000, vflip = a1 & 0x2000;
                int tno = a2 & 0x3FF;
                int palbank = (a2 >> 12) & 0xF;

                int cols = w >> 3, rows = h >> 3;
                int stride = obj_1d ? cols : 32;

                for (int ry = 0; ry < rows; ry++) {
                    for (int rx = 0; rx < cols; rx++) {
                        int tile = (tno + ry * stride + rx) & 0x3FF;
                        uint32_t tbase = obj_vram + (uint32_t)tile * 32;
                        for (int py = 0; py < 8; py++) {
                            int sy = ry*8 + py;
                            for (int px = 0; px < 8; px++) {
                                int sx = rx*8 + px;
                                int dxs = hflip ? (w-1-sx) : sx;
                                int dys = vflip ? (h-1-sy) : sy;
                                int scr_x = xpos + dxs, scr_y = ypos + dys;
                                if (scr_x < 0 || scr_x >= SCREEN_W || scr_y < 0 || scr_y >= SCREEN_H) continue;
                                uint32_t ch = tbase + (uint32_t)py * (cmode ? 8 : 4) + (uint32_t)(cmode ? px : (px >> 1));
                                int palidx;
                                if (cmode) palidx = read_vram8(g, ch);
                                else {
                                    uint8_t bb = read_vram8(g, ch);
                                    palidx = (px & 1) ? (bb >> 4) : (bb & 0xF);
                                    palidx |= palbank << 4;
                                }
                                if (palidx == 0) continue;
                                uint32_t fb = (uint32_t)scr_y * SCREEN_W + scr_x;
                                /* sprite pixel beats BG if its priority <= current */
                                if (pri <= p->bgpri[fb]) {
                                    p->frame[fb] = read_pal(g, 0x100 + palidx);
                                    p->bgpri[fb] = pri;
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

void ppu_render_frame(GBA *g) {
    for (int y = 0; y < SCREEN_H; y++)
        render_line(g, y);
}
