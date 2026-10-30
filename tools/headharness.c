/* Headless harness: load a ROM, run N frames, report CPU/ppu/apu state and
 * optionally dump VRAM/palette/OAM/screen to a file for pixel comparison. */
#include "gba.h"
#include "arm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef APUDBG
extern unsigned apu_dbg_pops[2], apu_dbg_underrun[2], apu_dbg_maxlen[2], apu_dbg_ringdrop;
#endif

int main(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "usage: %s <rom> <frames> <cmd> [arg]\n", argv[0]);
        return 2;
    }
    const char *rom = argv[1];
    int nframes = atoi(argv[2]);
    const char *cmd = argv[3];

    GBA *g = calloc(1, sizeof(GBA));
    if (!gba_load_rom(g, rom)) { fprintf(stderr, "load fail\n"); return 1; }
    const char *bios = getenv("BIOS");
    if (bios) gba_load_bios(g, bios);
    g->bios_fastboot = getenv("NOFASTBOOT") ? false : true;   /* same HLE boot as the app default */
    gba_reset(g);

    /* PCMDUMP=<path>: write the APU stream as raw s16le stereo, so it can be
       compared sample-by-sample against mGBA's audio output. */
    FILE *pcm = NULL;
    if (getenv("PCMDUMP")) pcm = fopen(getenv("PCMDUMP"), "wb");

    const char* pcmmon = getenv("PCMON");
    FILE* pmf = pcmmon ? fopen(pcmmon, "w") : NULL;
    for (int f = 0; f < nframes; f++) {
        gba_run_frame(g);
        if (pmf) fprintf(pmf, "f%d pc=%08X\n", f, g->cpu.r[15]);
        if (pcm) {
            int16_t buf[4096];
            int n;
            while ((n = apu_drain(buf, 2048)) > 0)
                fwrite(buf, sizeof(int16_t) * 2, (size_t)n, pcm);
        }
        if (getenv("REGLOG") && f < 4000) {
            printf("f%d dispcnt=%04X dispstat=%04X if=%04X ie=%04X ime=%d",
                   f, g->ppu.dispcnt, g->ppu.dispstat, g->if_reg, g->ie, g->ime ? 1 : 0);
            printf(" bgcnt=");
            for (int i = 0; i < 4; i++) printf("%04X,", g->ppu.bgcnt[i]);
            printf(" scroll=");
            for (int i = 0; i < 4; i++)
                printf("%04X,%04X,", g->ppu.bghofs[i], g->ppu.bgvofs[i]);
            printf(" rot=");
            for (int i = 0; i < 2; i++)
                printf("%04X,%04X,%04X,%04X,%04X,%04X,", (uint16_t)g->ppu.pa[i], (uint16_t)g->ppu.pb[i],
                       (uint16_t)g->ppu.pc[i], (uint16_t)g->ppu.pd[i], g->ppu.bgx[i], g->ppu.bgy[i]);
            printf(" win=%04X,%04X,%04X,%04X,%04X,%04X,%04X,%04X,%04X",
                   g->ppu.winh[0], g->ppu.winh[1], g->ppu.winv[0], g->ppu.winv[1],
                   g->ppu.winin, g->ppu.winout, g->ppu.mosaic, g->ppu.bldcnt, g->ppu.bldalpha);
            printf(" oam0=%04X%04X%04X\n",
                   g->oam[0] | (g->oam[1] << 8), g->oam[2] | (g->oam[3] << 8),
                   g->oam[4] | (g->oam[5] << 8));
        }
    }

    if (pcm) fclose(pcm);
    printf("frame_count=%d cycle=%u cpu_pc=%08X cpsr=%08X mode=%d thumb=%d\n",
           (int)g->frame_count, (unsigned)g->cycle, g->cpu.r[15], g->cpu.cpsr,
           g->cpu.cpsr & 0x1F, (g->cpu.cpsr >> 5) & 1);
    printf("dispcnt=%04X dispstat=%04X vcount=%u if=%04X ie=%04X ime=%d key=%04X\n",
           g->ppu.dispcnt, g->ppu.dispstat, (unsigned)g->ppu.vcount,
           g->if_reg, g->ie, g->ime, g->key_input);
    {
        printf("bgcnt:");
        for (int i = 0; i < 4; i++) printf(" bg%d=%04X", i, g->ppu.bgcnt[i]);
        printf("\n");
        printf("scroll:");
        for (int i = 0; i < 4; i++) printf(" bg%d=%04X/%04X", i, g->ppu.bghofs[i], g->ppu.bgvofs[i]);
        printf("\n");
        printf("rot:");
        for (int i = 0; i < 2; i++) printf(" bg%d: pa=%04X pb=%04X pc=%04X pd=%04X xy=%08X/%08X",
            2+i, (uint16_t)g->ppu.pa[i], (uint16_t)g->ppu.pb[i],
            (uint16_t)g->ppu.pc[i], (uint16_t)g->ppu.pd[i],
            (unsigned)g->ppu.refx[i], (unsigned)g->ppu.refy[i]);
        printf("\n");
        printf("win/bld: winh=%04X/%04X winv=%04X/%04X winin=%04X winout=%04X bldcnt=%04X blda=%04X bldy=%04X\n",
            g->ppu.winh[0], g->ppu.winh[1], g->ppu.winv[0], g->ppu.winv[1],
            g->ppu.winin, g->ppu.winout, g->ppu.bldcnt, g->ppu.bldalpha, g->ppu.bldy);
    }
    printf("apu: master=%d sample_acc=%llu ring_avail=%d lfsr=%04X\n",
           g->apu.master, (unsigned long long)g->apu.sample_acc,
           apu_available(), g->apu.lfsr);
