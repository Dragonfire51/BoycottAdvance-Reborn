/*
 * audio_out.c - selectable sound engines (SDL / OpenAL / DirectSound).
 *
 * All three are loaded at run time through LoadLibrary/GetProcAddress: the
 * executable keeps a native import table, and a missing DLL only disables the
 * engine that needs it.  Each engine owns a refill path that calls the caller's
 * fill() with interleaved stereo s16 samples.
 */
#ifdef _WIN32

#include <windows.h>
#include <dsound.h>
#include <stdlib.h>
#include <string.h>
#include "audio_out.h"

static int          g_engine = -1;
static int          g_freq   = 44100;
static int          g_frames = 1024;
static AudioFillFn  g_fill;
static void        *g_ud;
static int          g_open;

/* latency = g_frames * AO_SEGS */
#define AO_SEGS 4

static const char *AO_NAME[3] = { "SDL", "OpenAL", "DirectSound" };
const char *audio_out_engine_name(int e) {
    return (e >= 0 && e < AUDIO_ENGINE_COUNT) ? AO_NAME[e] : "None";
}

int audio_out_engine(void) { return g_engine; }
int audio_out_is_open(void) { return g_open; }

/* ===================================================================== SDL */
/* SDL2 is loaded dynamically, so its ABI is declared here (fixed by SDL 2.x). */
typedef struct {
    int       freq;
    uint16_t  format;
    uint8_t   channels;
    uint8_t   silence;
    uint16_t  samples;
    uint16_t  padding;
    uint32_t  size;
    void    (*callback)(void *userdata, uint8_t *stream, int len);
    void     *userdata;
} SdlAudioSpec;

#define SDL_INIT_AUDIO      0x00000010u
#define SDL_AUDIO_S16SYS    0x8010      /* little-endian host */

typedef int      (*pfn_SDL_InitSubSystem)(uint32_t);
typedef uint32_t (*pfn_SDL_OpenAudioDevice)(const char *, int, const SdlAudioSpec *, SdlAudioSpec *, int);
typedef void     (*pfn_SDL_PauseAudioDevice)(uint32_t, int);
typedef void     (*pfn_SDL_CloseAudioDevice)(uint32_t);

static struct { HMODULE dll; uint32_t dev; } sdl;

static void sdl_callback(void *ud, uint8_t *stream, int len) {
    g_fill((int16_t *)stream, len / 2 / 2, ud);      /* stereo s16 */
}

static int sdl_open(void) {
    sdl.dll = LoadLibraryA("SDL2.dll");
    if (!sdl.dll) return -1;
    pfn_SDL_InitSubSystem    pInit = (pfn_SDL_InitSubSystem)   (void *)GetProcAddress(sdl.dll, "SDL_InitSubSystem");
    pfn_SDL_OpenAudioDevice  pOpen = (pfn_SDL_OpenAudioDevice) (void *)GetProcAddress(sdl.dll, "SDL_OpenAudioDevice");
    pfn_SDL_PauseAudioDevice pPlay = (pfn_SDL_PauseAudioDevice)(void *)GetProcAddress(sdl.dll, "SDL_PauseAudioDevice");
    pfn_SDL_CloseAudioDevice pClos = (pfn_SDL_CloseAudioDevice)(void *)GetProcAddress(sdl.dll, "SDL_CloseAudioDevice");
    if (!pInit || !pOpen || !pPlay || !pClos) { FreeLibrary(sdl.dll); sdl.dll = NULL; return -1; }

    if (pInit(SDL_INIT_AUDIO) < 0) { FreeLibrary(sdl.dll); sdl.dll = NULL; return -1; }
    SdlAudioSpec want, have;
    memset(&want, 0, sizeof want);
    want.freq = g_freq; want.format = SDL_AUDIO_S16SYS; want.channels = 2;
    want.samples = (uint16_t)g_frames; want.callback = sdl_callback; want.userdata = g_ud;
    sdl.dev = pOpen(NULL, 0, &want, &have, 0);
    if (!sdl.dev) { FreeLibrary(sdl.dll); sdl.dll = NULL; return -1; }
    pPlay(sdl.dev, 0);
    return 0;
}
static void sdl_close(void) {
    if (!sdl.dll) return;
    pfn_SDL_CloseAudioDevice pClos = (pfn_SDL_CloseAudioDevice)(void *)GetProcAddress(sdl.dll, "SDL_CloseAudioDevice");
    if (pClos && sdl.dev) pClos(sdl.dev);
    sdl.dev = 0;
    FreeLibrary(sdl.dll); sdl.dll = NULL;
}

