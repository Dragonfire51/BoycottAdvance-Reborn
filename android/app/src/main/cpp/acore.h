/*
 * acore.h - portable emulator frontend core.
 *
 * Wraps the GBA core (gba.c/ppu.c/apu.c/...) with everything a frontend
 * needs but that is not platform-specific: the settings (the same fields
 * the desktop .ini stores), ROM/BIOS loading, savestates, keypad state and
 * the 240x160 RGB555 -> ARGB8888 frame conversion.
 *
 * It has no OS or UI dependency, so the Android port (and the headless
 * tests) drive exactly the same code as the desktop builds. The frontend
 * owns the window, the audio device and the file picker; acore owns the
 * emulation and the configuration.
 */
#ifndef ACORE_FRONTEND_H
#define ACORE_FRONTEND_H
#include <stdint.h>
#include <stdbool.h>

#define ACORE_W 240
#define ACORE_H 160
#define ACORE_SAMPLE_RATE 44100
#define ACORE_CHANNELS 2

/* GBA keypad bits (KEYINPUT convention: active low, 0 = pressed). */
enum {
    ACORE_KEY_A      = 1 << 0,
    ACORE_KEY_B      = 1 << 1,
    ACORE_KEY_SELECT = 1 << 2,
    ACORE_KEY_START  = 1 << 3,
    ACORE_KEY_RIGHT  = 1 << 4,
    ACORE_KEY_LEFT   = 1 << 5,
    ACORE_KEY_UP     = 1 << 6,
    ACORE_KEY_DOWN   = 1 << 7,
    ACORE_KEY_R      = 1 << 8,
    ACORE_KEY_L      = 1 << 9
};

/* Settings, mirroring the desktop .ini keys. */
typedef struct {
    int  sound_enable;
    int  dsound_a, dsound_b, gbc_sound, reverse_stereo;
    int  biosenable, biosfastboot, skipintros;
    int  frameskip, autoframeskip;
    int  scanline, smooth;
    int  state_slot;
    int  showfps;
} AcoreCfg;

typedef struct Acore Acore;

/* Lifecycle. acore_create allocates the core (~34 MB); returns NULL if out
 * of memory. */
Acore *acore_create(void);
void   acore_destroy(Acore *a);

AcoreCfg *acore_cfg(Acore *a);

/* Load a BIOS dump. Returns true if a 16 KB image was read. */
bool acore_load_bios(Acore *a, const char *path);
/* Load a ROM (also loads its .sav next to it). Returns true on success. */
bool acore_load_rom(Acore *a, const char *path);
bool acore_rom_loaded(const Acore *a);
const char *acore_rom_path(const Acore *a);
/* ROM metadata for the UI (may be called after a successful load). */
void acore_rom_info(const Acore *a, char *title, int title_n,
                    char *code, int code_n, char *savetype, int savetype_n);

void acore_reset(Acore *a);
/* Save the battery (.sav) to disk now. */
void acore_save_battery(Acore *a);

/* Keypad: mask is a set of ACORE_KEY_* bits (0 = nothing pressed). */
void acore_set_keys(Acore *a, uint16_t mask);

/* Run one frame into the internal RGB555 buffer, then convert it to a
 * tightly packed ARGB8888 (0xAARRGGBB) buffer of w*h pixels scaled with
 * nearest-neighbour into the caller's target size. out must hold
 * out_w*out_h uint32_t. */
void acore_run_frame(Acore *a);
void acore_frame_argb(const Acore *a, uint32_t *out, int out_w, int out_h,
                      int scanline);

/* Savestates. slot is 0..9; the file lives next to the ROM. */
int  acore_save_state(Acore *a, int slot);
int  acore_load_state(Acore *a, int slot);

/* Audio: the emulation produces samples into an internal ring; the frontend
 * pulls them from its audio callback. Returns the number of stereo frames
 * written. */
int  acore_audio_mix(Acore *a, int16_t *out, int nframes);
int  acore_audio_available(const Acore *a);
/* Fast-forward decimation (keeps the newest window instead of dropping). */
int  acore_audio_drain_latest(Acore *a, int16_t *out, int nframes);

#endif /* ACORE_FRONTEND_H */