#ifdef APUDBG
    printf("apudbg: popsA=%u underA=%u maxA=%u popsB=%u underB=%u maxB=%u ringdrop=%u\n",
           apu_dbg_pops[0], apu_dbg_underrun[0], apu_dbg_maxlen[0],
           apu_dbg_pops[1], apu_dbg_underrun[1], apu_dbg_maxlen[1], apu_dbg_ringdrop);
    printf("apudbg2: cnt_h=%04X cnt_l=%04X master=%d fifo_held=%d,%d",
           g->apu.cnt_h, g->apu.cnt_l, g->apu.master, g->apu.fifo_held[0], g->apu.fifo_held[1]);
    for (int t = 0; t < 2; t++) {
        int ps = g->timer[t].prescaler ? g->timer[t].prescaler : 1;
        uint32_t per = (uint32_t)(0x10000 - g->timer[t].reload) * (uint32_t)ps;
        printf(" tm%d(reload=%04X ps=%d en=%d per=%u hz=%.0f)",
               t, g->timer[t].reload, ps, g->timer[t].enabled, per, 16777216.0 / per);
    }
    printf("\n");
#endif
    printf("timers:");
    for (int i = 0; i < 4; i++) printf(" t%d=%04X", i, g->io[0x100 + i * 2] | (g->io[0x101 + i * 2] << 8));
    printf(" dma:");
    for (int i = 0; i < 4; i++) printf(" d%d=src%08X/dst%08X/cnt%04X/on%d", i,
           (unsigned)g->dma[i].src, (unsigned)g->dma[i].dst,
           (unsigned)g->dma[i].count, g->dma[i].enabled);
    printf("\n");

    if (!strcmp(cmd, "dump")) {
        /* arg = prefix; write vram.bin, pal.bin, oam.bin, screen.bin */
        char p[512];
        snprintf(p, sizeof(p), "%s.vram.bin", argv[4]);
        FILE *f = fopen(p, "wb"); fwrite(g->vram, 1, sizeof(g->vram), f); fclose(f);
        snprintf(p, sizeof(p), "%s.pal.bin", argv[4]);
        f = fopen(p, "wb"); fwrite(g->pal, 1, sizeof(g->pal), f); fclose(f);
        snprintf(p, sizeof(p), "%s.oam.bin", argv[4]);
        f = fopen(p, "wb"); fwrite(g->oam, 1, sizeof(g->oam), f); fclose(f);
        snprintf(p, sizeof(p), "%s.screen.bin", argv[4]);
        f = fopen(p, "wb"); fwrite(g->ppu.frame, 1, sizeof(g->ppu.frame), f); fclose(f);
        snprintf(p, sizeof(p), "%s.ewram.bin", argv[4]);
        f = fopen(p, "wb"); fwrite(g->ewram, 1, sizeof(g->ewram), f); fclose(f);
        snprintf(p, sizeof(p), "%s.iwram.bin", argv[4]);
        f = fopen(p, "wb"); fwrite(g->iwram, 1, sizeof(g->iwram), f); fclose(f);
        snprintf(p, sizeof(p), "%s.io.bin", argv[4]);
        f = fopen(p, "wb"); fwrite(g->io, 1, sizeof(g->io), f); fclose(f);
        printf("dumped %s.{vram,pal,oam,screen,ewram,iwram,io}.bin\n", argv[4]);
    } else if (!strcmp(cmd, "renderstate")) {
        /* arg = prefix: load vram/pal/oam + regs, render one frame, save screen.
         * regs file format (text, one per line): name=value in hex, names as in
         * the ppu struct: dispcnt dispstat bgcnt0..3 bghofs0..3 bgvofs0..3
         * winh0 winh1 winv0 winv1 winin winout bldcnt bldalpha bldy */
        char p[512];
        snprintf(p, sizeof(p), "%s.vram.bin", argv[4]);
        FILE *f = fopen(p, "rb"); if (f) { fread(g->vram, 1, sizeof(g->vram), f); fclose(f); }
        snprintf(p, sizeof(p), "%s.pal.bin", argv[4]);
        f = fopen(p, "rb"); if (f) { fread(g->pal, 1, sizeof(g->pal), f); fclose(f); }
        snprintf(p, sizeof(p), "%s.oam.bin", argv[4]);
        f = fopen(p, "rb"); if (f) { fread(g->oam, 1, sizeof(g->oam), f); fclose(f); }
        snprintf(p, sizeof(p), "%s.regs", argv[4]);
        f = fopen(p, "r");
        if (f) {
            char k[32]; unsigned v;
            while (fscanf(f, " %31[a-zA-Z0-9_]=%x", k, &v) == 2) {
                if (!strcmp(k, "dispcnt")) g->ppu.dispcnt = v;
                else if (!strcmp(k, "dispstat")) g->ppu.dispstat = v;
                else if (!strcmp(k, "bgcnt0")) g->ppu.bgcnt[0] = v;
                else if (!strcmp(k, "bgcnt1")) g->ppu.bgcnt[1] = v;
                else if (!strcmp(k, "bgcnt2")) g->ppu.bgcnt[2] = v;
                else if (!strcmp(k, "bgcnt3")) g->ppu.bgcnt[3] = v;
                else if (!strcmp(k, "bghofs0")) g->ppu.bghofs[0] = v;
                else if (!strcmp(k, "bghofs1")) g->ppu.bghofs[1] = v;
                else if (!strcmp(k, "bghofs2")) g->ppu.bghofs[2] = v;
                else if (!strcmp(k, "bghofs3")) g->ppu.bghofs[3] = v;
                else if (!strcmp(k, "bgvofs0")) g->ppu.bgvofs[0] = v;
                else if (!strcmp(k, "bgvofs1")) g->ppu.bgvofs[1] = v;
                else if (!strcmp(k, "bgvofs2")) g->ppu.bgvofs[2] = v;
                else if (!strcmp(k, "bgvofs3")) g->ppu.bgvofs[3] = v;
                else if (!strcmp(k, "winh0")) g->ppu.winh[0] = v;
                else if (!strcmp(k, "winh1")) g->ppu.winh[1] = v;
                else if (!strcmp(k, "winv0")) g->ppu.winv[0] = v;
                else if (!strcmp(k, "winv1")) g->ppu.winv[1] = v;
                else if (!strcmp(k, "winin")) g->ppu.winin = v;
                else if (!strcmp(k, "winout")) g->ppu.winout = v;
                else if (!strcmp(k, "bldcnt")) g->ppu.bldcnt = v;
                else if (!strcmp(k, "bldalpha")) g->ppu.bldalpha = v;
                else if (!strcmp(k, "bldy")) g->ppu.bldy = v;
                else if (!strcmp(k, "mosaic")) g->ppu.mosaic = v;
                else if (!strcmp(k, "bgx0")) g->ppu.refx[0] = v;
                else if (!strcmp(k, "bgy0")) g->ppu.refy[0] = v;
                else if (!strcmp(k, "pa0")) g->ppu.pa[0] = (int16_t)v;
                else if (!strcmp(k, "pb0")) g->ppu.pb[0] = (int16_t)v;
                else if (!strcmp(k, "pc0")) g->ppu.pc[0] = (int16_t)v;
                else if (!strcmp(k, "pd0")) g->ppu.pd[0] = (int16_t)v;
            }
            fclose(f);
        }
        ppu_render_frame(g);
        snprintf(p, sizeof(p), "%s.render.screen.bin", argv[4]);
        f = fopen(p, "wb"); fwrite(g->ppu.frame, 1, sizeof(g->ppu.frame), f); fclose(f);
        snprintf(p, sizeof(p), "%s.render.dbspcnt", argv[4]);
        f = fopen(p, "wb"); fwrite(&g->ppu.dispcnt, 2, 1, f); fclose(f);
        printf("rendered %s.render.screen.bin (dispcnt=%04X)\n", argv[4], g->ppu.dispcnt);
    } else if (!strcmp(cmd, "findshot")) {
        /* arg = output .ppm; run frames, capture the first frame where the
           display leaves forced blank (DISPCNT bit 7 clear) with >1000
           non-black pixels. This is the boot-agnostic "screen on" point. */
        FILE *fo = NULL;
        for (int f = 0; f < nframes && !fo; f++) {
            gba_run_frame(g);
            if ((g->ppu.dispcnt & 0x0080) == 0) {
                int nonblack = 0;
                for (int i = 0; i < SCREEN_W * SCREEN_H; i++)
                    if (g->ppu.frame[i]) nonblack++;
                if (nonblack > 1000) {
                    char p[512];
                    snprintf(p, sizeof(p), "%s", argv[4]);
                    fo = fopen(p, "wb");
                    fprintf(fo, "P6\n240 160\n255\n");
                    for (int i = 0; i < SCREEN_W * SCREEN_H; i++) {
                        uint16_t c = g->ppu.frame[i];
                        uint8_t r = (c & 0x1F) << 3;
                        uint8_t g2 = ((c >> 5) & 0x1F) << 3;
                        uint8_t b = ((c >> 10) & 0x1F) << 3;
                        unsigned char rgb[3] = { r | (r >> 5), g2 | (g2 >> 5), b | (b >> 5) };
                        fwrite(rgb, 1, 3, fo);
                    }
                    fclose(fo);
                    printf("screen-on at frame %d\n", f);
                }
            }
        }
        if (!fo) printf("no screen-on frame in %d frames\n", nframes);
    } else if (!strcmp(cmd, "audio")) {
        /* print audio regs / fifo state */
        printf("audio regs: 60=%04X 62=%04X 64=%04X 68=%04X 6C=%04X 70=%04X 72=%04X 74=%04X 78=%04X 7C=%04X 80=%04X 82=%04X 84=%04X 88=%04X\n",
               apu_read_reg(g, 0x60), apu_read_reg(g, 0x62), apu_read_reg(g, 0x64),
               apu_read_reg(g, 0x68), apu_read_reg(g, 0x6C), apu_read_reg(g, 0x70),
               apu_read_reg(g, 0x72), apu_read_reg(g, 0x74), apu_read_reg(g, 0x78),
               apu_read_reg(g, 0x7C), apu_read_reg(g, 0x80), apu_read_reg(g, 0x82),
               apu_read_reg(g, 0x84), apu_read_reg(g, 0x88));
        printf("fifoA held=%d len=%d head=%d | fifoB held=%d len=%d head=%d\n",
               g->apu.fifo_held[0], g->apu.fifo_len[0], g->apu.fifo_head[0],
               g->apu.fifo_held[1], g->apu.fifo_len[1], g->apu.fifo_head[1]);
        for (int i = 0; i < 4; i++) {
            ApuCh *c = &g->apu.ch[i];
            printf("ch%d on=%d vol=%d env_t=%d len=%d pos=%d cc=%llu\n", i, c->on, c->vol,
                   c->env_t, c->len, c->pos, (unsigned long long)c->cc);
        }
    } else if (!strcmp(cmd, "layers")) {
        /* Render one frame with only OBJ (dispcnt clears BG bits) or only BG.
           Writes obj.ppm / bg.ppm. */
        uint16_t dc = g->ppu.dispcnt;
        ppu_frame_start(g);
        g->ppu.dispcnt = dc & ~0x0F00;           /* OBJ still on, BG off */
        for (int yy = 0; yy < SCREEN_H; yy++) ppu_render_line(g, yy);
        FILE* fo = fopen("/tmp/obj.ppm", "wb"); fprintf(fo, "P6\n240 160\n255\n");
        for (int i = 0; i < SCREEN_W * SCREEN_H; i++) {
            uint16_t c = g->ppu.frame[i];
            unsigned char rgb[3] = { ((c & 0x1F) << 3) | (((c & 0x1F) >> 2)),
                                     (((c >> 5) & 0x1F) << 3) | ((((c >> 5) & 0x1F) >> 2)),
                                     (((c >> 10) & 0x1F) << 3) | ((((c >> 10) & 0x1F) >> 2)) };
            fwrite(rgb, 1, 3, fo);
        }
        fclose(fo);
        g->ppu.dispcnt = dc | 0x0F00;            /* BG on, OBJ off */
        ppu_frame_start(g);
        for (int yy = 0; yy < SCREEN_H; yy++) ppu_render_line(g, yy);
        FILE* fb = fopen("/tmp/bg.ppm", "wb"); fprintf(fb, "P6\n240 160\n255\n");
        for (int i = 0; i < SCREEN_W * SCREEN_H; i++) {
            uint16_t c = g->ppu.frame[i];
            unsigned char rgb[3] = { ((c & 0x1F) << 3) | (((c & 0x1F) >> 2)),
                                     (((c >> 5) & 0x1F) << 3) | ((((c >> 5) & 0x1F) >> 2)),
                                     (((c >> 10) & 0x1F) << 3) | ((((c >> 10) & 0x1F) >> 2)) };
            fwrite(rgb, 1, 3, fb);
        }
        fclose(fb);
        puts("wrote /tmp/obj.ppm /tmp/bg.ppm");
    } else if (!strcmp(cmd, "shot")) {
        /* arg = output .ppm; RGB555 has R in low bits */
        FILE *f = fopen(argv[4], "wb");
        fprintf(f, "P6\n240 160\n255\n");
        for (int i = 0; i < SCREEN_W * SCREEN_H; i++) {
            uint16_t c = g->ppu.frame[i];
            uint8_t r =  (c      & 0x1F) << 3;
            uint8_t g2 = ((c >> 5) & 0x1F) << 3;
            uint8_t b =  ((c >> 10) & 0x1F) << 3;
            unsigned char rgb[3] = { r | (r >> 5), g2 | (g2 >> 5), b | (b >> 5) };
            fwrite(rgb, 1, 3, f);
        }
        fclose(f);
        printf("wrote %s\n", argv[4]);
    } else if (!strcmp(cmd, "raw")) {
        FILE *f = fopen(argv[4], "w");
        for (int i = 0; i < 0x400; i++) fprintf(f, "%02X", g->io[i]);
        fprintf(f, "|");
        for (int i = 0; i < 0x400; i++) fprintf(f, "%08X", (unsigned)g->ppu.bgx[0] + i); /* placeholder */
        fclose(f);
        printf("raw io written\n");
    }
    free(g);
    return 0;
}