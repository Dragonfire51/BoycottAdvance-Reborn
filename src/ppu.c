/*
 * ppu.c - GBA PPU: BG modes 0-5, OBJ (regular + affine), windows, colour
 * special effects (alpha / brighten / darken), BG mosaic.
 * Per-scanline renderer, written from GBATEK ("LCD" chapters).
 *
 * Call order per frame (see gba.c run_frame_core):
 *   ppu_frame_start(g);                 // latch affine reference points
 *   for line 0..159: run CPU for the visible 960 cycles, ppu_render_line(g, line)
 */
#include "gba.h"
#include "mem.h"

#define TRANS 0x8000u            /* marker: transparent pixel in a layer buffer */

enum { L_BG0, L_BG1, L_BG2, L_BG3, L_OBJ, L_BD };   /* matches BLDCNT bit order */

typedef struct {
    uint16_t bg[4][SCREEN_W];    /* RGB555 or TRANS */
    uint16_t obj[SCREEN_W];      /* RGB555 or TRANS */
    uint8_t  obj_pri[SCREEN_W];
    uint8_t  obj_semi[SCREEN_W]; /* OBJ mode 1 (semi-transparent) */
    uint8_t  obj_win[SCREEN_W];  /* OBJ mode 2 (OBJ window mask) */
} LineBuf;

static LineBuf lb;

/* ---- memory helpers ---------------------------------------------------- */
/* VRAM is 96K mirrored in 128K: 0x18000-0x1FFFF mirrors 0x10000-0x17FFF.
 * (The old helpers indexed past the end of g->vram for those addresses.) */
static inline uint32_t vmirror(uint32_t off) {
    off &= 0x1FFFF;
    return off >= VRAM_SIZE ? off - 0x8000 : off;
}
static inline uint16_t read_pal(GBA *g, int idx) {
    idx &= 0x1FF;
    return (uint16_t)((g->pal[idx*2] | (g->pal[idx*2+1] << 8)) & 0x7FFF);
}
static inline uint16_t read_vram16(GBA *g, uint32_t off) {
    off = vmirror(off) & ~1u;
    return (uint16_t)(g->vram[off] | (g->vram[off+1] << 8));
}
static inline uint8_t read_vram8(GBA *g, uint32_t off) {
    return g->vram[vmirror(off)];
}
static inline uint16_t oam16(GBA *g, int off) {
    return (uint16_t)(g->oam[off] | (g->oam[off+1] << 8));
}

/* ---- text backgrounds (GBATEK "LCD I/O BG Control", "BG Screen Data Format") */
static void render_text_bg(GBA *g, int bgn, int y) {
    Ppu *p = &g->ppu;
    uint16_t *out = lb.bg[bgn];
    uint16_t bgcnt = p->bgcnt[bgn];
    uint32_t cbb = ((bgcnt >> 2) & 3) * 0x4000;     /* char base block */
    uint32_t sb  = ((bgcnt >> 8) & 0x1F) * 0x800;   /* screen base block */
    bool is8 = bgcnt & 0x80;
    int size = (bgcnt >> 14) & 3;                   /* 0:256x256 1:512x256 2:256x512 3:512x512 */
    int mw = (size & 1) ? 512 : 256, mh = (size & 2) ? 512 : 256;
    int mos_h = 1, mos_v = 1;
    if (bgcnt & 0x40) {                             /* BG mosaic enable */
        mos_h = (p->mosaic & 0xF) + 1;
        mos_v = ((p->mosaic >> 4) & 0xF) + 1;
    }
    int sy = y - (y % mos_v);
    int hofs = p->bghofs[bgn] & 0x1FF, vofs = p->bgvofs[bgn] & 0x1FF;

    for (int x = 0; x < SCREEN_W; x++) {
        int sx = x - (x % mos_h);
        int tx = (sx + hofs) & (mw - 1);
        int ty = (sy + vofs) & (mh - 1);
        int tile_x = tx >> 3, tile_y = ty >> 3;
        int bx = tile_x >> 5, by = tile_y >> 5;
        int blk = (size == 0) ? 0 : (size == 1) ? bx : (size == 2) ? by : (by * 2 + bx);
        uint32_t map_off = sb + (uint32_t)blk * 0x800
                         + (uint32_t)(tile_y & 31) * 64 + (uint32_t)(tile_x & 31) * 2;
        uint16_t se = read_vram16(g, map_off);
        int tno = se & 0x3FF;
        int px = tx & 7, py = ty & 7;
        if (se & 0x0400) px = 7 - px;
        if (se & 0x0800) py = 7 - py;
        if (is8) {
            int idx = read_vram8(g, cbb + (uint32_t)tno * 64 + (uint32_t)py * 8 + px);
            out[x] = idx ? read_pal(g, idx) : TRANS;
        } else {
            uint8_t b = read_vram8(g, cbb + (uint32_t)tno * 32 + (uint32_t)py * 4 + (px >> 1));
            int c = (px & 1) ? (b >> 4) : (b & 0xF);
            /* palette bank comes from the SCREEN ENTRY (bits 12-15), and
             * transparency is colour 0 of the tile, not of the whole palette */
            out[x] = c ? read_pal(g, (se >> 12) * 16 + c) : TRANS;
        }
    }
}

