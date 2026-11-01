/*
 * acore.c - portable emulator frontend core (see acore.h).
 *
 * Holds the GBA core instance, the settings, the ROM/BIOS paths and the
 * ARGB conversion. No OS, window or audio-device calls: the frontend keeps
 * those. This is what lets the Android port reuse the very same emulation
 * and settings code as the desktop builds.
 */
#include "acore.h"
#include "gba.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

struct Acore {
    GBA      g;
    AcoreCfg cfg;
    char     rom_path[1024];
    char     bios_path[1024];
    int      have_rom;
};

Acore *acore_create(void) {
    Acore *a = (Acore *)calloc(1, sizeof(Acore));
    if (!a) return NULL;
    gba_init(&a->g);
    apu_open();
    /* Same defaults as the desktop frontend. */
    a->cfg.sound_enable  = 1;
    a->cfg.dsound_a      = 1;
    a->cfg.dsound_b      = 1;
    a->cfg.gbc_sound     = 1;
    a->cfg.biosenable    = 1;
    a->cfg.biosfastboot  = 0;
    a->cfg.frameskip     = 0;
    a->cfg.autoframeskip = 0;
    a->cfg.scanline      = 0;
    a->cfg.smooth        = 0;
    a->cfg.showfps       = 1;
    return a;
}

void acore_destroy(Acore *a) {
    if (!a) return;
    if (a->have_rom) gba_save_battery(&a->g, a->rom_path);
    apu_close();
    free(a);
}

AcoreCfg *acore_cfg(Acore *a) { return &a->cfg; }

bool acore_load_bios(Acore *a, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    fclose(f);
    gba_load_bios(&a->g, path);
    snprintf(a->bios_path, sizeof a->bios_path, "%s", path);
    a->g.bios_enabled  = a->cfg.biosenable ? 1 : 0;
    a->g.bios_fastboot = a->cfg.biosfastboot;
    a->g.skip_intros   = a->cfg.skipintros;
    return a->g.bios_loaded;
}

bool acore_load_rom(Acore *a, const char *path) {
    if (!gba_load_rom(&a->g, path)) return false;
    snprintf(a->rom_path, sizeof a->rom_path, "%s", path);
    a->have_rom = 1;
    gba_reset(&a->g);
    return true;
}

bool acore_rom_loaded(const Acore *a) { return a->have_rom != 0; }
const char *acore_rom_path(const Acore *a) { return a->rom_path; }

void acore_rom_info(const Acore *a, char *title, int title_n,
                    char *code, int code_n, char *savetype, int savetype_n) {
    const GBA *g = &a->g;
    if (title) {
        int n = 0;
        while (n < 12 && g->rom_title[n]) n++;
        snprintf(title, title_n, "%.*s", n, g->rom_title);
    }
    if (code) {
        /* Game code is 4 bytes at 0xAC, maker code 2 at 0xB0. */
        char c[5] = {0};
        if (g->rom_size > 0xB0) memcpy(c, g->rom + 0xAC, 4);
        snprintf(code, code_n, "%s", c);
    }
    if (savetype) {
        const char *s = "none";
        switch (g->save_type) {
        case SAVE_SRAM:     s = "SRAM 32KB";     break;
        case SAVE_FLASH64:  s = "Flash 64KB";    break;
        case SAVE_FLASH128: s = "Flash 128KB";   break;
        case SAVE_EEPROM:   s = "EEPROM";        break;
        default:            s = "none";          break;
        }
        snprintf(savetype, savetype_n, "%s", s);
    }
}

void acore_reset(Acore *a) {
    if (a->have_rom) gba_save_battery(&a->g, a->rom_path);
    gba_reset(&a->g);
}

void acore_save_battery(Acore *a) {
    if (a->have_rom) gba_save_battery(&a->g, a->rom_path);
}

void acore_set_keys(Acore *a, uint16_t mask) {
    /* mask uses ACORE_KEY_* bits (1 = pressed); KEYINPUT is active low. */
    gba_update_keypad(&a->g, (uint16_t)(~mask & 0x3FF));
}

void acore_run_frame(Acore *a) {
    gba_run_frame(&a->g);
    gba_apply_cheats(&a->g);
}

void acore_frame_argb(const Acore *a, uint32_t *out, int out_w, int out_h,
                      int scanline) {
    if (out_w <= 0 || out_h <= 0) return;
    const uint16_t *src = a->g.ppu.frame;
    for (int j = 0; j < out_h; j++) {
        int sy = j * ACORE_H / out_h;
        if (sy >= ACORE_H) sy = ACORE_H - 1;
        int dim = scanline && (sy & 1);
        const uint16_t *srow = src + sy * ACORE_W;
        uint32_t *drow = out + (size_t)j * out_w;
        for (int i = 0; i < out_w; i++) {
            int sx = i * ACORE_W / out_w;
            if (sx >= ACORE_W) sx = ACORE_W - 1;
            uint16_t c = srow[sx];
            int r5 = c & 0x1F, g5 = (c >> 5) & 0x1F, b5 = (c >> 10) & 0x1F;
            int r = (r5 << 3) | (r5 >> 2);
            int gg = (g5 << 3) | (g5 >> 2);
            int b = (b5 << 3) | (b5 >> 2);
            if (dim) { r >>= 1; gg >>= 1; b >>= 1; }
            drow[i] = 0xFF000000u | ((uint32_t)r << 16) |
                      ((uint32_t)gg << 8) | (uint32_t)b;
        }
    }
}

/* Savestate path: <rom>.ss<slot>. */
static void state_path(const Acore *a, int slot, char *out, size_t n) {
    snprintf(out, n, "%s.ss%d", a->rom_path, slot);
}

int acore_save_state(Acore *a, int slot) {
    if (!a->have_rom) return -1;
    char p[1100];
    state_path(a, slot, p, sizeof p);
    return gba_save_state(&a->g, p) == 0 ? 0 : -1;
}

int acore_load_state(Acore *a, int slot) {
    if (!a->have_rom) return -1;
    char p[1100];
    state_path(a, slot, p, sizeof p);
    return gba_load_state(&a->g, p) == 0 ? 0 : -1;
}

int acore_audio_mix(Acore *a, int16_t *out, int nframes) {
    if (!a->cfg.sound_enable) {
        memset(out, 0, (size_t)nframes * ACORE_CHANNELS * sizeof(int16_t));
        return nframes;
    }
    apu_mix(&a->g, out, nframes);
    if (a->cfg.reverse_stereo) {
        for (int i = 0; i < nframes; i++) {
            int16_t l = out[i * 2];
            out[i * 2] = out[i * 2 + 1];
            out[i * 2 + 1] = l;
        }
    }
    return nframes;
}

int acore_audio_available(const Acore *a) { (void)a; return apu_available(); }

int acore_audio_drain_latest(Acore *a, int16_t *out, int nframes) {
    (void)a;
    return apu_drain_latest(out, nframes);
}
