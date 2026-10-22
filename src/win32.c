/*
 * win32.c - native Win32 frontend (x64/x86), no SDL.
 *
 * BoycottAdvance Reborn on Windows uses only the APIs the original
 * BoycottAdvance used: a real Win32 window + menu bar (GDI), DirectSound
 * via the mm waveOut API, and WinMM joystick polling. This file owns the
 * HWND, the message loop, the audio stream and the frame loop; emulation
 * (gba.c), rendering (video.c GDI/D3D9) and the menus (gui.c) are shared
 * pure-C modules.
 *
 * The legacy SDL2 frontend (main.c) continues to build for non-Windows
 * platforms; this translation unit is compiled only for _WIN32.
 */
#ifdef _WIN32

#define _WIN32_WINNT 0x0600
#define _DEFAULT_SOURCE
#include "gba.h"
#include "fb.h"
#include "video.h"
#include "icon.h"
#include "gui.h"
#include "uiactions.h"
#include <windows.h>
#include <mmsystem.h>
#include <commdlg.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

/* Keep the same keypad layout / defaults as the SDL frontend. */
enum { KP_A, KP_B, KP_SELECT, KP_START, KP_RIGHT, KP_LEFT, KP_UP, KP_DOWN, KP_R, KP_L, KP_COUNT };
static const char *KP_NAME[KP_COUNT] = {
    "A", "B", "Select", "Start", "Right", "Left", "Up", "Down", "R", "L" };
/* Default virtual-key code per keypad button. */
static const UINT KP_DEFAULT_VK[KP_COUNT] = {
    'A', 'S', VK_BACK, VK_RETURN,
    VK_RIGHT, VK_LEFT, VK_UP, VK_DOWN,
    'D', 'F' };
/* Default joystick assignments: face buttons A/B/Select/Start, POV = D-pad,
 * R/L on buttons 4/5. */
static const int JOY_DEFAULT[KP_COUNT] = {
    0, 1, 2, 3,
    JOY_POV_RIGHT, JOY_POV_LEFT, JOY_POV_UP, JOY_POV_DOWN,
    4, 5 };

/* ------------------------------------------------------------------ config */
typedef struct {
    int  zoom, scalemode, scanline, smooth, autothrottle;
    int  showfps, showfps_pct, frameskip, autoframeskip, sound_enable;
    int  dsound_a, dsound_b, gbc_sound, reverse_stereo;
    int  biosenable, biosfastboot, skipintros, autorun, flashid, state_slot;
    int  video_backend;
    int  keymap[KP_COUNT];        /* VK code per keypad button */
    int  joymap[KP_COUNT];        /* joystick: button or JOY_POV_* */
    char lastdir[512], rompath[512];
} Config;
static Config cfg;
static GBA   *g;
static int    g_paused;

/* GBA frame rate: 16.777216 MHz / (228 lines * 1232 cycles) = 59.7275 Hz. */
#define GBA_FRAME_MS 16.7427

/* ------------------------------------------------------------------ window */
static HWND        g_hwnd;
static HINSTANCE   g_inst;
static int         g_client_w, g_client_h;   /* last WM_SIZE */
static int         g_fs;                     /* fullscreen mode */
static DWORD       g_fs_style, g_fs_ex;
static RECT        g_fs_rect;
static volatile int g_quit;

/* ------------------------------------------------------------------ audio */
/* waveOut playback of the stream the APU mixer produces. A short ringing
 * buffer keeps the callback latency reasonable without dropping beats. */
#define AUDIO_FREQ 44100
#define AUDIO_CHANNELS 2
#define AUDIO_SAMPLES 1024          /* per fragment */
#define AUDIO_FRAGS   6

typedef struct {
    HWAVEOUT  hwo;
    WAVEHDR   hdr[AUDIO_FRAGS];
    int16_t  *buf[AUDIO_FRAGS];
    int       cur;
    volatile int closing;   /* set before shutdown so the callback stops requeueing */
} Audio;
static Audio g_audio;

static void CALLBACK wave_proc(HWAVEOUT hwo, UINT uMsg, DWORD_PTR inst,
                               DWORD_PTR p1, DWORD_PTR p2) {
    (void)hwo; (void)p1; (void)p2;
    if (uMsg != WOM_DONE) return;
    Audio *a = (Audio *)inst;
    /* During shutdown the buffers must not be re-queued, otherwise
       waveOutReset can never drain and waveOutClose deadlocks. */
    if (a->closing) return;
    /* Mix in fresh audio for the finished fragment. */
    if (cfg.sound_enable && g) {
        apu_mix(g, a->buf[a->cur], AUDIO_SAMPLES);
        if (cfg.reverse_stereo)
            for (int i = 0; i < AUDIO_SAMPLES; i++) {
                int16_t l = a->buf[a->cur][i*2];
                a->buf[a->cur][i*2] = a->buf[a->cur][i*2+1];
                a->buf[a->cur][i*2+1] = l;
            }
    } else {
        memset(a->buf[a->cur], 0, (size_t)AUDIO_SAMPLES * 2 * sizeof(int16_t));
    }
    waveOutWrite(hwo, &a->hdr[a->cur], sizeof(WAVEHDR));
    a->cur = (a->cur + 1) % AUDIO_FRAGS;
}

