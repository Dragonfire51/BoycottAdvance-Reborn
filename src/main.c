/*
 * main.c - SDL2 frontend for BoycottAdvance Reborn.
 *
 * Fixes vs original BoycottAdvance 0.2.8:
 *  - Audio via SDL2 instead of obsolete WINMM waveOut (broken on Win11).
 *  - Window title shows new version.
 *  - Help menu with About / History (credits to original authors).
 */
#include "gba.h"
#include "mem.h"
#include "arm.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- config from .ini-like defaults ---- */
static const char *g_rom_path = NULL;
static int g_zoom = 3;

/* ---- audio buffer (mono, 44100) ---- */
static int16_t audio_buf[2048];

static uint32_t timer_start_us = 0;

uint32_t get_ticks_us(void) { return (uint32_t)(SDL_GetTicks() * 1000); }
void sleep_ms(int ms) { SDL_Delay(ms); }

#define AUDIO_FREQ 44100
#define AUDIO_CHANNELS 2

static void audio_cb(void *userdata, Uint8 *stream, int len) {
    GBA *g = (GBA *)userdata;
    int16_t *out = (int16_t *)stream;
    int n = len / 2 / AUDIO_CHANNELS;
    apu_mix(g, out, n);
}

/* key mapping (GBA -> SDL scan) */
static int key_map[12] = {
    SDLK_a,      /* A */
    SDLK_s,      /* B */
    SDLK_RETURN, /* START */
    SDLK_BACKSPACE, /* SELECT */
    SDLK_RIGHT,  /* RIGHT */
    SDLK_LEFT,   /* LEFT */
    SDLK_UP,     /* UP */
    SDLK_DOWN,   /* DOWN */
    SDLK_d,      /* R */
    SDLK_f,      /* L */
    SDLK_KP_MINUS, /* FF */
    SDLK_TAB,    /* TB */
};

static inline uint16_t key_state(void) {
    uint16_t keys = 0x03FF;  /* all released */
    const Uint8 *ks = SDL_GetKeyboardState(NULL);
    if (ks[SDL_SCANCODE_A]) keys &= ~0x0001;
    if (ks[SDL_SCANCODE_S]) keys &= ~0x0002;
    if (ks[SDL_SCANCODE_RETURN]) keys &= ~0x0008;
    if (ks[SDL_SCANCODE_BACKSPACE]) keys &= ~0x0004;
    if (ks[SDL_SCANCODE_RIGHT]) keys &= ~0x0010;
    if (ks[SDL_SCANCODE_LEFT]) keys &= ~0x0020;
    if (ks[SDL_SCANCODE_UP]) keys &= ~0x0040;
    if (ks[SDL_SCANCODE_DOWN]) keys &= ~0x0080;
    if (ks[SDL_SCANCODE_D]) keys &= ~0x0100;  /* R */
    if (ks[SDL_SCANCODE_F]) keys &= ~0x0200;  /* L */
    return keys;
}

static void show_dialog(const char *title, const char *msg) {
    char buf[512];
    snprintf(buf, sizeof buf, "%s", msg);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, title, buf, NULL);
}

static void show_about(void) {
    show_dialog("About",
        GBA_TITLE "\n\n"
        "Reconstruction of BoycottAdvance 0.2.8 (clean-room)\n"
        "with a complete ARM7TDMI core, correct BIOS SWI\n"
        "emulation, SDL2 audio/video frontend.\n\n"
        "Fixes: white-screen bug in Pokemon titles (interrupts,\n"
        "CpuSet), sound on Windows 11 (WINMM -> SDL2).\n\n"
        "Original authors:\n"
        "  Julien Frelat \"Gollum\" - main emulation & GBA core\n"
        "  Gollum, Anarko, Fitzy - additional contributors\n\n"
        "This work respects the original cardware license.\n"
        "Credits and thanks to the BoycottAdvance team.");
}

static void show_history(void) {
    show_dialog("History",
        "BoycottAdvance 0.2.8 (2003) - original by Julien Frelat\n"
        "  - GBA emulation, flash/eeprom, bios, debugger\n"
        "  - Pollux/Anarko/Fitzy contributions\n\n"
        "BoycottAdvance Reborn v1.0.0\n"
        "  - Clean-room ARM7TDMI core (full ARM + Thumb)\n"
        "  - Correct BIOS SWI (CpuSet, VBlankIntrWait...)\n"
        "  - Fixed VBlank/HBlank/IRQ (boots Pokemon)\n"
        "  - SDL2 audio/video (Windows 11 compatible)\n"
        "  - Savestates, cheats, BIOS fastboot\n"
        "  - Modern CMake/MinGW build");
}

