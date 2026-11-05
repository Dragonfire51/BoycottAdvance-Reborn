/*
 * audio_out.h - selectable sound engines for the native Win32 build
 * (Options -> Sound Engine -> SDL / OpenAL / DirectSound).
 *
 * Every backend is loaded at run time with LoadLibrary/GetProcAddress, so the
 * executable's import table stays native (no link-time SDL2 or OpenAL): the
 * DLLs are only required if the user actually selects that engine.
 */
#ifndef AUDIO_OUT_H
#define AUDIO_OUT_H

#include <stdint.h>

enum {
    AUDIO_ENGINE_SDL = 0,
    AUDIO_ENGINE_OPENAL = 1,
    AUDIO_ENGINE_DSOUND = 2,
    AUDIO_ENGINE_COUNT = 3
};

/* Pull callback: write `frames` stereo s16 samples (2*frames int16) to dst. */
typedef void (*AudioFillFn)(int16_t *dst, int frames, void *ud);

/* Open `engine` at `freq` Hz, stereo, refilling in blocks of `frames` frames.
 * Returns 0 on success, -1 if the engine (or its DLL) is unavailable. */
int  audio_out_open(int engine, int freq, int frames, AudioFillFn fill, void *ud);
void audio_out_close(void);
int  audio_out_is_open(void);              /* a device is playing */
int  audio_out_engine(void);               /* engine actually in use, or -1 */
const char *audio_out_engine_name(int engine);

#endif