/* ---- affine backgrounds (BG2 / BG3), GBATEK "BG Rotation/Scaling" -------- */
static void render_affine_bg(GBA *g, int bgn, int y) {
    Ppu *p = &g->ppu;
    uint16_t *out = lb.bg[bgn];
    int a = bgn - 2;
    uint16_t bgcnt = p->bgcnt[bgn];
    uint32_t cbb = ((bgcnt >> 2) & 3) * 0x4000;
    uint32_t sb  = ((bgcnt >> 8) & 0x1F) * 0x800;
    bool wrap = bgcnt & 0x2000;                     /* area overflow: wraparound */
    int dim = 128 << ((bgcnt >> 14) & 3);           /* 128/256/512/1024, square */
    int mos_h = 1, mos_v = 1;
    if (bgcnt & 0x40) {
        mos_h = (p->mosaic & 0xF) + 1;
        mos_v = ((p->mosaic >> 4) & 0xF) + 1;
    }
    int32_t pa = p->pa[a], pc = p->pc[a];
    int32_t rx = p->refx[a], ry = p->refy[a];
    if (mos_v > 1) ry -= p->pd[a] * (y % mos_v);    /* first line of the mosaic row */
    for (int x = 0; x < SCREEN_W; x++) {
        int xs = x - (x % mos_h);
        int32_t sx = (rx + pa * xs) >> 8;
        int32_t sy = (ry + pc * xs) >> 8;
        if (sx < 0 || sx >= dim || sy < 0 || sy >= dim) {
            if (!wrap) { out[x] = TRANS; continue; }
            sx &= dim - 1; sy &= dim - 1;
        }
        int tno = read_vram8(g, sb + (uint32_t)(sy >> 3) * (uint32_t)(dim >> 3) + (uint32_t)(sx >> 3));
        int idx = read_vram8(g, cbb + (uint32_t)tno * 64 + (uint32_t)(sy & 7) * 8 + (uint32_t)(sx & 7));
        out[x] = idx ? read_pal(g, idx) : TRANS;
    }
}

/* ---- bitmap modes 3/4/5 (BG2) ------------------------------------------- */
static void render_bitmap(GBA *g, int mode, int y) {
    uint16_t *out = lb.bg[2];
    uint32_t page = ((g->ppu.dispcnt >> 4) & 1) * 0xA000u;
    for (int x = 0; x < SCREEN_W; x++) {
        if (mode == 3) {
            out[x] = read_vram16(g, ((uint32_t)y * 240 + x) * 2) & 0x7FFF;
        } else if (mode == 4) {
            int idx = read_vram8(g, page + (uint32_t)y * 240 + x);
            out[x] = idx ? read_pal(g, idx) : TRANS;
        } else {
            out[x] = (x < 160 && y < 128)
                   ? (read_vram16(g, page + ((uint32_t)y * 160 + x) * 2) & 0x7FFF) : TRANS;
        }
    }
}

/* ---- OBJ (GBATEK "LCD OBJ - OBJ Attributes") ------------------------------
 * attr0: 0-7 Y, 8 rot/scale, 9 double-size (rot) / disable (no rot),
 *        10-11 mode (0 normal, 1 semi-transparent, 2 OBJ window), 12 mosaic,
 *        13 256 colours, 14-15 shape
 * attr1: 0-8 X, rot: 9-13 matrix index / no rot: 12 hflip 13 vflip, 14-15 size
 * attr2: 0-9 tile, 10-11 priority, 12-15 palette bank                      */