static int audio_open(void) {
    WAVEFORMATEX wf;
    memset(&wf, 0, sizeof wf);
    wf.wFormatTag = WAVE_FORMAT_PCM;
    wf.nChannels = AUDIO_CHANNELS;
    wf.nSamplesPerSec = AUDIO_FREQ;
    wf.wBitsPerSample = 16;
    wf.nBlockAlign = (WORD)(wf.nChannels * wf.wBitsPerSample / 8);
    wf.nAvgBytesPerSec = wf.nSamplesPerSec * wf.nBlockAlign;

    memset(&g_audio, 0, sizeof g_audio);
    if (waveOutOpen(&g_audio.hwo, WAVE_MAPPER, &wf, (DWORD_PTR)wave_proc,
                    (DWORD_PTR)&g_audio, CALLBACK_FUNCTION) != MMSYSERR_NOERROR)
        return -1;
    for (int i = 0; i < AUDIO_FRAGS; i++) {
        g_audio.buf[i] = (int16_t *)calloc((size_t)AUDIO_SAMPLES * AUDIO_CHANNELS, sizeof(int16_t));
        g_audio.hdr[i].lpData = (LPSTR)g_audio.buf[i];
        g_audio.hdr[i].dwBufferLength = AUDIO_SAMPLES * AUDIO_CHANNELS * sizeof(int16_t);
        waveOutPrepareHeader(g_audio.hwo, &g_audio.hdr[i], sizeof(WAVEHDR));
        waveOutWrite(g_audio.hwo, &g_audio.hdr[i], sizeof(WAVEHDR));
    }
    return 0;
}
static void audio_close(void) {
    if (!g_audio.hwo) return;
    /* Stop the callback from re-queueing before resetting, or waveOutReset
       never drains and waveOutClose blocks forever. */
    g_audio.closing = 1;
    waveOutReset(g_audio.hwo);
    for (int i = 0; i < AUDIO_FRAGS; i++) {
        if (g_audio.hdr[i].dwFlags & WHDR_PREPARED)
            waveOutUnprepareHeader(g_audio.hwo, &g_audio.hdr[i], sizeof(WAVEHDR));
        free(g_audio.buf[i]);
        g_audio.buf[i] = NULL;
    }
    waveOutClose(g_audio.hwo);
    g_audio.hwo = NULL;
}

/* ------------------------------------------------------------------ keypad */
static uint16_t key_state(void) {
    uint16_t k = 0x03FF;
    for (int i = 0; i < KP_COUNT; i++) {
        int vk = cfg.keymap[i];
        if (vk >= 0 && (GetAsyncKeyState(vk) & 0x8000))
            k &= (uint16_t)~(1u << i);
    }
    k &= ctl_joy_poll(cfg.joymap);
    return k;
}

/* ------------------------------------------------------------------ .ini */
/* The config lives next to the executable, as the original did. Writing to
 * a bare relative name would drop it in the process working directory, which
 * for a shortcut or Start-menu launch is somewhere the user cannot find. If
 * the install directory is read-only (Program Files) we fall back to the
 * working directory so the settings still persist. */
static const char *ini_cwd_path(void) { return "boycottadvance-reborn.ini"; }
static const char *ini_path(void) {
    static char path[MAX_PATH];
    static int resolved = 0;
    if (!resolved) {
        char exe[MAX_PATH];
        DWORD n = GetModuleFileNameA(NULL, exe, sizeof exe);
        resolved = 1;
        if (n > 0 && n < sizeof exe) {
            char *slash = strrchr(exe, '\\');
            if (slash) slash[1] = 0; else exe[0] = 0;
            snprintf(path, sizeof path, "%sboycottadvance-reborn.ini", exe);
        } else {
            snprintf(path, sizeof path, "%s", ini_cwd_path());
        }
    }
    return path;
}
static void ini_get(const char *path, const char *key, char *out, size_t n) {
    FILE *f = fopen(path, "rt"); out[0] = 0;
    if (!f) return;
    char line[512]; size_t klen = strlen(key);
    while (fgets(line, sizeof line, f)) {
        char *p = line;
        while (*p==' '||*p=='\t') p++;
        if (strncmp(p, key, klen)==0) {
            /* The key must be followed by a delimiter, otherwise a shorter
             * key would match a longer one (Key_R vs Key_Right). */
            char after = p[klen];
            if (after!=' ' && after!='\t' && after!='=' && after!='\r' &&
                after!='\n' && after!='\0') continue;
            p += klen; while (*p==' '||*p=='\t'||*p=='=') p++;
            char *v = p; while (*v && *v!='\r' && *v!='\n') v++; *v=0;
            snprintf(out, n, "%s", p); break;
        }
    }
    fclose(f);
}
static int ini_geti(const char *p, const char *k, int d) {
    char b[64]; ini_get(p, k, b, sizeof b); return b[0] ? atoi(b) : d;
}
static void cfg_load(const char *path) {
    memset(&cfg, 0, sizeof cfg);
    cfg.zoom         = ini_geti(path, "ScreenZoom", 3);
    cfg.scanline     = ini_geti(path, "Scanline", 0);
    cfg.autothrottle = ini_geti(path, "AutoThrottle", 1);
    cfg.showfps      = ini_geti(path, "ShowFPS", 1);
    cfg.showfps_pct  = ini_geti(path, "ShowFPSPercent", 0);
    cfg.frameskip    = ini_geti(path, "FrameSkip", 0);
    cfg.autoframeskip= ini_geti(path, "AutoFrameSkip", 0);
    cfg.sound_enable = ini_geti(path, "SoundEnable", 1);
    cfg.gbc_sound    = ini_geti(path, "GameboySoundEnable", 1);
    cfg.dsound_a     = ini_geti(path, "DirectSoundAEnable", 1);
    cfg.dsound_b     = ini_geti(path, "DirectSoundBEnable", 1);
    cfg.biosenable   = ini_geti(path, "BIOSEnable", 1);
    cfg.biosfastboot = ini_geti(path, "BIOSFastBoot", 1);
    cfg.skipintros   = ini_geti(path, "SkipIntros", 0);
    cfg.autorun      = ini_geti(path, "AutoRun", 1);
    cfg.flashid      = ini_geti(path, "FlashROMId", 0);
    cfg.state_slot   = ini_geti(path, "StateSlot", 0);
    if (cfg.state_slot < 0 || cfg.state_slot >= M_SLOT_COUNT) cfg.state_slot = 0;
    cfg.video_backend= ini_geti(path, "VideoBackend", VID_GDI);
    cfg.smooth       = ini_geti(path, "SmoothEmulation", 0);
    cfg.scalemode    = ini_geti(path, "ScaleMode", 0);
    cfg.reverse_stereo= ini_geti(path, "ReverseStereo", 0);
    ini_get(path, "RomPath", cfg.lastdir, sizeof cfg.lastdir);
    if (!cfg.lastdir[0]) snprintf(cfg.lastdir, sizeof cfg.lastdir, ".");
    for (int i = 0; i < KP_COUNT; i++) {
        char key[32]; snprintf(key, sizeof key, "Key_%s", KP_NAME[i]);
        cfg.keymap[i] = (int)ini_geti(path, key, (int)KP_DEFAULT_VK[i]);
    }
    for (int i = 0; i < KP_COUNT; i++) {
        char key[32]; snprintf(key, sizeof key, "JoyGBA_%s", KP_NAME[i]);
        cfg.joymap[i] = ini_geti(path, key, JOY_DEFAULT[i]);
    }
}
/* Write the config atomically: a temp file followed by a replace, so an
 * interrupted or failed write cannot truncate a good config (the reference
 * frontend does the same with its prefs). */
