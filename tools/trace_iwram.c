#include "gba.h"
#include "arm.h"
#include <stdio.h>
#include <stdlib.h>
static int valid(uint32_t pc){
    if(pc<0x4000) return 1;
    if(pc>=0x02000000&&pc<0x02040000) return 1;
    if(pc>=0x03000000&&pc<0x03008000) return 1;
    if(pc>=0x08000000&&pc<0x0E000000) return 1;
    return 0;
}
int main(int argc,char**argv){
    GBA*g=calloc(1,sizeof(GBA)); gba_init(g); gba_load_bios(g,"gba_bios.bin");
    if(!gba_load_rom(g,argv[2])){printf("fail\n");return 1;} gba_reset(g);
    uint32_t ring[256]; int rp=0; long steps=0; int entered=0;
    for(int f=1;f<=300;f++){
        for(int line=0;line<228;line++){
            g->ppu.vcount=line;
            if(line==160){ ppu_render_frame(g); g->frame_count++; if(g->cst.halt) g->cst.halt=false; }
            if(g->ppu.dispstat&0x10) g->if_reg|=0x2;
            if(g->ppu.dispstat&0x20){ int vm=(g->ppu.dispstat>>8)&0xFF; if(line==vm) g->if_reg|=0x4; }
            for(int c=0;c<400;c++){
                uint32_t pc=g->cpu.r[15];
                if(!entered && pc>=0x03004000 && pc<0x03060000){
                    printf("first IWRAM exec pc=%08X frame=%d line=%d step=%ld T=%d mode=%d\n",
                        pc,f,line,steps,(g->cpu.cpsr>>5)&1,g->cpu.cpsr&0x1F);
                    printf("last 40 PCs (oldest..newest):\n");
                    for(int k=192;k<256;k++) printf("  %08X\n",ring[(rp+k)%256]);
                    printf("regs:"); for(int r=0;r<16;r++) printf(" r%d=%08X",r,g->cpu.r[r]); printf("\n");
                    entered=1; return 0;
                }
                if(!valid(pc)){
                    printf("INVALID pc=%08X frame=%d line=%d step=%ld cpsr=%08X mode=%d T=%d\n",
                        pc,f,line,steps,g->cpu.cpsr,g->cpu.cpsr&0x1F,(g->cpu.cpsr>>5)&1);
                    { printf("last PCs:\n"); for(int k=0;k<64;k++) printf("  %08X\n",ring[(rp+k)%64]); }
                    
                    printf("regs:"); for(int r=0;r<16;r++) printf(" r%d=%08X",r,g->cpu.r[r]); printf("\n");
                    printf("IWRAM dump 0x03004100..0x03004200:\n");
                    for(uint32_t a=0x03004100;a<0x03004200;a+=16){
                        printf("%08X:",a);
                        for(int b=0;b<16;b++) printf(" %02X",g->iwram[a-0x03000000+b]);
                        printf("\n");
                    }
                    return 0;
                }
                ring[rp]=pc; rp=(rp+1)%64;
                if(!g->cst.halt){ if(g->cpu.cpsr&0x20) thumb_exec(g); else arm_exec(g);} else break;
                steps++;
            }
        }
    }
    printf("no invalid pc; final pc=%08X dispcnt=%04X\n",g->cpu.r[15],g->ppu.dispcnt);
    return 0;
}