static void render_objs(GBA *g, int y) {
    static const uint8_t dim[3][4][2] = {
        {{8,8},{16,16},{32,32},{64,64}},
        {{16,8},{32,8},{32,16},{64,32}},
        {{8,16},{8,32},{16,32},{32,64}},
    };
    Ppu *p = &g->ppu;
    for (int x = 0; x < SCREEN_W; x++) {
        lb.obj[x] = TRANS; lb.obj_pri[x] = 7; lb.obj_semi[x] = 0; lb.obj_win[x] = 0;
    }
    if (!(p->dispcnt & 0x1000)) return;

    int mode = p->dispcnt & 7;
    bool one_d = p->dispcnt & 0x40;
    uint32_t obj_base = (mode >= 3) ? 0x14000 : 0x10000;  /* OBJ char base in bitmap modes (GBATEK) */

    for (int o = 0; o < 128; o++) {
        uint16_t a0 = oam16(g, o * 8), a1 = oam16(g, o * 8 + 2), a2 = oam16(g, o * 8 + 4);
        bool aff = a0 & 0x0100;
        if (!aff && (a0 & 0x0200)) continue;               /* OBJ disabled */
        int omode = (a0 >> 10) & 3;
        if (omode == 3) continue;
        int shape = (a0 >> 14) & 3;
        if (shape == 3) continue;
        int w = dim[shape][(a1 >> 14) & 3][0], h = dim[shape][(a1 >> 14) & 3][1];
        bool dbl = aff && (a0 & 0x0200);
        int bw = dbl ? w * 2 : w, bh = dbl ? h * 2 : h;

        int ypos = a0 & 0xFF;
        int ry = (y - ypos) & 0xFF;                        /* row inside bounding box, wraps at 256 */
        if (ry >= bh) continue;
        int xpos = a1 & 0x1FF; if (xpos >= 256) xpos -= 512;

        bool pal256 = a0 & 0x2000;                         /* colour mode lives in attr0 */
        int tno = a2 & 0x3FF, pri = (a2 >> 10) & 3, palbank = (a2 >> 12) & 0xF;
        if (mode >= 3 && tno < 512) continue;              /* bitmap modes: tiles 0-511 unusable */
        int tstep = pal256 ? 2 : 1;                        /* 8x8 tile = 1 or 2 32-byte slots */
        int row_stride = one_d ? (w >> 3) * tstep : 32;

        /* OBJ mosaic (GBATEK "Mosaic Function"): per-OBJ enable in attr0 bit 12,
         * sizes from the MOSAIC register bits 8-15 (minus 1). The mosaic grid is
         * aligned to the screen (as mGBA): a pixel is colorized by the upper-left
         * covered pixel of its block, and the vertical block is latched per
         * scanline and clamped to the sprite's on-screen rows. */
        bool obj_mos = a0 & 0x1000;
        int mh = obj_mos ? ((p->mosaic >> 8) & 0xF) + 1 : 1;
        int mv = obj_mos ? ((p->mosaic >> 12) & 0xF) + 1 : 1;
        int ry_eff = ry;
        if (obj_mos && mv > 1) {
            int mos_y = y - (y % mv);
            if (mos_y < ypos && ypos < SCREEN_H) mos_y = ypos;
            if (mos_y >= ypos + bh) mos_y = ypos + bh - 1;
            ry_eff = (mos_y - ypos) & 0xFF;
            if (ry_eff >= bh) ry_eff = bh - 1;
        }

        int pa = 0, pb = 0, pc = 0, pd = 0;
        if (aff) {
            int grp = ((a1 >> 9) & 0x1F) * 32;
            pa = (int16_t)oam16(g, grp + 6);  pb = (int16_t)oam16(g, grp + 14);
            pc = (int16_t)oam16(g, grp + 22); pd = (int16_t)oam16(g, grp + 30);
        }

        for (int bx = 0; bx < bw; bx++) {
            int scr_x = xpos + bx;
            if (scr_x < 0 || scr_x >= SCREEN_W) continue;
            int tx, ty;
            if (aff) {
                /* screen-aligned: sample the transform at the block origin */
                int obx = (mh > 1) ? bx - (scr_x % mh) : bx;
                int cx = obx - bw / 2, cy = ry_eff - bh / 2;
                tx = ((pa * cx + pb * cy) >> 8) + w / 2;
                ty = ((pc * cx + pd * cy) >> 8) + h / 2;
                if (tx < 0 || tx >= w || ty < 0 || ty >= h) continue;
            } else {
                if (mh > 1) {
                    int off = scr_x % mh;               /* block offset, scr_x >= 0 */
                    tx = (a1 & 0x1000) ? (w - 1 - bx) + off : bx - off;
                    if (tx < 0) tx = 0; else if (tx > w - 1) tx = w - 1;
                } else {
                    tx = (a1 & 0x1000) ? w - 1 - bx : bx;
                }
                ty = (a1 & 0x2000) ? h - 1 - ry_eff : ry_eff;
            }
            /* Tile index within the OBJ character area. BG modes 0-2 have a
             * 32K area at 0x06010000 (1024 tiles, 10-bit index); bitmap modes
             * 3-5 only 16K at 0x06014000, and tile numbers 512-1023 map to
             * slots 0-511 there (GBATEK "VRAM - Character Data"). */
            uint32_t slot = (uint32_t)(tno + (ty >> 3) * row_stride + (tx >> 3) * tstep);
            slot = (mode >= 3) ? ((slot - 512) & 0x1FF) : (slot & 0x3FF);
            uint32_t tbase = obj_base + slot * 32;
            int px = tx & 7, py = ty & 7, c, palidx;
            if (pal256) {
                c = read_vram8(g, tbase + (uint32_t)py * 8 + px);
                palidx = 0x100 + c;
            } else {
                uint8_t b = read_vram8(g, tbase + (uint32_t)py * 4 + (px >> 1));
                c = (px & 1) ? (b >> 4) : (b & 0xF);
                palidx = 0x100 + palbank * 16 + c;
            }
            if (!c) continue;                              /* transparent: colour 0 of the bank */
            if (omode == 2) { lb.obj_win[scr_x] = 1; continue; }
            /* lower priority number wins; ties go to the lower OAM index
             * (we iterate o ascending, so only replace on strictly lower) */
            if (lb.obj[scr_x] == TRANS || pri < lb.obj_pri[scr_x]) {
                lb.obj[scr_x] = read_pal(g, palidx);
                lb.obj_pri[scr_x] = (uint8_t)pri;
                lb.obj_semi[scr_x] = (omode == 1);
            }
        }
    }
}