static void save_screenshot(GBA *g, const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fwrite("P6\n240 160\n255\n", 1, 14, f);
    uint8_t row[240*3];
    for (int y = 0; y < SCREEN_H; y++) {
        for (int x = 0; x < SCREEN_W; x++) {
            uint16_t c = g->ppu.frame[y*SCREEN_W+x];
            int r = (c & 0x1F) << 3, g2 = ((c>>5)&0x1F) << 3, b = ((c>>10)&0x1F) << 3;
            row[x*3+0] = r; row[x*3+1] = g2; row[x*3+2] = b;
        }
        fwrite(row, 1, sizeof row, f);
    }
    fclose(f);
}

int main(int argc, char **argv) {
    if (argc > 1) g_rom_path = argv[1];

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
        fprintf(stderr, "SDL init failed: %s\n", SDL_GetError());
        return 1;
    }

    /* GBA holds the full ROM buffer (up to 32 MB); keep it off the stack.
       Windows default stack is 1 MB, so a stack-allocated GBA dies instantly. */
    GBA *gp = (GBA *)calloc(1, sizeof(GBA));
    if (!gp) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "BoycottAdvance Reborn",
            "Not enough memory to allocate the emulator core.", NULL);
        SDL_Quit();
        return 1;
    }
    GBA *g = gp;
    gba_init(g);

    /* try to load bundled bios */
    gba_load_bios(g, "gba_bios.bin");
    FILE *bf = fopen("gba_bios.bin", "rb");
    if (bf) { fclose(bf); }
    else { /* fallback zero bios */ }

    if (g_rom_path) {
        if (!gba_load_rom(g, g_rom_path)) {
            fprintf(stderr, "cannot load ROM: %s\n", g_rom_path);
            SDL_Quit();
            return 1;
        }
        gba_reset(g);
    } else {
        gba_reset(g);
    }

    /* window */
    int w = 240 * 2, h = 160 * 2;
    SDL_Window *win = SDL_CreateWindow(
        GBA_TITLE " - " GBA_NAME,
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        w, h, SDL_WINDOW_RESIZABLE);
    if (!win) { SDL_Quit(); fprintf(stderr,"no window\n"); return 1; }
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING, SCREEN_W, SCREEN_H);

    /* audio */
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = AUDIO_FREQ;
    want.format = AUDIO_S16SYS;
    want.channels = AUDIO_CHANNELS;
    want.samples = 1024;
    want.callback = audio_cb;
    want.userdata = g;
    SDL_AudioDeviceID adev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (adev) SDL_PauseAudioDevice(adev, 0);

    SDL_SetWindowTitle(win, GBA_TITLE " - " GBA_NAME);
    uint32_t *pixels = (uint32_t *)malloc(sizeof(uint32_t)*240*160);
    if (!pixels) { SDL_DestroyWindow(win); SDL_Quit(); free(gp); return 1; }
    SDL_PixelFormat *fmt = SDL_AllocFormat(SDL_PIXELFORMAT_ARGB8888);

    int running = 1, paused = 0;
    uint64_t last = SDL_GetTicks64();
    uint32_t frame_accum = 0;

    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
            case SDL_QUIT: running = 0; break;
            case SDL_KEYDOWN:
                if (ev.key.keysym.sym == SDLK_ESCAPE) running = 0;
                if (ev.key.keysym.sym == SDLK_p) paused = !paused;
                if (ev.key.keysym.sym == SDLK_F2) gba_save_state(g, "state.sav");
                if (ev.key.keysym.sym == SDLK_F3) gba_load_state(g, "state.sav");
                if (ev.key.keysym.sym == SDLK_F5) save_screenshot(g, "shot.ppm");
                if (ev.key.keysym.sym == SDLK_F1) show_about();
                if (ev.key.keysym.sym == SDLK_F9) show_history();
                break;
            }
        }
        if (!running) break;

        /* run one frame if not paused */
        if (!paused) {
            gba_update_keypad(g, key_state());
            gba_run_frame(g);
            gba_apply_cheats(g);
        }

        /* blit */
        for (int i = 0; i < 240*160; i++) {
            uint16_t c = g->ppu.frame[i];
            int r = (c & 0x1F) << 3, g2 = ((c>>5)&0x1F) << 3, b = ((c>>10)&0x1F) << 3;
            pixels[i] = 0xFF000000u | (r<<16) | (g2<<8) | b;
        }
        SDL_UpdateTexture(tex, NULL, pixels, 240*4);
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, NULL, NULL);
        SDL_RenderPresent(ren);

        /* throttle to 60fps */
        uint64_t now = SDL_GetTicks64();
        uint64_t elapsed = now - last;
        uint32_t target = 16;
        if (elapsed < target) SDL_Delay(target - elapsed);
        last = SDL_GetTicks64();
        (void)frame_accum;
    }

    if (adev) SDL_CloseAudioDevice(adev);
    free(pixels);
    free(gp);
    SDL_FreeFormat(fmt);
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}