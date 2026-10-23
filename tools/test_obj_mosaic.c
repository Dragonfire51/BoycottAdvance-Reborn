#include "gba.h"
#include "arm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Craftea un frame sintético: modo 0 con BG0 (tiles), un OBJ con mosaic.
   Verifica que el muestreo de píxeles cambia según el mosaic del registro MOSAIC. */

static uint32_t crc32(const uint8_t *d, size_t n) {
    uint32_t c=0xFFFFFFFF; for(size_t i=0;i<n;i++){ c^=d[i]; for(int k=0;k<8;k++) c = (c>>1) ^ (0xEDB88320u & -(c&1)); } return ~c;
}

int main(void) {
    GBA *g = calloc(1,sizeof(GBA)); gba_init(g); gba_load_bios(g,"gba_bios.bin");
    if(!gba_load_rom(g,"Pokemon Esmeralda.gba")){printf("fail rom\n");return 1;} gba_reset(g);

    /* OAM atributos big-endian de 16 bits; aquí los escribe little-endian byte a byte.
       attr0 = Y(0-7) | rot(8) | disable(9) | mode(10-11) | mosaic(12) | 256col(13) | shape(14-15)
       attr1 = X(0-8) | hflip(12) | vflip(13) | size(14-15)
       attr2 = tile(0-9) | pri(10-11) | palbank(12-15) */
    uint8_t oam[512]; memset(oam,0,sizeof(oam));
    oam[0] = 20;                       /* Y=20 */
    oam[1] = 0x10;                     /* bit 12 = mosaic, shape 0, no rot, normal */
    oam[2] = 40; oam[3] = 0x40;        /* X=40, size=1 (16x16) */
    oam[4] = 32; oam[5] = 0x00;        /* tile 32, pri 0, palbank 0 */
    memcpy(g->oam, oam, 512);

    /* Tile OBJ en char base de OBJ (modo 0 -> 0x10000). tno=32 => slot 32 => 0x10400.
       16 colores: 4 bytes por fila. pixeles= (fila*3+col+1) para distinguir bloques. */
    for (int ty = 0; ty < 2; ty++)
        for (int y = 0; y < 8; y++) {
            uint32_t base = 0x10000 + (uint32_t)((32 + ty*1) * 32) + (uint32_t)y * 4;
            for (int col = 0; col < 4; col++) {
                /* byte: pixel 2*col = ((ty*8+y)*3 + 2*col + 1) & 0xF, pixel 2*col+1 = ...+2 */
                uint8_t lo = (uint8_t)(((y + ty*8) * 3 + (col*2) + 1) & 0xF);
                uint8_t hi = (uint8_t)(((y + ty*8) * 3 + (col*2) + 2) & 0xF);
                g->vram[base + col] = (uint8_t)(lo | (hi << 4));
            }
        }
    /* paleta: color 0 negro, colores 1..15 visibles con RGB puro */
    for (int i = 1; i < 16; i++) {
        int colidx = 0x100 + i;
        g->pal[colidx*2] = (uint8_t)((i * 2) & 0x1F);
        g->pal[colidx*2+1] = 0;
    }

    /* modo 0, solo BG0 (mapa vacío -> color 0), OBJ activo */
    g->ppu.bgcnt[0] = 0;
    g->ppu.dispcnt = 0x1100;   /* bit 8 BG0 on, bit 12 OBJ on */

    /* MOSAIC: OBJ H=2 (bits 8-11 = 1), V=1 */
    g->ppu.mosaic = (1 << 8);

    ppu_frame_start(g); for (int y=0;y<160;y++) ppu_render_line(g,y);
    uint32_t c1 = crc32((const uint8_t*)g->ppu.frame, 240*160*2);

    /* Control: mismo frame pero sin mosaic (attr0 sin bit 12) */
    oam[1] = 0x00;
    memcpy(g->oam, oam, 512);
    ppu_frame_start(g); for (int y=0;y<160;y++) ppu_render_line(g,y);
    uint32_t c2 = crc32((const uint8_t*)g->ppu.frame, 240*160*2);

    printf("crc_mosaic=%08X crc_normal=%08X %s\n", c1, c2, c1 != c2 ? "OK: mosaic afecta píxeles" : "FALLO: mismo píxel");
    return c1 != c2 ? 0 : 1;
}