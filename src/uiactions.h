/*
 * uiactions.h - menu command identifiers shared by the frontend (main.c)
 * and the native Win32 GUI (gui.c). Keeping them in one place lets both
 * drive the same actions.
 */
#ifndef UIACTIONS_H
#define UIACTIONS_H

enum {
    M_LOADROM = 1, M_ROMINFO, M_AUTORUN, M_RUNPAUSE, M_RESET, M_STATE_SLOT,
    M_LOADSTATE, M_SAVESTATE, M_EXIT,
    M_ZOOM1, M_ZOOM2, M_ZOOM3, M_ZOOM4, M_FLASHID, M_SCANLINE, M_FIT, M_FULLSCREEN,
    M_SKIP0, M_SKIP1, M_SKIP2, M_SKIP3, M_SKIP4, M_SKIP5, M_AUTOSKIP, M_SMOOTH,
    M_THROTTLE, M_SHOWFPS, M_SHOWFPSPCT, M_SOUND, M_DSOUNDA, M_DSOUNDB, M_GBCSOUND,
    M_REVSTEREO, M_SELBIOS, M_BIOSEN, M_BIOSBOOT, M_SKIPINTROS, M_CONTROLS,
    M_HISTORY, M_CMDLINE, M_ABOUT,
    /* Frame Buffer backend items (contiguous) */
    M_VB_AUTO, M_VB_SOFT, M_VB_GL, M_VB_VK, M_VB_GDI, M_VB_D3D,
    /* Controls remap items (contiguous, one per keypad button) */
    M_KEY0
};

/* One past the last remap item (10 keypad buttons: A B Select Start
 * Right Left Up Down R L). Kept as a literal so this header does not
 * clash with the KP_COUNT enum constant in main.c. */
#define M_KEY0_END (M_KEY0 + 9)

/* Joystick remap items (contiguous, one per keypad button). */
enum { M_JOY0 = M_KEY0 + 10 };
#define M_JOY0_END (M_JOY0 + 9)
/* Placed after the remap block so it cannot collide with a "Map ..." item. */
enum { M_RESETJOY = M_JOY0_END + 1 };
/* Opens the modal Input Configuration dialog (Keyboard / Joystick pages). */
enum { M_INPUTCFG = M_RESETJOY + 1 };
/* "Choose State Slot" submenu: ten contiguous items, radio-checked. The
 * original BoycottAdvance exposed the same slot list. */
enum { M_SLOT0 = M_INPUTCFG + 1 };
#define M_SLOT_COUNT 10
#define M_SLOT_LAST  (M_SLOT0 + M_SLOT_COUNT - 1)

#endif