/* ---- colour effects ------------------------------------------------------ */
static uint16_t blend_alpha(uint16_t a, uint16_t b, int eva, int evb) {
    uint16_t out = 0;
    for (int s = 0; s < 15; s += 5) {
        int v = (((a >> s) & 31) * eva + ((b >> s) & 31) * evb) >> 4;
        if (v > 31) v = 31;
        out |= (uint16_t)(v << s);
    }
    return out;
}
static uint16_t fade(uint16_t c, int evy, bool up) {
    uint16_t out = 0;
    for (int s = 0; s < 15; s += 5) {
        int v = (c >> s) & 31;
        v = up ? v + (((31 - v) * evy) >> 4) : v - ((v * evy) >> 4);
        out |= (uint16_t)(v << s);
    }
    return out;
}

/* ---- windows + priority resolution + blending ----------------------------- */
static void composite_line(GBA *g, int y) {
    Ppu *p = &g->ppu;
    uint16_t dc = p->dispcnt;
    uint16_t backdrop = read_pal(g, 0);
    bool anywin = dc & 0xE000;                       /* WIN0 / WIN1 / OBJ window */

    int wx1[2], wx2[2]; bool wy[2];
    for (int w = 0; w < 2; w++) {
        int x1 = p->winh[w] >> 8, x2 = p->winh[w] & 0xFF;
        int y1 = p->winv[w] >> 8, y2 = p->winv[w] & 0xFF;
        if (x2 > 240 || x1 > x2) x2 = 240;           /* GBATEK: garbage -> 240 / 160 */
        if (y2 > 160 || y1 > y2) y2 = 160;
        wx1[w] = x1; wx2[w] = x2;
        wy[w] = (dc & (0x2000 << w)) && y >= y1 && y < y2;
    }
    uint8_t win_ctl[2] = { (uint8_t)(p->winin & 0x3F), (uint8_t)((p->winin >> 8) & 0x3F) };
    uint8_t out_ctl = (uint8_t)(p->winout & 0x3F);
    uint8_t obj_ctl = (uint8_t)((p->winout >> 8) & 0x3F);

    int eva = p->bldalpha & 0x1F;        if (eva > 16) eva = 16;
    int evb = (p->bldalpha >> 8) & 0x1F; if (evb > 16) evb = 16;
    int evy = p->bldy & 0x1F;            if (evy > 16) evy = 16;
    uint16_t bld = p->bldcnt;
    int emode = (bld >> 6) & 3;
    uint16_t *dst = &p->frame[(uint32_t)y * SCREEN_W];

    for (int x = 0; x < SCREEN_W; x++) {
        uint8_t m = 0x3F;                            /* bits 0-3 BG, 4 OBJ, 5 effects */
        if (anywin) {
            if      (wy[0] && x >= wx1[0] && x < wx2[0])        m = win_ctl[0];
            else if (wy[1] && x >= wx1[1] && x < wx2[1])        m = win_ctl[1];
            else if ((dc & 0x8000) && lb.obj_win[x])            m = obj_ctl;
            else                                                m = out_ctl;
        }
        /* find the two topmost opaque layers: per priority, OBJ above BG0..3 */
        uint16_t col[2] = { backdrop, backdrop };
        int lay[2] = { L_BD, L_BD };
        int n = 0;
        for (int pri = 0; pri < 4 && n < 2; pri++) {
            if (n < 2 && (m & 0x10) && lb.obj[x] != TRANS && lb.obj_pri[x] == pri) {
                col[n] = lb.obj[x]; lay[n] = L_OBJ; n++;
            }
            for (int bg = 0; bg < 4 && n < 2; bg++) {
                if ((m & (1 << bg)) && lb.bg[bg][x] != TRANS && (p->bgcnt[bg] & 3) == pri) {
                    col[n] = lb.bg[bg][x]; lay[n] = bg; n++;
                }
            }
        }
        uint16_t out = col[0];
        if (m & 0x20) {
            int t1 = (bld >> lay[0]) & 1;
            int t2 = (bld >> (8 + lay[1])) & 1;
            if (lay[0] == L_OBJ && lb.obj_semi[x] && t2) out = blend_alpha(col[0], col[1], eva, evb);
            else if (emode == 1 && t1 && t2)             out = blend_alpha(col[0], col[1], eva, evb);
            else if (emode == 2 && t1)                   out = fade(col[0], evy, true);
            else if (emode == 3 && t1)                   out = fade(col[0], evy, false);
        }
        dst[x] = out;
    }
}

