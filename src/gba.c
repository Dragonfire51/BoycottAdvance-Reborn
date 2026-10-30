/*
 * gba.c - GBA system core: init/reset, ROM load, frame loop, timing.
 * Clean-room implementation.
 */
#include "gba.h"
#include "mem.h"
#include "arm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const uint8_t builtin_bios[BIOS_SIZE] = {0};   /* zeros OK: many titles boot */

void gba_init(GBA *g) {
    memset(g, 0, sizeof(GBA));
    g->frameskip = 0;
    g->throttle = 60;
    g->bios_enabled = true;
    g->bios_fastboot = true;
    g->save_type = 0;
    g->cpu.cpsr = 0x0000001F;   /* SVC mode, IRQ disabled */
    g->cpu.m = 0;
    g->ppu.dispcnt = 0x0000;
    g->ppu.dispstat = 0;
    g->ppu.vcount = 0;
    g->ie = 0;
    g->if_reg = 0;
    g->ime = false;
    g->key_input = 0x03FF;        /* all released (low active) */
    g->cycle = 0;
    g->frame_base = 0;
    g->frame_count = 0;
    g->n_cheats = 0;
    g->state_buf = NULL;
    g->state_size = 0;
    g->rom_mask = ROM_MAX - 1;
    /* built-ins */
    memcpy(g->bios, builtin_bios, BIOS_SIZE);
}