/* ================================================================== OpenAL */
typedef struct ALCdevice  ALCdevice;
typedef struct ALCcontext ALCcontext;
typedef struct {
    void  *(*alcOpenDevice)(const char *);
    void  *(*alcCreateContext)(ALCdevice *, const int *);
    int    (*alcMakeContextCurrent)(ALCcontext *);
    void   (*alcDestroyContext)(ALCcontext *);
    void   (*alcCloseDevice)(ALCdevice *);
    void   (*alGenBuffers)(int, unsigned *);
    void   (*alDeleteBuffers)(int, const unsigned *);
    void   (*alGenSources)(int, unsigned *);
    void   (*alDeleteSources)(int, const unsigned *);
    void   (*alSourcei)(unsigned, int, int);
    void   (*alSourcef)(unsigned, int, float);
    void   (*alSourcePlay)(unsigned);
    void   (*alSourceStop)(unsigned);
    void   (*alBufferData)(unsigned, int, const void *, int, int);
    void   (*alSourceQueueBuffers)(unsigned, int, const unsigned *);
    void   (*alSourceUnqueueBuffers)(unsigned, int, unsigned *);
    void   (*alGetSourcei)(unsigned, int, int *);
} AlApi;

#define AL_FORMAT_STEREO16    0x1002
#define AL_BUFFERS_PROCESSED  0x1016
#define AL_SOURCE_STATE       0x1010
#define AL_PLAYING            0x1012

static struct {
    HMODULE dll;
    AlApi   api;
    ALCdevice  *dev;
    ALCcontext *ctx;
    unsigned src;
    unsigned bufs[AO_SEGS];
    HANDLE   thread, stop;
    int16_t *pcm;
} al;

static DWORD WINAPI al_thread(LPVOID p) {
    (void)p;
    for (;;) {
        if (WaitForSingleObject(al.stop, 5) == WAIT_OBJECT_0) break;
        int done = 0;
        al.api.alGetSourcei(al.src, AL_BUFFERS_PROCESSED, &done);
        while (done-- > 0) {
            unsigned b = 0;
            al.api.alSourceUnqueueBuffers(al.src, 1, &b);
            g_fill(al.pcm, g_frames, g_ud);
            al.api.alBufferData(b, AL_FORMAT_STEREO16, al.pcm, g_frames * 4, g_freq);
            al.api.alSourceQueueBuffers(al.src, 1, &b);
        }
        int state = 0;
        al.api.alGetSourcei(al.src, AL_SOURCE_STATE, &state);
        if (state != AL_PLAYING) al.api.alSourcePlay(al.src);
    }
    return 0;
}