static int cfg_save(const char *path) {
    char tmp[MAX_PATH + 8];
    if ((size_t)snprintf(tmp, sizeof tmp, "%s.tmp", path) >= sizeof tmp) return 0;
    FILE *f = fopen(tmp, "wt"); if (!f) return 0;
    int ok = 1;
    if (fprintf(f,
      "ScreenZoom %d\nScanline %d\nAutoThrottle %d\nShowFPS %d\nShowFPSPercent %d\n"
      "FrameSkip %d\nAutoFrameSkip %d\nSoundEnable %d\nGameboySoundEnable %d\n"
      "DirectSoundAEnable %d\nDirectSoundBEnable %d\nBIOSEnable %d\nBIOSFastBoot %d\n"
      "SkipIntros %d\nAutoRun %d\nFlashROMId %d\nStateSlot %d\nVideoBackend %d\n"
      "SmoothEmulation %d\nScaleMode %d\nReverseStereo %d\nRomPath %s\n",
      cfg.zoom, cfg.scanline, cfg.autothrottle, cfg.showfps, cfg.showfps_pct,
      cfg.frameskip, cfg.autoframeskip, cfg.sound_enable, cfg.gbc_sound,
      cfg.dsound_a, cfg.dsound_b, cfg.biosenable, cfg.biosfastboot,
      cfg.skipintros, cfg.autorun, cfg.flashid, cfg.state_slot,
      cfg.video_backend, cfg.smooth, cfg.scalemode, cfg.reverse_stereo,
      cfg.lastdir) < 0) ok = 0;
    for (int i = 0; i < KP_COUNT; i++)
        if (fprintf(f, "Key_%s %d\n", KP_NAME[i], cfg.keymap[i]) < 0) ok = 0;
    for (int i = 0; i < KP_COUNT; i++)
        if (fprintf(f, "JoyGBA_%s %d\n", KP_NAME[i], cfg.joymap[i]) < 0) ok = 0;
    if (fclose(f) != 0) ok = 0;
    if (!ok || !MoveFileExA(tmp, path,
                            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileA(tmp);
        return 0;
    }
    return 1;
}
/* Persist immediately whenever a setting changes, so a setting is never lost
 * if the emulator is killed (the real BIOS can spin, and a force-close must
 * not throw away the user's choices). Falls back to the working directory
 * when the install directory is not writable. */
static void save_cfg(void) {
    if (cfg_save(ini_path())) return;
    if (strcmp(ini_path(), ini_cwd_path()) != 0) cfg_save(ini_cwd_path());
}

/* Load from next to the executable; if that file does not exist, accept a
 * config left in the working directory by an older build. */
static void cfg_load_best(void) {
    cfg_load(ini_path());
    if (strcmp(ini_path(), ini_cwd_path()) != 0) {
        FILE *probe = fopen(ini_path(), "rt");
        if (!probe) {
            FILE *old = fopen(ini_cwd_path(), "rt");
            if (old) { fclose(old); cfg_load(ini_cwd_path()); }
        } else fclose(probe);
    }
}

/* ------------------------------------------------------------------ messages */
static void msgbox(const char *t, const char *b) { gui_native_message(t, b); }

static void load_rom(const char *path) {
    if (gba_load_rom(g, path)) {
        snprintf(cfg.rompath, sizeof cfg.rompath, "%s", path);
        const char *sl = strrchr(path,'/'); if (!sl) sl = strrchr(path,'\\');
        if (sl) { size_t n = sl-path; if (n<sizeof cfg.lastdir) { memcpy(cfg.lastdir,path,n); cfg.lastdir[n]=0; } }
        gba_reset(g); g_paused = 0;
    } else msgbox("Error", "Cannot load ROM file.");
}

static void state_path(char *b, size_t n) { snprintf(b,n,"state%d.sav",cfg.state_slot); }
static const char *save_type_name(int t) {
    switch (t) {
    case SAVE_SRAM:     return "SRAM 32KB";
    case SAVE_FLASH64:  return "Flash 64KB";
    case SAVE_FLASH128: return "Flash 128KB";
    case SAVE_EEPROM:   return "EEPROM";
    default:            return "none";
    }
}

static void show_rom_info(void) {
    char b[1024];
    snprintf(b,sizeof b,
        "FileName : %s\nTitle    : %.16s\nSize     : %u bytes (%u KB)\n"
        "Save type: %s\nBackup size: %u bytes",
        cfg.rompath[0]?cfg.rompath:"(none)", g->rom_title, g->rom_size,
        g->rom_size/1024, save_type_name(g->save_type), g->save_size);
    msgbox("ROM Info", b);
}
static void show_about(void) {
    msgbox("About",
      GBA_TITLE "\nClean-room reconstruction of BoycottAdvance 0.2.8\n"
      "with a complete ARM7TDMI core and BIOS SWI emulation.\n"
      "Windows build: native GDI/Direct3D9, waveOut audio, no SDL.\n\n"
      "Original authors:\n"
      "  Julien Frelat \"Gollum\" (main emulation, GBA core, GUI,\n"
      "  Win32 port, documentation)\n"
      "  Contributors: Anarko, Fitzy, Dysfunction, Perfect Dark,\n"
      "  SMY, Niels Wagenaar, Forgotten, Champipi\n\n"
      "Original BoycottAdvance was cardware: if you liked it,\n"
      "please send a postcard. Keep crediting the authors.");
}
static void show_history(void) {
    /* Show only the changes of the current release, as the user expects. */
    static const char *hist =
      GBA_TITLE "\n\n"
      "What's new in " GBA_VERSION ":\n"
      "  - Core: games no longer run too fast. The BIOS SWIs\n"
      "    (decompression, CpuSet, Div) and the DMA channels did not\n"
      "    charge CPU cycles, so loaders that copy data finished in a\n"
      "    handful of frames (Medabots AX entered gameplay on frame 18;\n"
      "    mGBA takes 979). SWI and DMA cycles are now accounted as\n"
      "    GBATEK describes, and loading matches mGBA within 1%.\n"
      "  - Core: IntrWait/VBlankIntrWait only wake on the awaited\n"
      "    interrupt. Unrelated IRQs no longer interrupt the BIOS wait\n"
      "    loop, keeping the perceived clock in step with real hardware.";
    msgbox("History", hist);
}
static void reset_keys(void) {
    memcpy(cfg.keymap, KP_DEFAULT_VK, sizeof cfg.keymap);
}
static void reset_joystick(void) {
    memcpy(cfg.joymap, JOY_DEFAULT, sizeof cfg.joymap);
}

/* Human-readable names shown in the Controls menu. */
static const char *vk_name(int vk) {
    static char buf[24];
    switch (vk) {
    case VK_RETURN: return "Enter";
    case VK_BACK:   return "Backspace";
    case VK_TAB:    return "Tab";
    case VK_SPACE:  return "Space";
    case VK_ESCAPE: return "Esc";
    case VK_LEFT:   return "Left";
    case VK_RIGHT:  return "Right";
    case VK_UP:     return "Up";
    case VK_DOWN:   return "Down";
    case VK_SHIFT:  return "Shift";
    case VK_CONTROL:return "Ctrl";
    case VK_MENU:   return "Alt";
    }
    if (vk >= VK_F1 && vk <= VK_F12) { snprintf(buf, sizeof buf, "F%d", vk - VK_F1 + 1); return buf; }
    if (vk >= 0x20 && vk < 0x7F) { buf[0] = (char)vk; buf[1] = 0; return buf; }
    snprintf(buf, sizeof buf, "VK %d", vk);
    return buf;
}
static const char *joy_name(int kp) {
    static char buf[24];
    int v = cfg.joymap[kp];
    switch (v) {
    case JOY_POV_UP:    return "POV Up";
    case JOY_POV_RIGHT: return "POV Right";
    case JOY_POV_DOWN:  return "POV Down";
    case JOY_POV_LEFT:  return "POV Left";
    }
    snprintf(buf, sizeof buf, "Button %d", v);
    return buf;
}
static void toggle_fullscreen(void) {
    g_fs = !g_fs;
    if (g_fs) {
        GetWindowRect(g_hwnd, &g_fs_rect);
        g_fs_style = GetWindowLongPtr(g_hwnd, GWL_STYLE);
        g_fs_ex    = GetWindowLongPtr(g_hwnd, GWL_EXSTYLE);
        SetWindowLongPtr(g_hwnd, GWL_STYLE, g_fs_style & ~(WS_CAPTION|WS_THICKFRAME));
        SetWindowLongPtr(g_hwnd, GWL_EXSTYLE, g_fs_ex & ~(WS_EX_DLGMODALFRAME|WS_EX_CLIENTEDGE|WS_EX_STATICEDGE));
        SetWindowPos(g_hwnd, HWND_TOP, 0, 0,
            GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN),
            SWP_FRAMECHANGED|SWP_SHOWWINDOW);
    } else {
        SetWindowLongPtr(g_hwnd, GWL_STYLE, g_fs_style);
        SetWindowLongPtr(g_hwnd, GWL_EXSTYLE, g_fs_ex);
        SetWindowPos(g_hwnd, HWND_TOP, g_fs_rect.left, g_fs_rect.top,
            g_fs_rect.right-g_fs_rect.left, g_fs_rect.bottom-g_fs_rect.top,
            SWP_FRAMECHANGED|SWP_SHOWWINDOW);
    }
}
static void save_screenshot(void) {
    if (!g || g->rom_size == 0) return;
    FILE *f = fopen("shot.ppm","wb");
    if (!f) return;
    fwrite("P6\n240 160\n255\n",1,14,f);
    for (int y2=0;y2<SCREEN_H;y2++) for (int x2=0;x2<SCREEN_W;x2++) {
        uint16_t c = g->ppu.frame[y2*SCREEN_W+x2];
        fputc((c&0x1F)<<3,f); fputc(((c>>5)&0x1F)<<3,f); fputc(((c>>10)&0x1F)<<3,f);
    }
    fclose(f);
}