void gba_reset(GBA *g) {
    memset(g->ewram, 0, sizeof(g->ewram));
    memset(g->iwram, 0, sizeof(g->iwram));
    memset(g->vram, 0, sizeof(g->vram));
    memset(g->oam, 0, sizeof(g->oam));
    memset(g->pal, 0, sizeof(g->pal));
    memset(g->io, 0, sizeof(g->io));
    memset(g->io_raw, 0, sizeof(g->io_raw));
    memset(&g->timer, 0, sizeof(g->timer));
    memset(&g->dma, 0, sizeof(g->dma));
    memset(&g->apu, 0, sizeof(g->apu));
    memset(&g->ppu, 0, sizeof(g->ppu));
    /* GBATEK / mGBA GBAIOInit: the affine parameters reset to the identity
       matrix (PA = PD = 256). Bitmap modes 3-5 use BG2's matrix, so a game
       that never writes BG2PA/PD would otherwise render a degenerate
       (collapsed) bitmap instead of the unscaled one. */
    g->ppu.pa[0] = g->ppu.pd[0] = 256;
    g->ppu.pa[1] = g->ppu.pd[1] = 256;
    g->cycle = 0;
    g->frame_base = 0;
    g->frame_count = 0;
    g->cst.halt = false;
    g->cst.swi_stall = 0;
    g->cst.stopped = false;
    g->cst.wait_mask = 0;
    g->ime = false;
    g->if_reg = 0;
    g->ie = 0;
    g->ppu.vcount = 0;

    /* Boot. The default (BIOS fast boot) replicates the state the real BIOS
       leaves after its init and jumps straight into the cartridge, so no
       copyrighted BIOS dump is required. With fast boot disabled, a BIOS
       image present and the intro not skipped, the ARM7 starts at
       0x00000000 and runs the BIOS. */
    if (g->bios_enabled && !g->bios_fastboot && !g->skip_intros &&
        g->bios_loaded) {
        g->cpu.cpsr = 0x0000001F;        /* system mode, ARM state, IRQ enabled */
        for (int i = 0; i < 16; i++) g->cpu.r[i] = 0;
        g->cpu.r[13] = 0x03007F00;       /* SP (USR/SYS) */
        g->cpu.r[15] = 0x00000000;       /* BIOS entry */
        g->cpu.bank[BANK_IRQ][5] = 0x03007FA0;
        g->cpu.bank[BANK_SVC][5] = 0x03007FE0;
        g->cpu.bank[0][5] = 0x03007F00;
        g->io[0x130] = 0xFF; g->io[0x131] = 0x03;
        g->key_input = 0x03FF;
        return;
    }

    g->cpu.cpsr = 0x0000001F;            /* system mode, ARM state, IRQ enabled */
    for (int i = 0; i < 16; i++) g->cpu.r[i] = 0;
    g->cpu.r[13] = 0x03007F00;           /* SP (USR/SYS) */
    g->cpu.r[15] = 0x08000000;           /* entry point in ROM */
    g->cpu.bank[BANK_IRQ][5] = 0x03007FA0;
    g->cpu.bank[BANK_SVC][5] = 0x03007FE0;
    g->cpu.bank[0][5] = 0x03007F00;
    /* default I/O state the BIOS sets up */
    g->io[0x204] = 0x00; g->io[0x205] = 0x00;
    g->io[0x130] = 0xFF; g->io[0x131] = 0x03;
    g->io[0x132] = 0x00; g->io[0x133] = 0x00;
    g->key_input = 0x03FF;

    /* Replicate the register state the real BIOS leaves behind when it hands
       control to the cartridge. Titles such as Pokemon Emerald queue their
       display writes and only drain the queue from their VBlank handler, so
       if the VBlank interrupt is not already armed the queue never flushes:
       DISPSTAT is never written, the handler never runs, and the game
       deadlocks. Leaving VBlank armed (DISPSTAT bit 3 + IE bit 0 + IME) and
       the display in forced blank mirrors the hardware and lets the queue
       drain. The IRQ vector points at the BIOS default handler so the first
       VBlank before the game installs its own handler is harmless.

       BA_NO_PREARM=1 disables this pre-arming for A/B testing: mGBA and real
       hardware boot with IME=0 and let the ROM arm interrupts itself. */
    if (!getenv("BA_NO_PREARM")) {
        g->ppu.dispcnt |= 0x0080;            /* forced blank */
        g->ppu.dispstat |= 0x0008;           /* VBlank IRQ enable */
        g->ie |= 0x0001;                     /* IE: VBlank */
        g->ime = true;
    }
    gba_write8(g, 0x04000300, 1);        /* POSTFLG: BIOS has run */
    /* SIOCNT: the real BIOS leaves the serial port in 32-bit multi-play mode
       with the IRQ bit set (0x6003).  Our HLE fast boot left it at zero, which
       a title that polls SIOCNT (link menus, some save routines) can see. */
    g->io[0x128] = 0x03; g->io[0x129] = 0x60;
    /* Point the IRQ vector at the BIOS default handler, as the real BIOS does.
       Only meaningful when a BIOS image is mapped: with fast boot and no dump
       the BIOS region reads as zero, so an interrupt taken before the game
       installs its own handler would execute empty space. */
    if (g->bios_loaded)
        gba_write32(g, 0x03FFFFFC, 0x00000300);  /* BIOS default IRQ handler */
    else
        bios_install_stub(g);        /* HLE stub so the first IRQ is harmless */

    /* Tras el arranque real, el prefetcher de la BIOS retiene la palabra que
       sigue a la ultima instruccion ejecutada en 0x0DC (vector de reset): una
       lectura a la BIOS desde fuera devuelve [0x0E4] = E129F000. */
    g->bios_last_pc = 0xFFFFFFFFu;
    g->bios_prefetch = 0xE129F000u;

    bus_update_waitstates(g);            /* derive region timing from WAITCNT */
}

void gba_load_bios(GBA *g, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return;
    size_t got = fread(g->bios, 1, BIOS_SIZE, f);
    if (got < BIOS_SIZE) memset(g->bios + got, 0, BIOS_SIZE - got);
    fclose(f);
    g->bios_enabled = true;
    g->bios_loaded = (got == BIOS_SIZE);
}