static int al_open(void) {
    al.dll = LoadLibraryA("OpenAL32.dll");
    if (!al.dll) return -1;
    #define G(n) do { al.api.n = (void *)GetProcAddress(al.dll, #n); if (!al.api.n) { FreeLibrary(al.dll); al.dll=NULL; return -1; } } while (0)
    G(alcOpenDevice); G(alcCreateContext); G(alcMakeContextCurrent);
    G(alcDestroyContext); G(alcCloseDevice);
    G(alGenBuffers); G(alDeleteBuffers); G(alGenSources); G(alDeleteSources);
    G(alSourcei); G(alSourcef); G(alSourcePlay); G(alSourceStop); G(alBufferData);
    G(alSourceQueueBuffers); G(alSourceUnqueueBuffers); G(alGetSourcei);
    #undef G

    al.dev = (ALCdevice *)al.api.alcOpenDevice(NULL);
    if (!al.dev) { FreeLibrary(al.dll); al.dll = NULL; return -1; }
    al.ctx = (ALCcontext *)al.api.alcCreateContext(al.dev, NULL);
    if (!al.ctx) { al.api.alcCloseDevice(al.dev); FreeLibrary(al.dll); al.dll = NULL; return -1; }
    al.api.alcMakeContextCurrent(al.ctx);

    al.pcm = (int16_t *)malloc((size_t)g_frames * 2 * sizeof(int16_t));
    al.api.alGenSources(1, &al.src);
    al.api.alGenBuffers(AO_SEGS, al.bufs);
    for (int i = 0; i < AO_SEGS; i++) {
        g_fill(al.pcm, g_frames, g_ud);
        al.api.alBufferData(al.bufs[i], AL_FORMAT_STEREO16, al.pcm, g_frames * 4, g_freq);
    }
    al.api.alSourceQueueBuffers(al.src, AO_SEGS, al.bufs);
    al.api.alSourcePlay(al.src);

    al.stop = CreateEventW(NULL, FALSE, FALSE, NULL);
    al.thread = CreateThread(NULL, 0, al_thread, NULL, 0, NULL);
    if (al.thread) SetThreadPriority(al.thread, THREAD_PRIORITY_TIME_CRITICAL);
    return 0;
}
static void al_close(void) {
    if (!al.dll) return;
    if (al.thread) { SetEvent(al.stop); WaitForSingleObject(al.thread, 1000); CloseHandle(al.thread); al.thread = NULL; }
    if (al.stop) { CloseHandle(al.stop); al.stop = NULL; }
    if (al.src) { al.api.alSourceStop(al.src); al.api.alDeleteSources(1, &al.src); al.src = 0; }
    if (al.bufs[0]) { al.api.alDeleteBuffers(AO_SEGS, al.bufs); al.bufs[0] = 0; }
    if (al.ctx) { al.api.alcMakeContextCurrent(NULL); al.api.alcDestroyContext(al.ctx); al.ctx = NULL; }
    if (al.dev) { al.api.alcCloseDevice(al.dev); al.dev = NULL; }
    free(al.pcm); al.pcm = NULL;
    FreeLibrary(al.dll); al.dll = NULL;
}

/* ============================================================== DirectSound */
/* Like SDL and OpenAL, DirectSound is loaded from its DLL at run time, so the
 * executable's import table stays the same native set as before.  Only
 * DirectSoundCreate8 and the two interface GUIDs are needed at link time; the
 * vtable methods are macros from <dsound.h> and cost nothing to call. */
typedef HRESULT (WINAPI *pfn_DirectSoundCreate8)(const GUID *, IDirectSound8 **, IUnknown *);

static const GUID kIID_IDirectSoundBuffer8 =
    { 0x6825A449, 0x7524, 0x4D82, { 0x92, 0x0F, 0x50, 0xE3, 0x6A, 0xB3, 0xAB, 0x1E } };
static const GUID kIID_IDirectSoundNotify =
    { 0xB0210783, 0x89CD, 0x11D0, { 0xAF, 0x08, 0x00, 0xA0, 0xC9, 0x25, 0xCD, 0x16 } };

static void ds_close(void);
/* One looping buffer split into AO_SEGS segments, one notification per segment
 * boundary.  When the play cursor reaches segment i the segment that just
 * finished (i-1) is refilled; it plays again a whole buffer later, so the write
 * never touches the audio being played. */
static struct {
    HMODULE  dll;
    IDirectSound8       *ds;
    IDirectSoundBuffer8 *buf;
    IDirectSoundNotify8 *notify;
    HANDLE   ev[AO_SEGS];
    HANDLE   thread, stop;
    int16_t *pcm;
    DWORD    segbytes;
} ds;

