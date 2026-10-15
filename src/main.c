/*
 * main.c - SDL2 frontend for BoycottAdvance Reborn.
 *
 * Reproduces the original BoycottAdvance 0.2.8 GUI (menu bar + options)
 * with the CPU Debugger removed. Audio is SDL2 (no obsolete WINMM waveOut), so
 * sound works on Windows 11. Uses only SDL2 symbols available since 2.0.0,
 * so the executable runs against any reasonably recent system SDL2.dll.
 */
#define _DEFAULT_SOURCE
#include "gba.h"
#include "mem.h"
#include "arm.h"
#include "font8x8.h"
#include "icon.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#ifdef _WIN32
#  include <windows.h>
#else
#  include <dirent.h>
#  include <sys/stat.h>
#endif

#define AUDIO_FREQ     44100
#define AUDIO_CHANNELS 2
#define MENUBAR_H      18

/* ------------------------------------------------------------------ config */
typedef struct {
    int  zoom, scalemode, scanline, smooth, autothrottle;
    int  showfps, showfps_pct, frameskip, autoframeskip, sound_enable;
    int  dsound_a, dsound_b, gbc_sound, reverse_stereo;
    int  biosenable, biosfastboot, skipintros, autorun, flashid, state_slot;
    char lastdir[512], rompath[512];
} Config;
static Config cfg;
static GBA   *g;
static int    g_paused;