bool gba_load_rom(GBA *g, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > ROM_MAX) { fclose(f); return false; }
    size_t got = fread(g->rom, 1, (size_t)sz, f);
    fclose(f);
    g->rom_size = (uint32_t)got;
    g->rom_mask = 0x1FFFFFF;
    if (g->rom_size <= 0x20000) g->rom_mask = 0x1FFFF;
    else if (g->rom_size <= 0x40000) g->rom_mask = 0x3FFFF;    /* 256K */
    else if (g->rom_size <= 0x80000) g->rom_mask = 0x7FFFF;    /* 512K */
    else if (g->rom_size <= 0x100000) g->rom_mask = 0xFFFFF;   /* 1M */
    else if (g->rom_size <= 0x200000) g->rom_mask = 0x1FFFFF;  /* 2M */
    else { g->rom_mask = 0x1FFFFFF; }
    /* title */
    memcpy(g->rom_title, g->rom + 0xA0, 12);
    g->rom_title[12] = 0;
    /* Save type from the marker strings Nintendo's save drivers leave in
       the ROM image (same heuristic as the reference implementations). */
    g->save_type = SAVE_NONE;
    for (uint32_t i = 0; i + 12 <= g->rom_size; i++) {
        if (memcmp(g->rom + i, "FLASH1M_V", 9) == 0) { g->save_type = SAVE_FLASH128; break; }
        if (memcmp(g->rom + i, "FLASH512_V", 10) == 0 ||
            memcmp(g->rom + i, "FLASH_V", 7) == 0) { g->save_type = SAVE_FLASH64; break; }
        if (memcmp(g->rom + i, "SRAM_V", 6) == 0) { g->save_type = SAVE_SRAM; break; }
        if (memcmp(g->rom + i, "EEPROM_V", 8) == 0) { g->save_type = SAVE_EEPROM; break; }
    }
    g->save_size = g->save_type == SAVE_FLASH128 ? 0x20000 :
                   g->save_type == SAVE_FLASH64  ? 0x10000 :
                   g->save_type == SAVE_SRAM     ? 0x8000  :
                   g->save_type == SAVE_EEPROM   ? EEPROM_SIZE : 0;
    memset(g->save_mem, 0xFF, sizeof(g->save_mem));
    g->save_dirty = false;
    eeprom_init(g);
    flash_init(g);
    gba_load_battery(g, path);
    return true;
}

/* Battery save file: the ROM path with a .sav suffix. */
static void sav_path(const char *rom_path, char *out, size_t n) {
    snprintf(out, n, "%s.sav", rom_path);
}

void gba_load_battery(GBA *g, const char *rom_path) {
    char p[1200];
    sav_path(rom_path, p, sizeof p);
    FILE *f = fopen(p, "rb");
    if (!f) return;
    if (g->save_type == SAVE_EEPROM) {
        size_t got = fread(g->eeprom.data, 1, sizeof(g->eeprom.data), f);
        if (got < 0x200) memset(g->eeprom.data, 0xFF, sizeof(g->eeprom.data));
    } else {
        size_t got = fread(g->save_mem, 1, g->save_size, f);
        if (got < g->save_size) memset(g->save_mem + got, 0xFF, g->save_size - got);
    }
    fclose(f);
}

bool gba_save_battery(GBA *g, const char *rom_path) {
    if (g->save_type == SAVE_NONE || !rom_path || !rom_path[0]) return false;
    char p[1200];
    sav_path(rom_path, p, sizeof p);
    FILE *f = fopen(p, "wb");
    if (!f) return false;
    if (g->save_type == SAVE_EEPROM) fwrite(g->eeprom.data, 1, sizeof(g->eeprom.data), f);
    else                            fwrite(g->save_mem, 1, g->save_size, f);
    fclose(f);
    g->save_dirty = false;
    return true;
}

void gba_set_savetype(GBA *g, int type) {
    g->save_type = type;
    g->save_size = type == SAVE_FLASH128 ? 0x20000 :
                   type == SAVE_FLASH64  ? 0x10000 :
                   type == SAVE_SRAM     ? 0x8000  :
                   type == SAVE_EEPROM   ? EEPROM_SIZE : 0;
    flash_init(g);
}

/* update keypad values */
void gba_update_keypad(GBA *g, uint16_t keys) {
    /* `keys` ya es activo en bajo (0 = pulsado), igual que KEYINPUT:
       se guarda tal cual, sin invertir (GBATEK). */
    g->key_input = (uint16_t)(keys & 0x3FF);
    io_trigger_keypad(g, 0);
}

/* Run one video frame: step the PPU line by line, running the CPU for the
 * cycles each scanline takes, firing HBlank/VBlank/VCount interrupts. */