static void ds_fill_seg(int seg) {
    DWORD off = (DWORD)seg * ds.segbytes;
    void *p1, *p2; DWORD l1, l2;
    HRESULT hr = IDirectSoundBuffer8_Lock(ds.buf, off, ds.segbytes, &p1, &l1, &p2, &l2, 0);
    if (hr == DSERR_BUFFERLOST) {
        /* The device was reset (mode change, sleep/resume): DirectSound frees
         * the buffer's memory and Lock fails until Restore re-acquires it.
         * Without this the segment is never refilled again and the stream
         * degrades into clicks.  Standard DirectSound recovery. */
        IDirectSoundBuffer8_Restore(ds.buf);
        hr = IDirectSoundBuffer8_Lock(ds.buf, off, ds.segbytes, &p1, &l1, &p2, &l2, 0);
    }
    if (FAILED(hr)) return;
    g_fill(ds.pcm, g_frames, g_ud);
    memcpy(p1, ds.pcm, l1);
    if (p2) memcpy(p2, (uint8_t *)ds.pcm + l1, l2);
    IDirectSoundBuffer8_Unlock(ds.buf, p1, l1, p2, l2);
}

static DWORD WINAPI ds_thread(LPVOID p) {
    (void)p;
    /* Wait on the four segment events (plus the stop event) instead of polling:
     * a 5 ms poll can fire late under load, the buffer runs dry and the output
     * clicks.  Index 0 is the stop event, so a returned index i>=1 means
     * segment i-1 was reached and the one that just finished (i-2) is refilled. */
    HANDLE h[1 + AO_SEGS];
    h[0] = ds.stop;
    for (int i = 0; i < AO_SEGS; i++) h[i + 1] = ds.ev[i];
    for (;;) {
        DWORD r = WaitForMultipleObjects(1 + AO_SEGS, h, FALSE, INFINITE);
        if (r == WAIT_OBJECT_0) break;
        if (r > WAIT_OBJECT_0 && r < WAIT_OBJECT_0 + 1 + AO_SEGS) {
            int finished = (int)(r - WAIT_OBJECT_0 - 1 + AO_SEGS - 1) % AO_SEGS;
            ds_fill_seg(finished);
        }
    }
    return 0;
}