/* ------------------------------------------------------------------ .ini */
static void ini_get(const char *path, const char *key, char *out, size_t n) {
    FILE *f = fopen(path, "rt"); out[0] = 0;
    if (!f) return;
    char line[512]; size_t klen = strlen(key);
    while (fgets(line, sizeof line, f)) {
        char *p = line;
        while (*p==' '||*p=='\t') p++;
        if (strncmp(p, key, klen)==0) {
            p += klen; while (*p==' '||*p=='\t') p++;
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
    ini_get(path, "RomPath", cfg.lastdir, sizeof cfg.lastdir);
    if (!cfg.lastdir[0]) snprintf(cfg.lastdir, sizeof cfg.lastdir, ".");
}
static void cfg_save(const char *path) {
    FILE *f = fopen(path, "wt"); if (!f) return;
    fprintf(f,
      "ScreenZoom %d\nScanline %d\nAutoThrottle %d\nShowFPS %d\nShowFPSPercent %d\n"
      "FrameSkip %d\nAutoFrameSkip %d\nSoundEnable %d\nGameboySoundEnable %d\n"
      "DirectSoundAEnable %d\nDirectSoundBEnable %d\nBIOSEnable %d\nBIOSFastBoot %d\n"
      "SkipIntros %d\nAutoRun %d\nFlashROMId %d\nRomPath %s\n",
      cfg.zoom, cfg.scanline, cfg.autothrottle, cfg.showfps, cfg.showfps_pct,
      cfg.frameskip, cfg.autoframeskip, cfg.sound_enable, cfg.gbc_sound,
      cfg.dsound_a, cfg.dsound_b, cfg.biosenable, cfg.biosfastboot,
      cfg.skipintros, cfg.autorun, cfg.flashid, cfg.lastdir);
    fclose(f);
}

/* ------------------------------------------------------------------ audio */
static void audio_cb(void *ud, Uint8 *stream, int len) {
    GBA *gg = (GBA *)ud;
    int16_t *out = (int16_t *)stream;
    int n = len / 2 / AUDIO_CHANNELS;
    if (!cfg.sound_enable) { memset(stream, 0, len); return; }
    apu_mix(gg, out, n);
    if (cfg.reverse_stereo)
        for (int i = 0; i < n; i++) { int16_t l = out[i*2]; out[i*2] = out[i*2+1]; out[i*2+1] = l; }
}

/* ------------------------------------------------------------------- font */
static void draw_text(SDL_Renderer *r, int x, int y, const char *s, SDL_Color col) {
    SDL_SetRenderDrawColor(r, col.r, col.g, col.b, col.a);
    for (; *s; ++s, x += 8) {
        unsigned char c = (unsigned char)*s;
        for (int i = 0; i < 8; i++) { unsigned row = font8x8_basic[c][i];
            for (int j = 0; j < 8; j++) if (row & (1u<<j)) SDL_RenderDrawPoint(r, x+j, y+i); }
    }
}
static int text_w(const char *s) { return (int)strlen(s) * 8; }

/* --------------------------------------------------------------- key state */
static uint16_t key_state(void) {
    uint16_t k = 0x03FF; const Uint8 *s = SDL_GetKeyboardState(NULL);
    if (s[SDL_SCANCODE_A]) k &= ~0x0001;
    if (s[SDL_SCANCODE_S]) k &= ~0x0002;
    if (s[SDL_SCANCODE_BACKSPACE]) k &= ~0x0004;
    if (s[SDL_SCANCODE_RETURN]) k &= ~0x0008;
    if (s[SDL_SCANCODE_RIGHT]) k &= ~0x0010;
    if (s[SDL_SCANCODE_LEFT]) k &= ~0x0020;
    if (s[SDL_SCANCODE_UP]) k &= ~0x0040;
    if (s[SDL_SCANCODE_DOWN]) k &= ~0x0080;
    if (s[SDL_SCANCODE_D]) k &= ~0x0100;
    if (s[SDL_SCANCODE_F]) k &= ~0x0200;
    return k;
}

/* ------------------------------------------------------------- menu model */
enum {
    M_LOADROM=1, M_ROMINFO, M_AUTORUN, M_RUNPAUSE, M_RESET, M_STATE_SLOT,
    M_LOADSTATE, M_SAVESTATE, M_EXIT,
    M_ZOOM1, M_ZOOM2, M_ZOOM3, M_ZOOM4, M_FLASHID, M_SCANLINE, M_FIT, M_FULLSCREEN,
    M_SKIP0, M_SKIP1, M_SKIP2, M_SKIP3, M_SKIP4, M_SKIP5, M_AUTOSKIP, M_SMOOTH,
    M_THROTTLE, M_SHOWFPS, M_SHOWFPSPCT, M_SOUND, M_DSOUNDA, M_DSOUNDB, M_GBCSOUND,
    M_REVSTEREO, M_SELBIOS, M_BIOSEN, M_BIOSBOOT, M_SKIPINTROS, M_CONTROLLERS,
    M_HISTORY, M_CMDLINE, M_ABOUT
};
typedef struct { const char *label; int id; } MenuItem;
static MenuItem m_file[] = {
    {"Load ROM...",M_LOADROM},{"ROM Information...",M_ROMINFO},{"Auto Run",M_AUTORUN},{"",0},
    {"Run/Pause Emulation",M_RUNPAUSE},{"Reset Emulation",M_RESET},{"",0},
    {"Choose State Slot",M_STATE_SLOT},{"Load State",M_LOADSTATE},{"Save State",M_SAVESTATE},
    {"",0},{"Exit",M_EXIT},{NULL,0}};
static MenuItem m_window[] = {
    {"Zoom x1 (100%)",M_ZOOM1},{"Zoom x2 (200%)",M_ZOOM2},{"Zoom x3 (300%)",M_ZOOM3},
    {"Zoom x4 (400%)",M_ZOOM4},{"",0},{"Select FLASHROM ID",M_FLASHID},{"",0},
    {"Scanline Display (TV Mode)",M_SCANLINE},{"Stretch Fit To Screen",M_FIT},
    {"Fullscreen",M_FULLSCREEN},{NULL,0}};
static MenuItem m_options[] = {
    {"Frameskip",0},{"Skip 0 Frames",M_SKIP0},{"Skip 1 Frame",M_SKIP1},{"Skip 2 Frames",M_SKIP2},
    {"Skip 3 Frames",M_SKIP3},{"Skip 4 Frames",M_SKIP4},{"Skip 5 Frames",M_SKIP5},
    {"Auto Frame Skip",M_AUTOSKIP},{"",0},{"Smooth Emulation",M_SMOOTH},
    {"Auto Speed Throttle",M_THROTTLE},{"",0},{"Show FPS",M_SHOWFPS},
    {"Show FPS in percent (%)",M_SHOWFPSPCT},{"",0},{"Sound Enable",M_SOUND},
    {"Enable DSound A",M_DSOUNDA},{"Enable DSound B",M_DSOUNDB},{"Enable GBC Sound",M_GBCSOUND},
    {"Reverse Sound Stereo",M_REVSTEREO},{"",0},{"Select BIOS File...",M_SELBIOS},
    {"BIOS Enable",M_BIOSEN},{"Skip BIOS Boot",M_BIOSBOOT},{"Skip Intro Hacks",M_SKIPINTROS},
    {"",0},{"Controllers Configuration...",M_CONTROLLERS},{NULL,0}};
static MenuItem m_help[] = {
    {"History...",M_HISTORY},{"Command Line Options...",M_CMDLINE},{"About...",M_ABOUT},{NULL,0}};
static struct { const char *title; MenuItem *items; } menus[] = {
    {"File",m_file},{"Window",m_window},{"Options",m_options},{"Help",m_help}};
#define NMENUS ((int)(sizeof menus / sizeof menus[0]))

static int menu_open = -1, menu_hover = -1;
static int dialog_kind = 0;               /* 0 none, 1 text, 2 rom browser */
static char dialog_title[64], dialog_body[2048];

static int menu_x(int i) { int x=4; for (int j=0;j<i;j++) x += text_w(menus[j].title)+12; return x; }
static int menu_width(MenuItem *it) { int w=0; for (MenuItem*m=it;m->label;++m) if(m->label[0]&&text_w(m->label)>w) w=text_w(m->label); return w+24; }
static int menu_height(MenuItem *it) { int h=4; for (MenuItem*m=it;m->label;++m) h += m->label[0]?16:7; return h; }
static void dlg_text(const char *t, const char *b) {
    dialog_kind = 1; snprintf(dialog_title,sizeof dialog_title,"%s",t);
    snprintf(dialog_body,sizeof dialog_body,"%s",b);
}

/* built-in ROM browser (no dependency on newer SDL file-dialog APIs) */
typedef struct { char name[260]; int is_dir; } DirEnt;
#define BROWSER_MAX 512
static DirEnt rom_list[BROWSER_MAX]; static int rom_count, rom_sel;
static char   rom_dir[512];              /* directory currently listed */
static void   load_rom(const char *path);

static int has_rom_ext(const char *n) {
    size_t L = strlen(n);
    if (L < 5) return 0;
    const char *e = n + L - 4;
    return !strcasecmp(e, ".gba") || !strcasecmp(e, ".agb") || !strcasecmp(e, ".bin");
}
static int cmp_dir_ent(const void *a, const void *b) {
    const DirEnt *x = a, *y = b;
    if (x->is_dir != y->is_dir) return y->is_dir - x->is_dir;   /* dirs first */
    return strcasecmp(x->name, y->name);
}
static void rom_add(const char *name, int is_dir) {
    if (rom_count >= BROWSER_MAX) return;
    snprintf(rom_list[rom_count].name, sizeof rom_list[rom_count].name, "%s", name);
    rom_list[rom_count].is_dir = is_dir;
    rom_count++;
}
static void rom_dir_enter(const char *dir) {
    char resolved[512];
    if (realpath(dir, resolved)) snprintf(rom_dir, sizeof rom_dir, "%s", resolved);
    else snprintf(rom_dir, sizeof rom_dir, "%s", dir);
    rom_count = 0; rom_sel = 0;
    if (strcmp(rom_dir, "/")) rom_add("..", 1);                 /* parent */
#ifdef _WIN32
    char pat[600]; snprintf(pat, sizeof pat, "%s\\*", rom_dir);
    WIN32_FIND_DATAA fd; HANDLE h = FindFirstFileA(pat, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, "..")) continue;
            int isdir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            if (isdir || has_rom_ext(fd.cFileName)) rom_add(fd.cFileName, isdir);
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
#else
    DIR *d = opendir(rom_dir);
    if (d) { struct dirent *e;
        while ((e = readdir(d))) {
            const char *n = e->d_name;
            if (!strcmp(n, ".") || !strcmp(n, "..")) continue;
            char full[1024]; snprintf(full, sizeof full, "%s/%s", rom_dir, n);
            struct stat st; int isdir = (stat(full, &st) == 0 && S_ISDIR(st.st_mode));
            if (isdir || has_rom_ext(n)) rom_add(n, isdir);
        }
        closedir(d);
    }
#endif
    qsort(rom_list, rom_count, sizeof rom_list[0], cmp_dir_ent);
}
static void rom_browser_open(void) {
    dialog_kind = 2;
    rom_dir_enter(cfg.lastdir[0] ? cfg.lastdir : ".");
}
static void rom_activate(int idx) {
    if (idx < 0 || idx >= rom_count) return;
    if (rom_list[idx].is_dir) {                 /* navigate into folder */
        char full[1200];
        if (!strcmp(rom_list[idx].name, "..")) {
            snprintf(full, sizeof full, "%s/..", rom_dir);
        } else {
            snprintf(full, sizeof full, "%s/%s", rom_dir, rom_list[idx].name);
        }
        rom_dir_enter(full);
        return;
    }
    char full[1200]; snprintf(full, sizeof full, "%s/%s", rom_dir, rom_list[idx].name);
    load_rom(full);
    if (g->rom_size > 0) { snprintf(cfg.lastdir, sizeof cfg.lastdir, "%s", rom_dir); dialog_kind = 0; }
}

/* ------------------------------------------------------------------ actions */
static void load_rom(const char *path) {
    if (gba_load_rom(g, path)) {
        snprintf(cfg.rompath, sizeof cfg.rompath, "%s", path);
        /* remember folder for the browser */
        const char *sl = strrchr(path,'/'); if (!sl) sl = strrchr(path,'\\');
        if (sl) { size_t n = sl-path; if (n<sizeof cfg.lastdir) { memcpy(cfg.lastdir,path,n); cfg.lastdir[n]=0; } }
        gba_reset(g); g_paused = 0;
    } else dlg_text("Error", "Cannot load ROM file.");
}
static void state_path(char *b, size_t n) { snprintf(b,n,"state%d.sav",cfg.state_slot); }
static void show_rom_info(void) {
    char b[1024];
    snprintf(b,sizeof b,
        "FileName : %s\nTitle    : %.16s\nSize     : %u bytes (%u KB)\n"
        "Save type: %d\nBackup size: %u bytes",
        cfg.rompath[0]?cfg.rompath:"(none)", g->rom_title, g->rom_size,
        g->rom_size/1024, g->save_type, g->save_size);
    dlg_text("ROM Info", b);
}
static void show_about(void) {
    dlg_text("About",
      GBA_TITLE "\nClean-room reconstruction of BoycottAdvance 0.2.8\n"
      "with a complete ARM7TDMI core and BIOS SWI emulation\n"
      "and an SDL2 audio/video frontend.\n\n"
      "Original authors:\n"
      "  Julien Frelat \"Gollum\" (main emulation, GBA core, GUI,\n"
      "  Win32 port, documentation)\n"
      "  Contributors: Anarko, Fitzy, Dysfunction, Perfect Dark,\n"
      "  SMY, Niels Wagenaar, Forgotten, Champipi\n\n"
      "Original BoycottAdvance was cardware: if you liked it,\n"
      "please send a postcard. Keep crediting the authors.");
}
static void show_history(void) {
    dlg_text("History",
      "BoycottAdvance 0.2.8 (2003) - Julien Frelat and team.\n\n"
      "BoycottAdvance Reborn v1.1.0\n"
      "  - Clean-room ARM7TDMI core (ARM + Thumb)\n"
      "  - Correct BIOS SWI (CpuSet, VBlankIntrWait...)\n"
      "  - Fixed VBlank/HBlank/IRQ (boots Pokemon)\n"
      "  - SDL2 audio (Windows 11 compatible)\n"
      "  - Original menu GUI, CPU debugger removed\n\n"
      "v1.1.0 changes:\n"
      "  - Assembler-free multi-architecture builds\n"
      "    for Windows x86 and x64\n"
      "  - Fixed executable entry-point error on systems\n"
      "    with an older SDL2.dll (SDL_GetTicks64)\n"
      "  - Fixed instant startup crash (heap-allocated core\n"
      "    instead of the 1 MB Windows stack)\n"
      "  - Menu bar with every original option\n"
      "    (File / Window / Options / Help)\n"
      "  - Embedded window icon\n"
      "  - ROM browser and BoycottAdvance.ini support\n\n"
      "v1.2.0 changes:\n"
      "  - Fixed ARM LDR/LDM with PC target and PPU bitmap\n"
      "    layer gating that left Pokemon on a black screen\n"
      "  - Removed the CPU debugger menu entirely\n"
      "  - Built-in ROM browser with folder navigation\n"
      "    (pick ROMs from any directory)\n"
      "  - Embedded window and executable icon\n\n"
      "v1.0.0: first public clean-room release.");
}
static void show_cmdline(void) {
    dlg_text("BoycottAdvance Command Line Options",
      "Usage: boycottadvance-reborn [options] [romfile]\n\n"
      "-help        Display this help\n-autosave    Save INI file on exit\n"
      "-sound       Enable sound\n-bios        Enable BIOS\n-fastboot    Skip BIOS boot");
}
static void show_controllers(void) {
    dlg_text("Controllers Configuration",
      "Keyboard (default):\n  A=A  B=S  L=F  R=D  Start=Enter  Select=Backspace\n"
      "  D-Pad = arrow keys\n\n"
      "Joystick: standard SDL2 game controllers are used when present.");
}
static void toggle_fullscreen(SDL_Window *w) {
    static int fs = 0; fs = !fs;
    SDL_SetWindowFullscreen(w, fs ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
}

static void do_action(int id, SDL_Window *win) {
    switch (id) {
    case M_LOADROM: rom_browser_open(); break;
    case M_ROMINFO: show_rom_info(); break;
    case M_AUTORUN: cfg.autorun = !cfg.autorun; break;
    case M_RUNPAUSE: g_paused = !g_paused; break;
    case M_RESET: gba_reset(g); g_paused = 0; break;
    case M_STATE_SLOT: cfg.state_slot = (cfg.state_slot+1)%10; break;
    case M_LOADSTATE: { char p[64]; state_path(p,sizeof p); gba_load_state(g,p); break; }
    case M_SAVESTATE:{ char p[64]; state_path(p,sizeof p); gba_save_state(g,p); break; }
    case M_EXIT: { SDL_Event q; q.type=SDL_QUIT; SDL_PushEvent(&q); break; }
    case M_ZOOM1: case M_ZOOM2: case M_ZOOM3: case M_ZOOM4:
        cfg.zoom = id-M_ZOOM1+1; cfg.scalemode = 0; break;
    case M_FLASHID: cfg.flashid = (cfg.flashid+1)%3; break;
    case M_SCANLINE: cfg.scanline = !cfg.scanline; break;
    case M_FIT: cfg.scalemode = (cfg.scalemode==1)?0:1; break;
    case M_FULLSCREEN: toggle_fullscreen(win); cfg.scalemode = 2; break;
    case M_SKIP0: case M_SKIP1: case M_SKIP2: case M_SKIP3: case M_SKIP4: case M_SKIP5:
        cfg.frameskip = id-M_SKIP0; g->frameskip = cfg.frameskip; break;
    case M_AUTOSKIP: cfg.autoframeskip = !cfg.autoframeskip; break;
    case M_SMOOTH: cfg.smooth = !cfg.smooth; break;
    case M_THROTTLE: cfg.autothrottle = !cfg.autothrottle; break;
    case M_SHOWFPS: cfg.showfps = !cfg.showfps; break;
    case M_SHOWFPSPCT: cfg.showfps_pct = !cfg.showfps_pct; break;
    case M_SOUND: cfg.sound_enable = !cfg.sound_enable; break;
    case M_DSOUNDA: cfg.dsound_a = !cfg.dsound_a; break;
    case M_DSOUNDB: cfg.dsound_b = !cfg.dsound_b; break;
    case M_GBCSOUND: cfg.gbc_sound = !cfg.gbc_sound; break;
    case M_REVSTEREO: cfg.reverse_stereo = !cfg.reverse_stereo; break;
    case M_SELBIOS: gba_load_bios(g, "gba_bios.bin"); dlg_text("BIOS","BIOS loaded from gba_bios.bin"); break;
    case M_BIOSEN: cfg.biosenable = !cfg.biosenable; g->bios_enabled = cfg.biosenable; break;
    case M_BIOSBOOT: cfg.biosfastboot = !cfg.biosfastboot; g->bios_fastboot = cfg.biosfastboot; break;
    case M_SKIPINTROS: cfg.skipintros = !cfg.skipintros; break;
    case M_CONTROLLERS: show_controllers(); break;
    case M_HISTORY: show_history(); break;
    case M_CMDLINE: show_cmdline(); break;
    case M_ABOUT: show_about(); break;
    default: break;
    }
}

/* ---------------------------------------------------------------- hit test */
static int menubar_hit(int mx) {
    for (int i = 0; i < NMENUS; i++) { int x = menu_x(i), w = text_w(menus[i].title)+12;
        if (mx>=x && mx<x+w) return i; }
    return -1;
}
static int menu_item_hit(int mi, int mx, int my) {
    MenuItem *it = menus[mi].items; int mw = menu_width(it), bx = menu_x(mi);
    if (mx<bx || mx>=bx+mw || my<MENUBAR_H || my>=MENUBAR_H+menu_height(it)) return -1;
    int y = MENUBAR_H+2, idx = 0;
    for (MenuItem *m=it; m->label; ++m, idx++) {
        int h = m->label[0]?16:7;
        if (my>=y && my<y+h) return m->label[0]?idx:-1;
        y += h;
    }
    return -1;
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
        if (id>=M_ZOOM1 && id<=M_ZOOM4) return cfg.zoom==id-M_ZOOM1+1;
        if (id>=M_SKIP0 && id<=M_SKIP5) return cfg.frameskip==id-M_SKIP0;
        return 0;
    }
}

/* ------------------------------------------------------------------- draw */
static void draw_menubar(SDL_Renderer *r) {
    SDL_SetRenderDrawColor(r,220,220,220,255);
    SDL_Rect bar = {0,0,10000,MENUBAR_H}; SDL_RenderFillRect(r,&bar);
    SDL_SetRenderDrawColor(r,120,120,120,255);
    SDL_RenderDrawLine(r,0,MENUBAR_H-1,10000,MENUBAR_H-1);
    for (int i = 0; i < NMENUS; i++) {
        int x = menu_x(i);
        SDL_Color c = {0,0,0,255};
        if (menu_open == i) {
            SDL_SetRenderDrawColor(r,90,120,200,255);
            SDL_Rect hi = {x-2,2,text_w(menus[i].title)+12,MENUBAR_H-4};
            SDL_RenderFillRect(r,&hi); c = (SDL_Color){255,255,255,255};
        }
        draw_text(r, x+4, 5, menus[i].title, c);
    }
    if (menu_open < 0) return;
    MenuItem *it = menus[menu_open].items;
    int mw = menu_width(it), mh = menu_height(it), mx = menu_x(menu_open);
    SDL_SetRenderDrawColor(r,240,240,240,255);
    SDL_Rect box = {mx,MENUBAR_H,mw,mh}; SDL_RenderFillRect(r,&box);
    SDL_SetRenderDrawColor(r,80,80,80,255); SDL_RenderDrawRect(r,&box);
    int y = MENUBAR_H+2, idx = 0;
    for (MenuItem *m=it; m->label; ++m, idx++) {
        if (!m->label[0]) { SDL_SetRenderDrawColor(r,160,160,160,255);
            SDL_RenderDrawLine(r,mx+2,y+3,mx+mw-2,y+3); y += 7; continue; }
        int sel = (menu_hover==idx), chk = (m->id?item_checked(m->id):0);
        if (sel) { SDL_SetRenderDrawColor(r,90,120,200,255);
            SDL_Rect hi={mx+1,y,mw-2,16}; SDL_RenderFillRect(r,&hi); }
        SDL_Color c = sel?(SDL_Color){255,255,255,255}:(SDL_Color){0,0,0,255};
        draw_text(r, mx+4, y+4, chk?"*":" ", c);
        draw_text(r, mx+18, y+4, m->label, c);
        y += 16;
    }
}

/* ------------------------------------------------------------------- main */
int main(int argc, char **argv) {
    const char *rom_arg = NULL;
    cfg_load("BoycottAdvance.ini");        /* defaults first */
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i],"-help")) { printf("Options: -help -autosave -sound -bios -fastboot [rom]\n"); return 0; }
        else if (!strcmp(argv[i],"-sound")) cfg.sound_enable = 1;
        else if (!strcmp(argv[i],"-bios")) cfg.biosenable = 1;
        else if (!strcmp(argv[i],"-fastboot")) cfg.biosfastboot = 1;
        else if (argv[i][0] != '-') rom_arg = argv[i];
    }

    if (SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO|SDL_INIT_GAMECONTROLLER) < 0) {
        fprintf(stderr,"SDL init failed: %s\n", SDL_GetError()); return 1; }

    g = (GBA *)calloc(1, sizeof(GBA));       /* ~34 MB: keep off the stack */
    if (!g) { SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, GBA_NAME,
        "Not enough memory for the emulator core.", NULL); SDL_Quit(); return 1; }
    gba_init(g);
    gba_load_bios(g, "gba_bios.bin");
    g->bios_enabled = cfg.biosenable; g->bios_fastboot = cfg.biosfastboot;
    g->frameskip = cfg.frameskip;

    if (rom_arg) load_rom(rom_arg);

    int w = SCREEN_W*cfg.zoom, h = SCREEN_H*cfg.zoom + MENUBAR_H;
    SDL_Window *win = SDL_CreateWindow(GBA_TITLE, SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED, w, h, SDL_WINDOW_RESIZABLE);
    if (!win) { fprintf(stderr,"no window: %s\n", SDL_GetError()); SDL_Quit(); free(g); return 1; }
    {
        SDL_Surface *ico = SDL_CreateRGBSurfaceWithFormatFrom(
            (void *)gba_icon_argb, GBA_ICON_W, GBA_ICON_H, 32, GBA_ICON_W*4,
            SDL_PIXELFORMAT_ARGB8888);
        if (ico) { SDL_SetWindowIcon(win, ico); SDL_FreeSurface(ico); }
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, cfg.smooth?"1":"0");
    SDL_Renderer *ren = SDL_CreateRenderer(win,-1,SDL_RENDERER_ACCELERATED);
    if (!ren) ren = SDL_CreateRenderer(win,-1,SDL_RENDERER_SOFTWARE);
    SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING, SCREEN_W, SCREEN_H);

    SDL_AudioSpec want,hv; SDL_zero(want);
    want.freq=AUDIO_FREQ; want.format=AUDIO_S16SYS; want.channels=AUDIO_CHANNELS;
    want.samples=1024; want.callback=audio_cb; want.userdata=g;
    SDL_AudioDeviceID adev = SDL_OpenAudioDevice(NULL,0,&want,&hv,0);
    if (adev) SDL_PauseAudioDevice(adev,0);

    uint32_t *pixels = (uint32_t *)malloc(SCREEN_W*SCREEN_H*4);
    int running = 1; uint32_t last = SDL_GetTicks(), fps_t = last;
    int fps_n = 0; float fps = 0;

    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) running = 0;
            else if (ev.type == SDL_MOUSEBUTTONDOWN && ev.button.button == SDL_BUTTON_LEFT) {
                int mx = ev.button.x, my = ev.button.y;
                if (dialog_kind == 2) {
                    int list_y = 64;
                    if (my>=list_y && my<list_y+16*rom_count) {
                        int sel = (my-list_y)/16;
                        if (sel>=0 && sel<rom_count) rom_activate(sel);
                    }
                } else if (dialog_kind == 1) {
                    dialog_kind = 0;
                } else if (my < MENUBAR_H) {
                    int hit = menubar_hit(mx);
                    menu_open = (hit==menu_open)?-1:hit; menu_hover = -1;
                } else if (menu_open >= 0) {
                    int it = menu_item_hit(menu_open,mx,my);
                    if (it>=0) { do_action(menus[menu_open].items[it].id,win); menu_open=-1; }
                    else menu_open = -1;
                }
            } else if (ev.type == SDL_MOUSEMOTION && menu_open >= 0) {
                menu_hover = menu_item_hit(menu_open, ev.motion.x, ev.motion.y);
            } else if (ev.type == SDL_KEYDOWN) {
                SDL_Keycode k = ev.key.keysym.sym;
                if (dialog_kind == 2) {
                    if (k==SDLK_ESCAPE) dialog_kind=0;
                    else if (k==SDLK_DOWN && rom_sel+1<rom_count) rom_sel++;
                    else if (k==SDLK_UP && rom_sel>0) rom_sel--;
                    else if (k==SDLK_RETURN || k==SDLK_KP_ENTER) rom_activate(rom_sel);
                    continue;
                }
                if (dialog_kind) { if (k==SDLK_ESCAPE) dialog_kind=0; continue; }
                if (k==SDLK_ESCAPE) { if (menu_open>=0) menu_open=-1; else running=0; }
                else if (k==SDLK_F1) show_about();
                else if (k==SDLK_F2) { char p[64]; state_path(p,sizeof p); gba_save_state(g,p); }
                else if (k==SDLK_F3) { char p[64]; state_path(p,sizeof p); gba_load_state(g,p); }
                else if (k==SDLK_F5) { FILE*f=fopen("shot.ppm","wb"); if(f){fwrite("P6\n240 160\n255\n",1,14,f);
                    for(int y2=0;y2<SCREEN_H;y2++)for(int x2=0;x2<SCREEN_W;x2++){uint16_t c=g->ppu.frame[y2*SCREEN_W+x2];
                    fputc((c&0x1F)<<3,f);fputc(((c>>5)&0x1F)<<3,f);fputc(((c>>10)&0x1F)<<3,f);} fclose(f);} }
                else if (k==SDLK_p) g_paused = !g_paused;
                else if (k==SDLK_F11) toggle_fullscreen(win);
                else if (k==SDLK_F9) show_history();
            }
        }

        if (!g_paused && g->rom_size > 0) {
            gba_update_keypad(g, key_state());
            gba_run_frame(g);
            gba_apply_cheats(g);
        }

        for (int i = 0; i < SCREEN_W*SCREEN_H; i++) {
            uint16_t c = g->ppu.frame[i];
            int r=(c&0x1F)<<3, gg=((c>>5)&0x1F)<<3, b=((c>>10)&0x1F)<<3;
            if (cfg.scanline && ((i/SCREEN_W)&1)) { r>>=1; gg>>=1; b>>=1; }
            pixels[i] = 0xFF000000u | (r<<16) | (gg<<8) | b;
        }
        SDL_UpdateTexture(tex,NULL,pixels,SCREEN_W*4);

        SDL_SetRenderDrawColor(ren,0,0,0,255);
        SDL_RenderClear(ren);
        int dw=0,dh=0; SDL_GetRendererOutputSize(ren,&dw,&dh);
        SDL_Rect dst={0,MENUBAR_H,0,0};
        if (cfg.scalemode==1) {
            int aw=dw, ah=dh-MENUBAR_H;
            int s = (aw/SCREEN_W < ah/SCREEN_H) ? aw/SCREEN_W : ah/SCREEN_H;
            if (s<1) s=1;
            dst.w=SCREEN_W*s; dst.h=SCREEN_H*s;
            dst.x=(dw-dst.w)/2; dst.y=MENUBAR_H+(ah-dst.h)/2;
        } else {
            dst.w=SCREEN_W*cfg.zoom; dst.h=SCREEN_H*cfg.zoom;
            dst.x=(dw-dst.w)/2; if(dst.x<0) dst.x=0; dst.y=MENUBAR_H;
        }
        SDL_RenderCopy(ren,tex,NULL,&dst);

        if (cfg.showfps) {
            char fb[64];
            if (cfg.showfps_pct) snprintf(fb,sizeof fb,"%d%%",(int)(fps*100/60));
            else snprintf(fb,sizeof fb,"%.0f fps",fps);
            draw_text(ren, dw-text_w(fb)-6, MENUBAR_H+4, fb, (SDL_Color){255,255,0,255});
        }
        draw_menubar(ren);

        if (dialog_kind) {
            SDL_SetRenderDrawColor(ren,30,30,30,220);
            SDL_Rect cover={0,MENUBAR_H,10000,10000}; SDL_RenderFillRect(ren,&cover);
            SDL_SetRenderDrawColor(ren,210,210,210,255);
            SDL_Rect dbox={40,30,560,320}; SDL_RenderFillRect(ren,&dbox);
            SDL_SetRenderDrawColor(ren,0,0,0,255); SDL_RenderDrawRect(ren,&dbox);
            if (dialog_kind==1) {
                draw_text(ren,52,60,dialog_title,(SDL_Color){0,0,160,255});
                int y=80; char tmp[2048]; snprintf(tmp,sizeof tmp,"%s",dialog_body);
                char *line=strtok(tmp,"\n");
                while(line){ draw_text(ren,52,y,line,(SDL_Color){0,0,0,255}); y+=14; line=strtok(NULL,"\n"); }
                draw_text(ren,52,330,"Click or press ESC to close",(SDL_Color){80,80,80,255});
            } else if (dialog_kind==2) {
                draw_text(ren,52,36,"Please choose a ROM file to load...",(SDL_Color){0,0,160,255});
                draw_text(ren,52,48,rom_dir,(SDL_Color){60,60,60,255});
                int y=64, shown=0;
                for (int i=0;i<rom_count && shown<16;i++,y+=16) {
                    int sel = (i==rom_sel);
                    if (sel) { SDL_SetRenderDrawColor(ren,90,120,200,255);
                        SDL_Rect hi={52,y-2,540,16}; SDL_RenderFillRect(ren,&hi); }
                    SDL_Color c = sel?(SDL_Color){255,255,255,255}:(SDL_Color){0,0,0,255};
                    char lbl[300];
                    snprintf(lbl,sizeof lbl,"%s%.260s",rom_list[i].is_dir?"[+] ":"    ",rom_list[i].name);
                    draw_text(ren,56,y,lbl,c);
                    shown++;
                }
                if (!rom_count) draw_text(ren,56,64,"(no .gba/.agb/.bin in folder)",(SDL_Color){120,0,0,255});
                draw_text(ren,52,336,"Click or use Up/Down + Enter. ESC to cancel.",(SDL_Color){80,80,80,255});
            }
        }
        SDL_RenderPresent(ren);

        uint32_t now = SDL_GetTicks();
        if (cfg.autothrottle && !g_paused) {
            if (now-last < 16) SDL_Delay(16-(now-last));
            last = SDL_GetTicks();
        }
        fps_n++;
        if (now-fps_t>=500) { fps = fps_n*1000.0f/(now-fps_t); fps_n=0; fps_t=now; }
    }

    cfg_save("BoycottAdvance.ini");
    if (adev) SDL_CloseAudioDevice(adev);
    free(pixels); free(g);
    SDL_DestroyTexture(tex); SDL_DestroyRenderer(ren); SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