static void run_frame_core(GBA *g) {
    /* La linea dura 1232 ciclos (GBATEK "LCD VBlank"): 240 px visibles = 960
       ciclos, pero el flag/IRQ de HBlank y el DMA de HBlank NO se disparan al
       acabar los pixeles visibles sino 48 ciclos despues, tras la cola de
       fetch del PPU.  mGBA (VIDEO_HDRAW_LENGTH=1008 + VIDEO_HBLANK_LENGTH=224)
       y VBA-M (lcdHDrawLen()=1008) coinciden en 1008; usar 960 adelantaba
       todas las IRQ y el DMA de HBlank 48 ciclos por linea. */
    const uint32_t hdraw_cycles   = 1008;    /* 240 px + 48 de cola de fetch */
    const uint32_t hblank_cycles  = 224;     /* total 1232 por linea */
    const uint32_t line_cycles    = hdraw_cycles + hblank_cycles;
    /* La PPU marca el reloj: cada linea dura exactamente 1232 ciclos y el frame
       228*1232 = 280896.  Los limites son absolutos respecto a `base`, de modo
       que si el CPU se pasa del limite al acabar una instruccion, la linea
       siguiente arranca en el limite ideal (recupera el exceso) en vez de
       sumarlo.  Con presupuestos relativos el exceso de cada una de las 456
       interrupciones de linea se acumulaba (+1.7% por frame) y el estado del
       juego derivaba de fase respecto al hardware. */
    uint32_t base = g->frame_base;           /* reloj ideal del PPU, no el de la CPU */
    /* El reloj del PPU no debe quedar por delante de la CPU ni retrasarse mas de
       un frame (p.ej. tras cargar un savestate, que no serializa ninguno de los
       dos): en ese caso resincroniza con el contador de la CPU para no colgar el
       bucle con presupuestos negativos. */
    if ((int32_t)(base - g->cycle) > 0 ||
        (int32_t)(g->cycle - base) > (int32_t)(228u * line_cycles))
        base = g->cycle;
    g->frame_base = base + 228u * line_cycles;

    ppu_frame_start(g);                      /* recarga puntos de referencia afines */
    apu_step(g, 0);

    for (int line = 0; line < 228; line++) {
        int vm = (g->ppu.dispstat >> 8) & 0xFF;
        g->ppu.vcount = (uint16_t)line;
        /* GBATEK (4000004h): the H-Blank flag is toggled on every scanline
           (0..227); the visible part of the line starts out of HBlank. */
        g->ppu.hblank = false;

        /* DISPSTAT bits 0-1: bit0 VBlank (lineas 160-226), bit1 HBlank en la
           parte no visible; bit2 VCount match.  Bit1 lo mantiene el nucleo. */
        uint16_t st = (uint16_t)(g->ppu.dispstat & 0xFFF8);
        if (line >= 160 && line <= 226) st |= 0x0001;        /* VBlank (160..226, no 227) */
        if (line == vm)  st |= 0x0004;                      /* VCount match */
        g->ppu.dispstat = st;

        /* VBlank: GBATEK (4000004h) sets the V-Blank flag in line 160..226, and
           mGBA (GBA_VIDEO_VERTICAL_PIXELS) and VBA-M (VCOUNT == 160) both raise
           the VBlank IRQ / run the VBlank DMA when VCOUNT reaches 160, i.e. at
           the start of the first hidden line.  Firing it in line 159 ran the
           game's VBlank handler (OAM/scroll/palette copy) during the last
           visible line and corrupted it. */
        if (line == 160) {
            if (g->ppu.dispstat & 0x0008) g->if_reg |= IF_VBLANK;
            g->frame_count++;
            /* VBlankIntrWait satisfied; a wait on another mask is woken by
               arm_execute when its own flag comes up. */
            if (g->cst.halt && g->cst.wait_mask == 0) g->cst.halt = false;
            dma_trigger(g, 1);                      /* VBlank-scheduled DMA */
        }
        /* VCount match IRQ */
        if ((g->ppu.dispstat & 0x0020) && line == vm) g->if_reg |= IF_VCOUNT;

        /* parte visible de la linea: la CPU corre hasta el limite absoluto y al
           acabar se renderiza la linea con los registros tal como han quedado */
        uint32_t vis_end = base + (uint32_t)line * line_cycles + hdraw_cycles;
        uint32_t seg0 = g->cycle;
        int vis_budget = (int)(vis_end - seg0);
        if (vis_budget < 0) vis_budget = 0;
        io_update_timers(g, (uint32_t)vis_budget);
        arm_execute(g, (uint32_t)vis_budget);
        apu_step(g, g->cycle - seg0);
        if (line < 160) ppu_render_line(g, line);

        /* HBlank: la CPU corre hasta el limite de la linea; DMA e IRQ de HBlank.
           Los cambios de BGxHOFS, WINx, BLDx hechos aqui afectan a la linea
           siguiente (efectos de HDMA como Pokédex / transiciones). */
        g->ppu.dispstat |= 0x0002;
        g->ppu.hblank = true;
        if (g->ppu.dispstat & 0x0010) g->if_reg |= IF_HBLANK;
        if (line < 160) dma_trigger(g, 2);          /* HBlank-scheduled DMA */
        uint32_t hb_end = base + (uint32_t)(line + 1) * line_cycles;
        uint32_t seg1 = g->cycle;
        int hb_budget = (int)(hb_end - seg1);
        if (hb_budget < 0) hb_budget = 0;
        io_update_timers(g, (uint32_t)hb_budget);
        arm_execute(g, (uint32_t)hb_budget);
        apu_step(g, g->cycle - seg1);
    }
    g->ppu.vcount = 0;
    /* Between frames the scanline status bits must be clear: the real LCD
       leaves VBlank/HBlank/VCount flags at 0 when line 0 starts. Keeping
       HBlank set here (as the loop leaves it on the last VBlank line) made
       games that time their boot with DISPSTAT see a spurious HBlank every
       frame boundary. */
    g->ppu.dispstat &= 0xFFF8;
}

