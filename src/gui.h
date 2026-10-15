/*
 * gui.h - optional native Win32 GUI (real menu bar, file dialogs and
 * message boxes) for the Windows build. On other platforms, or when the
 * native GUI cannot be set up, the frontend keeps its software-drawn menu.
 */
#ifndef GUI_H
#define GUI_H
#include <stdint.h>

typedef struct {
    void (*action)(int id);                        /* run a menu command     */
    int  (*checked)(int id);                        /* checkmark state 0/1    */
    void (*loadrom_path)(const char *path);         /* load a chosen ROM file */
    void (*setbios_path)(const char *path);         /* load a chosen BIOS file*/
    void (*pause_audio)(void);                      /* called around menus     */
    void (*resume_audio)(void);
} GuiCallbacks;

/* Bring up the native GUI on an existing SDL window. Returns 1 on success. */
int  gui_native_init(void *sdl_hwnd, const uint32_t *icon_argb,
                     int icon_w, int icon_h, GuiCallbacks *cb);
int  gui_native_active(void);       /* is the native menu bar in use?         */
int  gui_native_quit_requested(void);/* has the window been closed?           */
void gui_native_sync(void);         /* refresh checkmarks after a state change*/
void gui_native_message(const char *title, const char *body);
void gui_native_open_rom(void);     /* open a ROM via the native file dialog  */
void gui_native_open_bios(void);    /* open a BIOS via the native file dialog */
void gui_native_shutdown(void);

#endif
