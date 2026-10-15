/*
 * gui.h - optional native Win32 GUI (real menu bar, file dialogs and
 * message boxes) for the Windows build. On other platforms, or when the
 * native GUI cannot be set up, the frontend keeps its software-drawn menu.
 */
#ifndef GUI_H
#define GUI_H
#include <stdint.h>

/* Joystick configuration shared by the frontend and the Controls menu.
 * Physical buttons live in 0..JOY_MAX_BUTTONS-1; the POV (D-pad) directions
 * use sentinel values that can never collide with a button. */
#define JOY_MAX_BUTTONS 32
#define JOY_POV_UP      771
#define JOY_POV_RIGHT   772
#define JOY_POV_DOWN    773
#define JOY_POV_LEFT    774
#define JOY_IS_POV(v)   (((v) >= JOY_POV_UP) && ((v) <= JOY_POV_LEFT))

/* GBA keypad indices in the order used by the joystick mapping and the
 * Controllers Configuration dialog. Kept parallel to KP_* in main.c/win32.c. */
enum {
    JOY_KP_A = 0, JOY_KP_B, JOY_KP_SELECT, JOY_KP_START,
    JOY_KP_RIGHT, JOY_KP_LEFT, JOY_KP_UP, JOY_KP_DOWN,
    JOY_KP_R, JOY_KP_L,
    JOY_KP_COUNT
};

/* Query joystick 0 (WinMM). On success it stores the held-button bitmask
 * in *buttons and the current POV direction (one of JOY_POV_*, or 0 when
 * centred / absent) in *pov, then returns 1. Without a joystick it writes
 * zeros and returns 0. (Stub returning 0 on non-Windows.) */
int ctl_joy_raw(int *buttons, int *pov);

/* Poll joystick 0 and fold buttons + POV hat into an active-low GBA keypad
 * mask (same convention as the KEYINPUT register). keys[] must hold
 * JOY_KP_COUNT assignments, each a button index or a JOY_POV_* sentinel.
 * Returns 0x03FF (nothing pressed) when no joystick exists. */
uint16_t ctl_joy_poll(const int *keys);

typedef struct {
    void (*action)(int id);                        /* run a menu command     */
    int  (*checked)(int id);                        /* checkmark state 0/1    */
    void (*loadrom_path)(const char *path);         /* load a chosen ROM file */
    void (*setbios_path)(const char *path);         /* load a chosen BIOS file*/
    void (*pause_audio)(void);                      /* called around menus     */
    void (*resume_audio)(void);
} GuiCallbacks;

/* Attach the native menu bar to an existing window (its HWND on Windows).
 * Returns 1 on success. */
int  gui_native_init(void *hwnd, const uint32_t *icon_argb,
                     int icon_w, int icon_h, GuiCallbacks *cb);
int  gui_native_active(void);       /* is the native menu bar in use?         */
int  gui_native_quit_requested(void);/* has the window been closed?           */
void gui_native_sync(void);         /* refresh checkmarks after a state change*/
void gui_native_message(const char *title, const char *body);
void gui_native_open_rom(void);     /* open a ROM via the native file dialog  */
void gui_native_open_bios(void);    /* open a BIOS via the native file dialog */
void gui_native_shutdown(void);

#endif