/* ------------------------------------------------------------------ actions */
static void do_action(int id);
static int  item_checked(int id);
static const char *vk_name(int vk);
static const char *joy_name(int kp);
static void gui_cb_action(int id)   { do_action(id); save_cfg(); }
static int  gui_cb_checked(int id)  { return item_checked(id); }
static void gui_cb_loadrom(const char *path) { load_rom(path); }
static void gui_cb_setbios(const char *path) { gba_load_bios(g, path); save_cfg(); }
static void gui_cb_pause(void)  { /* waveOut flows continuously; no need */ }
static void gui_cb_resume(void) { }
static const char *gui_cb_keyname(int kp) { return vk_name(cfg.keymap[kp]); }
static const char *gui_cb_joyname(int kp) { return joy_name(kp); }
static int  gui_cb_keyget(int kp) { return cfg.keymap[kp]; }
static void gui_cb_keyset(int kp, int vk) { cfg.keymap[kp] = vk; save_cfg(); }
static int  gui_cb_joyget(int kp) { return cfg.joymap[kp]; }
static void gui_cb_joyset(int kp, int v) { cfg.joymap[kp] = v; save_cfg(); }

/* ------------------------------------------------------------------ key capture */
/* The menu item arms a capture and returns; the next key or joystick event
 * on the emulator window is bound. Capture is deliberately non-modal so the
 * window keeps receiving input (a modal message box would swallow it). */