static int ds_open(void) {
    ds.dll = LoadLibraryA("DSOUND.dll");
    if (!ds.dll) return -1;
    pfn_DirectSoundCreate8 pCreate = (pfn_DirectSoundCreate8)(void *)GetProcAddress(ds.dll, "DirectSoundCreate8");
    if (!pCreate || FAILED(pCreate(NULL, &ds.ds, NULL))) goto fail;
    if (FAILED(IDirectSound8_SetCooperativeLevel(ds.ds, GetDesktopWindow(), DSSCL_PRIORITY))) goto fail;
    WAVEFORMATEX wf;
    memset(&wf, 0, sizeof wf);
    wf.wFormatTag = WAVE_FORMAT_PCM; wf.nChannels = 2; wf.nSamplesPerSec = (DWORD)g_freq;
    wf.wBitsPerSample = 16; wf.nBlockAlign = 4; wf.nAvgBytesPerSec = (DWORD)g_freq * 4;

    ds.segbytes = (DWORD)g_frames * 4;
    DSBUFFERDESC d = {0};
    d.dwSize = sizeof d;
    d.dwFlags = DSBCAPS_CTRLPOSITIONNOTIFY | DSBCAPS_GLOBALFOCUS | DSBCAPS_GETCURRENTPOSITION2;
    d.dwBufferBytes = ds.segbytes * AO_SEGS;
    d.lpwfxFormat = &wf;
    IDirectSoundBuffer *base = NULL;
    if (FAILED(IDirectSound8_CreateSoundBuffer(ds.ds, &d, &base, NULL))) goto fail;
    if (FAILED(IDirectSoundBuffer_QueryInterface(base, &kIID_IDirectSoundBuffer8, (void **)&ds.buf))) {
        IDirectSoundBuffer_Release(base); goto fail;
    }
    IDirectSoundBuffer_Release(base);
    if (FAILED(IDirectSoundBuffer8_QueryInterface(ds.buf, &kIID_IDirectSoundNotify, (void **)&ds.notify))) goto fail;

    DSBPOSITIONNOTIFY pn[AO_SEGS];
    for (int i = 0; i < AO_SEGS; i++) {
        ds.ev[i] = CreateEventW(NULL, FALSE, FALSE, NULL);
        pn[i].dwOffset = ds.segbytes * (DWORD)i;
        pn[i].hEventNotify = ds.ev[i];
    }
    if (FAILED(IDirectSoundNotify_SetNotificationPositions(ds.notify, AO_SEGS, pn))) goto fail;

    ds.pcm = (int16_t *)malloc((size_t)g_frames * 2 * sizeof(int16_t));
    for (int i = 0; i < AO_SEGS; i++) ds_fill_seg(i);
    IDirectSoundBuffer8_Play(ds.buf, 0, 0, DSBPLAY_LOOPING);

    ds.stop = CreateEventW(NULL, FALSE, FALSE, NULL);
    ds.thread = CreateThread(NULL, 0, ds_thread, NULL, 0, NULL);
    /* Same as the waveOut refill thread: without a raised priority the segment
     * refill can miss its notification while the CPU is busy, the buffer runs
     * dry and the output repeats the last sample (a click). */
    if (ds.thread) SetThreadPriority(ds.thread, THREAD_PRIORITY_TIME_CRITICAL);
    return 0;
fail:
    ds_close();
    return -1;
}
static void ds_close(void) {
    if (ds.thread) { SetEvent(ds.stop); WaitForSingleObject(ds.thread, 1000); CloseHandle(ds.thread); ds.thread = NULL; }
    if (ds.stop) { CloseHandle(ds.stop); ds.stop = NULL; }
    if (ds.buf) { IDirectSoundBuffer8_Stop(ds.buf); IDirectSoundBuffer8_Release(ds.buf); ds.buf = NULL; }
    if (ds.notify) { IDirectSoundNotify_Release(ds.notify); ds.notify = NULL; }
    for (int i = 0; i < AO_SEGS; i++)
        if (ds.ev[i]) { CloseHandle(ds.ev[i]); ds.ev[i] = NULL; }
    if (ds.ds) { IDirectSound8_Release(ds.ds); ds.ds = NULL; }
    free(ds.pcm); ds.pcm = NULL;
    if (ds.dll) { FreeLibrary(ds.dll); ds.dll = NULL; }
}

/* ================================================================= dispatch */
int audio_out_open(int engine, int freq, int frames, AudioFillFn fill, void *ud) {
    audio_out_close();
    if (engine < 0 || engine >= AUDIO_ENGINE_COUNT || !fill) return -1;
    g_engine = engine; g_freq = freq; g_frames = frames; g_fill = fill; g_ud = ud;

    int rc = -1;
    switch (engine) {
    case AUDIO_ENGINE_SDL:    rc = sdl_open(); break;
    case AUDIO_ENGINE_OPENAL: rc = al_open();  break;
    case AUDIO_ENGINE_DSOUND: rc = ds_open();  break;
    }
    if (rc != 0) { g_engine = -1; return -1; }
    g_open = 1;
    return 0;
}

void audio_out_close(void) {
    if (g_engine < 0) return;
    switch (g_engine) {
    case AUDIO_ENGINE_SDL:    sdl_close(); break;
    case AUDIO_ENGINE_OPENAL: al_close();  break;
    case AUDIO_ENGINE_DSOUND: ds_close();  break;
    }
    g_open = 0; g_engine = -1;
}

#endif /* _WIN32 */