void gba_run_frame(GBA *g) {
    run_frame_core(g);
}

/* cheats */
void gba_add_cheat(GBA *g, uint32_t addr, uint16_t value) {
    if (g->n_cheats < 100) {
        g->cheats[g->n_cheats].addr = addr & 0xFFFFFF;
        g->cheats[g->n_cheats].value = value;
        g->cheats[g->n_cheats].stored = 0;
        g->cheats[g->n_cheats].active = true;
        g->cheats[g->n_cheats].is_gameshark = false;
        g->n_cheats++;
    }
}
void gba_clear_cheats(GBA *g) { g->n_cheats = 0; }
void gba_apply_cheats(GBA *g) {
    for (int i = 0; i < g->n_cheats; i++) {
        if (!g->cheats[i].active) continue;
        uint32_t addr = g->cheats[i].addr;
        uint16_t v = g->cheats[i].value;
        if (addr >= 0x02000000 && addr < 0x02040000)
            gba_write16(g, addr, v);
        else if (addr >= 0x04000000 && addr < 0x04000400) {
            if ((addr & 3) == 0) gba_write16(g, addr, v);
        }
    }
}

int gba_save_state(GBA *g, const char *path) {
    static uint8_t buf[1 << 20];
    size_t n = 0;
    /* store header */
    memcpy(buf+n, "BARS", 4); n += 4;
    buf[n++] = 5;   /* version */
    /* CPU */
    memcpy(buf+n, &g->cpu, sizeof(g->cpu)); n += sizeof(g->cpu);
    memcpy(buf+n, &g->cst, sizeof(g->cst)); n += sizeof(g->cst);
    memcpy(buf+n, &g->cycle, sizeof(g->cycle)); n += sizeof(g->cycle);
    /* mem */
    memcpy(buf+n, g->ewram, sizeof(g->ewram)); n += sizeof(g->ewram);
    memcpy(buf+n, g->iwram, sizeof(g->iwram)); n += sizeof(g->iwram);
    memcpy(buf+n, g->vram, sizeof(g->vram));   n += sizeof(g->vram);
    memcpy(buf+n, g->oam, sizeof(g->oam));     n += sizeof(g->oam);
    memcpy(buf+n, g->pal, sizeof(g->pal));     n += sizeof(g->pal);
    memcpy(buf+n, g->io, sizeof(g->io));       n += sizeof(g->io);
    memcpy(buf+n, g->io_raw, sizeof(g->io_raw)); n += sizeof(g->io_raw);
    memcpy(buf+n, g->save_mem, sizeof(g->save_mem)); n += sizeof(g->save_mem);
    memcpy(buf+n, &g->eeprom, sizeof(g->eeprom)); n += sizeof(g->eeprom);
    memcpy(buf+n, &g->ie, sizeof(g->ie)); n += sizeof(g->ie);
    memcpy(buf+n, &g->if_reg, sizeof(g->if_reg)); n += sizeof(g->if_reg);
    memcpy(buf+n, &g->ime, sizeof(g->ime)); n += sizeof(g->ime);
    memcpy(buf+n, &g->ppu, sizeof(g->ppu)); n += sizeof(g->ppu);
    memcpy(buf+n, &g->timer, sizeof(g->timer)); n += sizeof(g->timer);
    memcpy(buf+n, &g->dma, sizeof(g->dma)); n += sizeof(g->dma);
    memcpy(buf+n, &g->apu, sizeof(g->apu)); n += sizeof(g->apu);
    memcpy(buf+n, &g->frameskip, sizeof(g->frameskip)); n += sizeof(g->frameskip);
    memcpy(buf+n, &g->rom_size, sizeof(g->rom_size)); n += sizeof(g->rom_size);
    memcpy(buf+n, &g->save_type, sizeof(g->save_type)); n += sizeof(g->save_type);

    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    fwrite(buf, 1, n, f);
    fclose(f);
    return 0;
}