static int key_capture = -1;
static int joy_capture = -1;

static void capture_key(int kp) {
    key_capture = kp;
    joy_capture = -1;
    SetWindowTextA(g_hwnd, "BoycottAdvance Reborn  -  press a key to map (ESC cancels)");
}
static void on_key_assign(int vk) {
    if (key_capture < 0) return;
    if (vk == VK_ESCAPE) { key_capture = -1; SetWindowTextA(g_hwnd, GBA_TITLE); return; }
    cfg.keymap[key_capture] = vk;
    key_capture = -1;
    SetWindowTextA(g_hwnd, GBA_TITLE);
    gui_native_sync();
    save_cfg();
}
static void capture_joy(int kp) {
    joy_capture = kp;
    key_capture = -1;
    SetWindowTextA(g_hwnd, "BoycottAdvance Reborn  -  press a joystick button to map (ESC cancels)");
}
static void on_joy_assign(int value) {
    if (joy_capture < 0) return;
    cfg.joymap[joy_capture] = value;
    joy_capture = -1;
    SetWindowTextA(g_hwnd, GBA_TITLE);
    gui_native_sync();
    save_cfg();
}
/* Called every frame while a joystick capture is armed: bind the first
 * newly pressed button, or the POV direction if the hat is moved. */
static void poll_joy_capture(void) {
    if (joy_capture < 0) return;
    static int prev_buttons = 0;
    int buttons = 0, pov = 0;
    if (!ctl_joy_raw(&buttons, &pov)) return;
    if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) { joy_capture = -1; SetWindowTextA(g_hwnd, GBA_TITLE); return; }
    int newly = buttons & ~prev_buttons;
    prev_buttons = buttons;
    if (newly) {
        int b = 0; while (b < JOY_MAX_BUTTONS && !(newly & (1 << b))) b++;
        on_joy_assign(b);
    } else if (pov) {
        on_joy_assign(pov);
    }
}

/* Resize the window so the *client area* is exactly SCREEN_W x SCREEN_H times
 * the zoom. SetWindowPos takes window dimensions: sizing it with the bare
 * client size crops the picture by the frame/caption width. */
