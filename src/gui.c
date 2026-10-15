/*
 * gui.c - native Win32 GUI for BoycottAdvance Reborn.
 *
 * Gives the Windows build the original BoycottAdvance look: a real menu
 * bar attached to the window, native file-open dialogs for ROMs and the
 * BIOS, and native message boxes for ROM info / history / about.
 *
 * The window is owned by SDL2, so we subclass its window procedure to
 * intercept the menu commands (WM_COMMAND) and refresh the checkmarks
 * (WM_INITMENUPOPUP); every other message is forwarded to SDL untouched.
 *
 * Only compiled on Windows (see Makefile / CI). On other platforms the
 * frontend keeps its software-drawn menu.
 */
#ifdef _WIN32

#include "gui.h"
#include "uiactions.h"
#include <windows.h>
#include <commdlg.h>
#include <string.h>
#include <stdio.h>

static HWND         g_hwnd;
static WNDPROC      g_oldproc;
static GuiCallbacks g_cb;
static volatile LONG g_quit;
/* Win32 file-dialog filters are double-null terminated UTF-16 strings;
 * keep the wide literal (with embedded NULs) intact. */
static const WCHAR   g_romfilter[] =
    L"Game Boy Advance ROMs (*.gba;*.agb;*.bin)\0*.gba;*.agb;*.bin\0"
    L"All files (*.*)\0*.*\0\0";

/* --------------------------------------------------------------- helpers */
static HMENU add_popup(HMENU bar, const char *title) {
    HMENU p = CreatePopupMenu();
    AppendMenuA(bar, MF_STRING | MF_POPUP, (UINT_PTR)p, title);
    return p;
}
static void item(HMENU m, const char *label, int id) {
    AppendMenuA(m, MF_STRING, (UINT)id, label);
}
static void sep(HMENU m) { AppendMenuA(m, MF_SEPARATOR, 0, NULL); }