/* ---- public API ------------------------------------------------------------ */
/* Internal affine reference points are reloaded from BGxX/BGxY at the start of
 * every frame and advanced by PB/PD after each scanline (GBATEK). */
void ppu_frame_start(GBA *g) {
    Ppu *p = &g->ppu;
    for (int i = 0; i < 2; i++) { p->refx[i] = p->bgx[i]; p->refy[i] = p->bgy[i]; }
}

void ppu_render_line(GBA *g, int y) {
    Ppu *p = &g->ppu;
    uint16_t dc = p->dispcnt;
    uint16_t *dst = &p->frame[(uint32_t)y * SCREEN_W];

    if (dc & 0x0080) {                               /* forced blank: white */
        for (int x = 0; x < SCREEN_W; x++) dst[x] = 0x7FFF;
    } else {
        for (int b = 0; b < 4; b++)
            for (int x = 0; x < SCREEN_W; x++) lb.bg[b][x] = TRANS;
        int mode = dc & 7;
        switch (mode) {
        case 0:
            for (int b = 0; b < 4; b++) if (dc & (0x100 << b)) render_text_bg(g, b, y);
            break;
        case 1:
            for (int b = 0; b < 2; b++) if (dc & (0x100 << b)) render_text_bg(g, b, y);
            if (dc & 0x400) render_affine_bg(g, 2, y);
            break;
        case 2:                                      /* BG2/BG3 affine only; no text BGs */
            if (dc & 0x400) render_affine_bg(g, 2, y);
            if (dc & 0x800) render_affine_bg(g, 3, y);
            break;
        case 3: case 4: case 5:
            if (dc & 0x400) render_bitmap(g, mode, y);
            break;
        default: break;
        }
        render_objs(g, y);
        composite_line(g, y);
    }
    for (int i = 0; i < 2; i++) {
        p->refx[i] += p->pb[i];
        p->refy[i] += p->pd[i];
    }
}

void ppu_render_frame(GBA *g) {                      /* kept for frontends / tools */
    ppu_frame_start(g);
    for (int y = 0; y < SCREEN_H; y++) ppu_render_line(g, y);
}