static void resize_for_zoom(void) {
    if (g_fs) return;                       /* fullscreen: client = screen */
    RECT r = { 0, 0, SCREEN_W * cfg.zoom, SCREEN_H * cfg.zoom };
    AdjustWindowRectEx(&r, (DWORD)GetWindowLongPtrA(g_hwnd, GWL_STYLE), TRUE,
                       (DWORD)GetWindowLongPtrA(g_hwnd, GWL_EXSTYLE));
    SetWindowPos(g_hwnd, NULL, 0, 0, r.right - r.left, r.bottom - r.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

static void do_action(int id) {
    switch (id) {
    case M_LOADROM:  gui_native_open_rom(); break;
    case M_ROMINFO:  show_rom_info(); break;
    case M_AUTORUN:  cfg.autorun = !cfg.autorun; break;
    case M_RUNPAUSE: g_paused = !g_paused; break;
    case M_RESET:    gba_save_battery(g, cfg.rompath); gba_reset(g); g_paused = 0; break;
    case M_STATE_SLOT: cfg.state_slot = (cfg.state_slot+1)%M_SLOT_COUNT; break;
    case M_LOADSTATE: { char p[64]; state_path(p,sizeof p); gba_load_state(g,p); break; }
    case M_SAVESTATE:{ char p[64]; state_path(p,sizeof p); gba_save_state(g,p); break; }
    case M_EXIT:     g_quit = 1; PostMessage(g_hwnd, WM_CLOSE, 0, 0); break;
    case M_ZOOM1: case M_ZOOM2: case M_ZOOM3: case M_ZOOM4:
        cfg.zoom = id-M_ZOOM1+1; cfg.scalemode = 0;
        resize_for_zoom();
        break;
    case M_FLASHID:  cfg.flashid = (cfg.flashid+1)%3; break;
    case M_SCANLINE: cfg.scanline = !cfg.scanline; break;
    case M_FIT:      cfg.scalemode = (cfg.scalemode==1)?0:1; break;
    case M_FULLSCREEN: toggle_fullscreen(); cfg.scalemode = 2; break;
    case M_SKIP0: case M_SKIP1: case M_SKIP2: case M_SKIP3: case M_SKIP4: case M_SKIP5:
        cfg.frameskip = id-M_SKIP0; g->frameskip = cfg.frameskip; break;
    case M_AUTOSKIP: cfg.autoframeskip = !cfg.autoframeskip; break;
    case M_SMOOTH:   cfg.smooth = !cfg.smooth; break;
    case M_THROTTLE: cfg.autothrottle = !cfg.autothrottle; break;
    case M_SHOWFPS:  cfg.showfps = !cfg.showfps; break;
    case M_SHOWFPSPCT: cfg.showfps_pct = !cfg.showfps_pct; break;
    case M_SOUND:    cfg.sound_enable = !cfg.sound_enable; break;
    case M_DSOUNDA:  cfg.dsound_a = !cfg.dsound_a; break;
    case M_DSOUNDB:  cfg.dsound_b = !cfg.dsound_b; break;
    case M_GBCSOUND: cfg.gbc_sound = !cfg.gbc_sound; break;
    case M_REVSTEREO: cfg.reverse_stereo = !cfg.reverse_stereo; break;
    case M_SELBIOS:  gui_native_open_bios(); break;
    case M_BIOSEN:
        cfg.biosenable = !cfg.biosenable; g->bios_enabled = cfg.biosenable;
        if (g->rom_size) gba_reset(g);
        break;
    case M_BIOSBOOT:
        cfg.biosfastboot = !cfg.biosfastboot; g->bios_fastboot = cfg.biosfastboot;
        if (g->rom_size) gba_reset(g);
        break;
    case M_CONTROLS: reset_keys(); break;
    case M_RESETJOY: reset_joystick(); break;
    case M_INPUTCFG: gui_native_input_config(); break;
    case M_SKIPINTROS:
        cfg.skipintros = !cfg.skipintros; g->skip_intros = cfg.skipintros;
        if (g->rom_size) gba_reset(g);
        break;
    case M_HISTORY:  show_history(); break;
    case M_CMDLINE:
        msgbox("BoycottAdvance Command Line Options",
            "Usage: boycottadvance-reborn [options] [romfile]\n\n"
            "-help        Display this help\n-autosave    Save INI file on exit\n"
            "-sound       Enable sound\n-bios        Enable BIOS\n-fastboot    Skip BIOS boot");
        break;
    case M_ABOUT:    show_about(); break;
    case M_VB_AUTO: case M_VB_SOFT: case M_VB_GL: case M_VB_VK: case M_VB_GDI: case M_VB_D3D: {
        static const int map[6] = { VID_GDI, VID_GDI, VID_GDI, VID_GDI,
                                    VID_GDI, VID_DIRECT3D };
        cfg.video_backend = map[id - M_VB_AUTO];
        break; }
    default:
        if (id >= M_SLOT0 && id <= M_SLOT_LAST) cfg.state_slot = id - M_SLOT0;
        else if (id >= M_KEY0 && id <= M_KEY0_END) capture_key(id - M_KEY0);
        else if (id >= M_JOY0 && id <= M_JOY0_END) capture_joy(id - M_JOY0);
        break;
    }
    gui_native_sync();
}
static int item_checked(int id) {
    switch (id) {
    case M_AUTORUN: return cfg.autorun; case M_SCANLINE: return cfg.scanline;
    case M_FIT: return cfg.scalemode==1; case M_AUTOSKIP: return cfg.autoframeskip;
    case M_SMOOTH: return cfg.smooth; case M_THROTTLE: return cfg.autothrottle;
    case M_SHOWFPS: return cfg.showfps; case M_SHOWFPSPCT: return cfg.showfps_pct;
    case M_SOUND: return cfg.sound_enable; case M_DSOUNDA: return cfg.dsound_a;
    case M_DSOUNDB: return cfg.dsound_b; case M_GBCSOUND: return cfg.gbc_sound;
    case M_REVSTEREO: return cfg.reverse_stereo; case M_BIOSEN: return cfg.biosenable;
    case M_BIOSBOOT: return cfg.biosfastboot; case M_SKIPINTROS: return cfg.skipintros;
    default:
        if (id>=M_SLOT0 && id<=M_SLOT_LAST) return cfg.state_slot==id-M_SLOT0;
        if (id>=M_ZOOM1 && id<=M_ZOOM4) return cfg.zoom==id-M_ZOOM1+1;
        if (id>=M_SKIP0 && id<=M_SKIP5) return cfg.frameskip==id-M_SKIP0;
        if (id>=M_VB_AUTO && id<=M_VB_D3D) {
            static const int map[6] = { VID_GDI, VID_GDI, VID_GDI, VID_GDI,
                                        VID_GDI, VID_DIRECT3D };
            return cfg.video_backend == map[id-M_VB_AUTO];
        }
        return 0;
    }
}

/* ------------------------------------------------------------------ wndproc */
static LRESULT CALLBACK wnd_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        break;
    case WM_SIZE:
        g_client_w = LOWORD(lp);
        g_client_h = HIWORD(lp);
        if (g_client_w < 1) g_client_w = 1;
        if (g_client_h < 1) g_client_h = 1;
        break;
    case WM_KEYDOWN: {
        int vk = (int)wp;
        if (key_capture >= 0) { on_key_assign(vk); return 0; }
        switch (vk) {
        case VK_ESCAPE: g_quit = 1; PostMessage(h, WM_CLOSE, 0, 0); break;
        case VK_F1: show_about(); break;
        case VK_F2: { char p[64]; state_path(p,sizeof p); gba_save_state(g,p); break; }
        case VK_F3: { char p[64]; state_path(p,sizeof p); gba_load_state(g,p); break; }
        case VK_F5: save_screenshot(); break;
        case 'P':   g_paused = !g_paused; break;
        case VK_F9: show_history(); break;
        case VK_F11: toggle_fullscreen(); break;
        default: break;
        }
        return 0;
    }
    case WM_MOUSEWHEEL:
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    default:
        break;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

/* ------------------------------------------------------------------ compose */
static void compose_frame(FB *fb, float fps) {
    fb_fill(fb, 0xFF000000);
    int aw = fb->w, ah = fb->h;
    /* Scale factor that keeps the 3:2 aspect. "Fit to window" uses the
     * largest whole multiple; a fixed zoom is reduced when the client area
     * is smaller, so the picture is never cropped. */
    double s;
    if (cfg.scalemode == 1) {
        double sx = (double)aw / SCREEN_W, sy = (double)ah / SCREEN_H;
        s = sx < sy ? sx : sy;
    } else {
        s = cfg.zoom;
        double sx = (double)aw / SCREEN_W, sy = (double)ah / SCREEN_H;
        double lim = sx < sy ? sx : sy;
        if (s > lim) s = lim;
    }
    int dstw = (int)(SCREEN_W * s), dsth = (int)(SCREEN_H * s);
    if (dstw < 1) dstw = 1;
    if (dsth < 1) dsth = 1;
    int dstx = (aw - dstw) / 2, dsty = (ah - dsth) / 2;
    fb_blit_frame(fb, dstx, dsty, dstw, dsth, g->ppu.frame, cfg.scanline);

    if (cfg.showfps) {
        char t[64];
        if (cfg.showfps_pct) snprintf(t,sizeof t,"%d%%",(int)(fps*100.0/59.7275));
        else snprintf(t,sizeof t,"%.0f fps",fps);
        fb_text(fb, fb->w-(int)strlen(t)*8-6, 4, t, 0xFFFFFF00);
    }
}

/* ------------------------------------------------------------------ utils */
uint32_t get_ticks_us(void) {
    return (uint32_t)(GetTickCount64() * 1000);
}
void sleep_ms(int ms) { Sleep((DWORD)ms); }

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmdline, int show) {
    (void)prev; (void)cmdline; (void)show;
    g_inst = inst;

    cfg_load_best();
    const char *rom_arg = NULL;
    {
        LPWSTR w = GetCommandLineW();
        int argc = 0; LPWSTR *argv = CommandLineToArgvW(w, &argc);
        static char romarg[512]; romarg[0] = 0;
        for (int i = 1; i < argc; i++) {
            if (argv[i][0] == L'-') {
                if (!_wcsicmp(argv[i], L"-sound")) cfg.sound_enable = 1;
                else if (!_wcsicmp(argv[i], L"-bios")) cfg.biosenable = 1;
                else if (!_wcsicmp(argv[i], L"-fastboot")) cfg.biosfastboot = 1;
            } else {
                WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, romarg, sizeof romarg, NULL, NULL);
                rom_arg = romarg;
            }
        }
        LocalFree(argv);
    }

    g = (GBA *)calloc(1, sizeof(GBA));
    if (!g) { MessageBoxA(NULL, "Not enough memory for the emulator core.", GBA_NAME, MB_OK|MB_ICONERROR); return 1; }
    gba_init(g);
    gba_load_bios(g, "gba_bios.bin");
    g->bios_enabled = cfg.biosenable; g->bios_fastboot = cfg.biosfastboot;
    g->skip_intros = cfg.skipintros;
    g->frameskip = cfg.frameskip;
    if (rom_arg) load_rom(rom_arg);

    WNDCLASSEXW wc; memset(&wc, 0, sizeof wc);
    wc.cbSize = sizeof wc;
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = inst;
    wc.hIcon = LoadIconA(inst, (LPCSTR)1);  /* icon.res supplies resource 1 */
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = L"BoycottAdvanceReborn";
    RegisterClassExW(&wc);

    {
        wchar_t wtitle[128];
        MultiByteToWideChar(CP_UTF8, 0, GBA_TITLE, -1, wtitle, 128);
        RECT r = { 0, 0, SCREEN_W * cfg.zoom, SCREEN_H * cfg.zoom };
        AdjustWindowRectEx(&r, WS_OVERLAPPEDWINDOW, TRUE, 0);
        g_hwnd = CreateWindowExW(0, wc.lpszClassName, wtitle,
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT, CW_USEDEFAULT,
            r.right - r.left, r.bottom - r.top,
            NULL, NULL, inst, NULL);
    }
    if (!g_hwnd) { MessageBoxA(NULL, "Cannot create window.", GBA_NAME, MB_OK|MB_ICONERROR); free(g); return 1; }

    SetWindowLongPtrA(g_hwnd, GWLP_USERDATA, 0);

    /* Native menu bar (gui.c). */
    GuiCallbacks cb = { gui_cb_action, gui_cb_checked, gui_cb_loadrom,
                        gui_cb_setbios, gui_cb_pause, gui_cb_resume,
                        gui_cb_keyname, gui_cb_joyname,
                        gui_cb_keyget, gui_cb_keyset, gui_cb_joyget, gui_cb_joyset };
    gui_native_init((void *)g_hwnd, gba_icon_argb, GBA_ICON_W, GBA_ICON_H, &cb);

    ShowWindow(g_hwnd, SW_SHOW);
    UpdateWindow(g_hwnd);
    g_client_w = SCREEN_W*cfg.zoom; g_client_h = SCREEN_H*cfg.zoom;

    if (cfg.sound_enable) audio_open();
    apu_open();

    Video *video = NULL;
    FB  fb;
    int fb_w = 0, fb_h = 0;
    int want_backend = cfg.video_backend;

    timeBeginPeriod(1);
    LARGE_INTEGER freq, t_frame, t1, t_fps; QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t_frame); QueryPerformanceCounter(&t_fps);
    int framecount = 0;
    float fps = 0;

    int running = 1;
    MSG msg;
    while (running) {
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) running = 0;
        }
        if (g_quit) break;

        poll_joy_capture();

        /* Run one frame. */
        if (!g_paused && g->rom_size > 0) {
            gba_update_keypad(g, key_state());
            gba_run_frame(g);
        }

        /* Recreate the framebuffer and presenter on demand. */
        RECT rc; GetClientRect(g_hwnd, &rc);
        int cw = rc.right - rc.left, ch = rc.bottom - rc.top;
        if (cw < 1) cw = 1;
        if (ch < 1) ch = 1;
        if (fb_w != cw || fb_h != ch) {
            if (fb_w) fb_free(&fb);
            fb_init(&fb, cw, ch);
            fb_w = cw; fb_h = ch;
        }
        if (want_backend != cfg.video_backend || !video) {
            if (video) vid_shutdown(video);
            video = vid_init((void *)g_hwnd, (VidBackend)cfg.video_backend);
            want_backend = cfg.video_backend;
        }

        compose_frame(&fb, fps);
        vid_present(video, fb.px, fb.w, fb.h);

        /* FPS counting + frame throttle.  The GBA runs 59.7275 Hz, i.e.
         * 16.7427 ms per frame.  The counter must be re-read *after* the
         * Sleep, otherwise the sleep time is not attributed to this frame
         * and the loop skips every other delay (~150% speed). */
        QueryPerformanceCounter(&t1);
        framecount++;
        double dt = (double)(t1.QuadPart - t_fps.QuadPart) / (double)freq.QuadPart;
        if (dt >= 0.5) { fps = framecount / (float)dt; framecount = 0; t_fps = t1; }
        double frame_ms = (double)(t1.QuadPart - t_frame.QuadPart) * 1000.0 / (double)freq.QuadPart;
        if (cfg.autothrottle && !g_paused && g->rom_size > 0 && frame_ms < GBA_FRAME_MS) {
            Sleep((DWORD)(GBA_FRAME_MS - frame_ms));
            QueryPerformanceCounter(&t1);
        }
        t_frame = t1;
    }

    gba_save_battery(g, cfg.rompath);
    timeEndPeriod(1);
    apu_close();
    audio_close();
    if (video) vid_shutdown(video);
    if (fb_w) fb_free(&fb);
    save_cfg();
    gui_native_shutdown();
    free(g);
    return 0;
}

#endif /* _WIN32 */