static void build_menu(void) {
    HMENU bar = CreateMenu();

    HMENU file = add_popup(bar, "&File");
    item(file, "&Load ROM...", M_LOADROM);
    item(file, "ROM &Information...", M_ROMINFO);
    sep(file);
    item(file, "&Auto Run", M_AUTORUN);
    sep(file);
    item(file, "&Run/Pause Emulation", M_RUNPAUSE);
    item(file, "&Reset Emulation", M_RESET);
    sep(file);
    item(file, "Choose &State Slot", M_STATE_SLOT);
    item(file, "&Load State", M_LOADSTATE);
    item(file, "&Save State", M_SAVESTATE);
    sep(file);
    item(file, "E&xit", M_EXIT);

    HMENU win = add_popup(bar, "&Window");
    item(win, "Zoom x1 (100%)", M_ZOOM1);
    item(win, "Zoom x2 (200%)", M_ZOOM2);
    item(win, "Zoom x3 (300%)", M_ZOOM3);
    item(win, "Zoom x4 (400%)", M_ZOOM4);
    sep(win);
    item(win, "Select FLASHROM ID", M_FLASHID);
    sep(win);
    item(win, "&Scanline Display (TV Mode)", M_SCANLINE);
    item(win, "&Stretch Fit To Screen", M_FIT);
    item(win, "&Fullscreen", M_FULLSCREEN);

    HMENU opt = add_popup(bar, "&Options");
    HMENU fs = add_popup(opt, "Frameskip");
    item(fs, "Skip 0 Frames", M_SKIP0);
    item(fs, "Skip 1 Frame",  M_SKIP1);
    item(fs, "Skip 2 Frames", M_SKIP2);
    item(fs, "Skip 3 Frames", M_SKIP3);
    item(fs, "Skip 4 Frames", M_SKIP4);
    item(fs, "Skip 5 Frames", M_SKIP5);
    item(opt, "Auto Frame Skip", M_AUTOSKIP);
    sep(opt);
    item(opt, "Smooth Emulation", M_SMOOTH);
    item(opt, "Auto Speed Throttle", M_THROTTLE);
    sep(opt);
    item(opt, "Show FPS", M_SHOWFPS);
    item(opt, "Show FPS in percent (%)", M_SHOWFPSPCT);
    sep(opt);
    item(opt, "Sound Enable", M_SOUND);
    item(opt, "Enable DSound A", M_DSOUNDA);
    item(opt, "Enable DSound B", M_DSOUNDB);
    item(opt, "Enable GBC Sound", M_GBCSOUND);
    item(opt, "Reverse Sound Stereo", M_REVSTEREO);
    sep(opt);
    item(opt, "Select BIOS File...", M_SELBIOS);
    item(opt, "BIOS Enable", M_BIOSEN);
    item(opt, "Skip BIOS Boot", M_BIOSBOOT);
    item(opt, "Skip Intro Hacks", M_SKIPINTROS);
    sep(opt);
    HMENU fb = add_popup(opt, "Frame Buffer");
    item(fb, "Auto (SDL)", M_VB_AUTO);
    item(fb, "Software (SDL)", M_VB_SOFT);
    item(fb, "OpenGL (SDL)", M_VB_GL);
    item(fb, "Vulkan", M_VB_VK);
    item(fb, "GDI (Windows)", M_VB_GDI);
    item(fb, "Direct3D 9 (Windows)", M_VB_D3D);
    sep(opt);
    HMENU ct = add_popup(opt, "Controls");
    item(ct, "Map A", M_KEY0 + 0);
    item(ct, "Map B", M_KEY0 + 1);
    item(ct, "Map Select", M_KEY0 + 2);
    item(ct, "Map Start", M_KEY0 + 3);
    item(ct, "Map Right", M_KEY0 + 4);
    item(ct, "Map Left", M_KEY0 + 5);
    item(ct, "Map Up", M_KEY0 + 6);
    item(ct, "Map Down", M_KEY0 + 7);
    item(ct, "Map R", M_KEY0 + 8);
    item(ct, "Map L", M_KEY0 + 9);
    sep(ct);
    item(ct, "Reset Key Mappings", M_CONTROLS);

    HMENU help = add_popup(bar, "&Help");
    item(help, "&History...", M_HISTORY);
    item(help, "&Command Line Options...", M_CMDLINE);
    item(help, "&About...", M_ABOUT);

    SetMenu(g_hwnd, bar);
}

/* Refresh the checkmarks for every toggleable command in a menu tree. */
static void sync_menu(HMENU m) {
    int n = GetMenuItemCount(m);
    for (int i = 0; i < n; i++) {
        UINT id = GetMenuItemID(m, i);
        if (id != (UINT)-1 && id != 0) {
            UINT st = (g_cb.checked && g_cb.checked((int)id)) ? MF_CHECKED : MF_UNCHECKED;
            CheckMenuItem(m, id, MF_BYCOMMAND | st);
        }
        HMENU sub = GetSubMenu(m, i);
        if (sub) sync_menu(sub);
    }
}
void gui_native_sync(void) {
    if (!g_hwnd) return;
    HMENU bar = GetMenu(g_hwnd);
    if (bar) sync_menu(bar);
}

/* --------------------------------------------------------------- wndproc */
static LRESULT CALLBACK gui_wndproc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_COMMAND: {
        int id = LOWORD(wp);
        if (id && g_cb.action) {
            if (g_cb.pause_audio)  g_cb.pause_audio();
            g_cb.action(id);
            if (g_cb.resume_audio) g_cb.resume_audio();
            gui_native_sync();
        }
        return 0;
    }
    case WM_INITMENUPOPUP:
        gui_native_sync();
        break;
    case WM_CLOSE:
        g_quit = 1;
        break;
    }
    if (g_oldproc) return CallWindowProcW(g_oldproc, h, msg, wp, lp);
    return DefWindowProcW(h, msg, wp, lp);
}

/* ------------------------------------------------------------------ file */
/* The frontend passes UTF-8; Win32 wants UTF-16. */
static void utf8_to_wide(const char *s, WCHAR *out, int outn) {
    out[0] = 0;
    MultiByteToWideChar(CP_UTF8, 0, s, -1, out, outn);
}
static void wide_to_utf8(const WCHAR *s, char *out, int outn) {
    out[0] = 0;
    WideCharToMultiByte(CP_UTF8, 0, s, -1, out, outn, NULL, NULL);
}