int gba_load_state(GBA *g, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    static uint8_t buf[1 << 20];
    size_t got = fread(buf, 1, sizeof(buf), f);
    fclose(f);
    size_t n = 0;
    if (got < 8) return -1;
    if (memcmp(buf+n, "BARS", 4) != 0) return -1;
    n += 4;
    int ver = buf[n++];
    if (ver != 5) return -1;
    size_t cpu_sz = sizeof(g->cpu);
    if (n + cpu_sz > got) return -1;
    memcpy(&g->cpu, buf+n, cpu_sz); n += cpu_sz;
    size_t cst_sz = sizeof(g->cst);
    if (n + cst_sz > got) return -1;
    memcpy(&g->cst, buf+n, cst_sz); n += cst_sz;
    if (n + sizeof(g->cycle) > got) return -1;
    memcpy(&g->cycle, buf+n, sizeof(g->cycle)); n += sizeof(g->cycle);
    memcpy(g->ewram, buf+n, sizeof(g->ewram)); n += sizeof(g->ewram);
    memcpy(g->iwram, buf+n, sizeof(g->iwram)); n += sizeof(g->iwram);
    memcpy(g->vram, buf+n, sizeof(g->vram)); n += sizeof(g->vram);
    memcpy(g->oam, buf+n, sizeof(g->oam)); n += sizeof(g->oam);
    memcpy(g->pal, buf+n, sizeof(g->pal)); n += sizeof(g->pal);
    memcpy(g->io, buf+n, sizeof(g->io)); n += sizeof(g->io);
    memcpy(g->io_raw, buf+n, sizeof(g->io_raw)); n += sizeof(g->io_raw);
    memcpy(g->save_mem, buf+n, sizeof(g->save_mem)); n += sizeof(g->save_mem);
    memcpy(&g->eeprom, buf+n, sizeof(g->eeprom)); n += sizeof(g->eeprom);
    memcpy(&g->ie, buf+n, sizeof(g->ie)); n += sizeof(g->ie);
    memcpy(&g->if_reg, buf+n, sizeof(g->if_reg)); n += sizeof(g->if_reg);
    memcpy(&g->ime, buf+n, sizeof(g->ime)); n += sizeof(g->ime);
    memcpy(&g->ppu, buf+n, sizeof(g->ppu)); n += sizeof(g->ppu);
    memcpy(&g->timer, buf+n, sizeof(g->timer)); n += sizeof(g->timer);
    memcpy(&g->dma, buf+n, sizeof(g->dma)); n += sizeof(g->dma);
    memcpy(&g->apu, buf+n, sizeof(g->apu)); n += sizeof(g->apu);
    memcpy(&g->frameskip, buf+n, sizeof(g->frameskip)); n += sizeof(g->frameskip);
    memcpy(&g->rom_size, buf+n, sizeof(g->rom_size)); n += sizeof(g->rom_size);
    memcpy(&g->save_type, buf+n, sizeof(g->save_type)); n += sizeof(g->save_type);
    return 0;
}