static int open_file_dialog(const char *title, const WCHAR *filter, char *out, size_t outn) {
    OPENFILENAMEW ofn;
    WCHAR wtitle[256], wfile[MAX_PATH];
    ZeroMemory(&ofn, sizeof ofn);
    out[0] = 0;
    wfile[0] = 0;
    utf8_to_wide(title, wtitle, 256);
    ofn.lStructSize = sizeof ofn;
    ofn.hwndOwner   = g_hwnd;
    ofn.lpstrFilter = (const WCHAR *)filter;   /* filter stays UTF-16 literal */
    ofn.lpstrFile   = wfile;
    ofn.nMaxFile    = MAX_PATH;
    ofn.lpstrTitle  = wtitle;
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) return 0;
    wide_to_utf8(wfile, out, (int)outn);
    return 1;
}

void gui_native_open_rom(void) {
    char path[MAX_PATH];
    if (open_file_dialog("Load ROM", g_romfilter, path, sizeof path))
        if (g_cb.loadrom_path) g_cb.loadrom_path(path);
}

void gui_native_open_bios(void) {
    char path[MAX_PATH];
    static const WCHAR filter[] =
        L"BIOS files (*.bin;*.rom)\0*.bin;*.rom\0All files (*.*)\0*.*\0\0";
    if (open_file_dialog("Select BIOS File", filter, path, sizeof path))
        if (g_cb.setbios_path) g_cb.setbios_path(path);
}

void gui_native_message(const char *title, const char *body) {
    WCHAR wt[256], wb[4096];
    utf8_to_wide(title, wt, 256);
    utf8_to_wide(body, wb, 4096);
    MessageBoxW(g_hwnd, wb, wt, MB_OK | MB_ICONINFORMATION);
}

/* ------------------------------------------------------------------ init */
int gui_native_active(void)   { return g_hwnd != NULL; }
int gui_native_quit_requested(void) { return g_quit != 0; }

int gui_native_init(void *sdl_hwnd, const uint32_t *icon, int iw, int ih, GuiCallbacks *cb) {
    (void)icon; (void)iw; (void)ih;
    if (!sdl_hwnd) return 0;
    g_hwnd = (HWND)sdl_hwnd;
    g_cb = *cb;
    g_quit = 0;

    /* Long-lived SDL menus must not be recomputed while a menu is open;
     * draw the menu bar into the client area only when idle. */
    g_oldproc = (WNDPROC)SetWindowLongPtrA(g_hwnd, GWLP_WNDPROC, (LONG_PTR)gui_wndproc);
    if (!g_oldproc) { g_hwnd = NULL; return 0; }

    build_menu();
    DrawMenuBar(g_hwnd);
    gui_native_sync();
    return 1;
}

void gui_native_shutdown(void) {
    if (!g_hwnd) return;
    HMENU bar = GetMenu(g_hwnd);
    if (bar) { SetMenu(g_hwnd, NULL); DestroyMenu(bar); }
    if (g_oldproc) SetWindowLongPtrA(g_hwnd, GWLP_WNDPROC, (LONG_PTR)g_oldproc);
    g_hwnd = NULL;
    g_oldproc = NULL;
}

#endif /* _WIN32 */

#ifndef _WIN32
/* Non-Windows builds keep the software-drawn menu; provide no-op stubs so
 * the frontend can call the same entry points unconditionally. */
#include "gui.h"
int  gui_native_active(void) { return 0; }
int  gui_native_quit_requested(void) { return 0; }
void gui_native_sync(void) {}
void gui_native_message(const char *t, const char *b) { (void)t; (void)b; }
void gui_native_open_rom(void) {}
void gui_native_open_bios(void) {}
void gui_native_shutdown(void) {}
int  gui_native_init(void *h, const uint32_t *i, int w, int hh, GuiCallbacks *cb) {
    (void)h; (void)i; (void)w; (void)hh; (void)cb; return 0; }
#endif
