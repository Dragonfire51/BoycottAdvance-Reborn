#ifndef EXPORT_H
#define EXPORT_H

/* Headless evidence run: drive the real emulator core (the same gba.c/ppu.c/
 * apu.c used by the GUI build) with no SDL window or audio device, writing
 * PNG screenshots and a WAV capture.  Used to prove behaviour changes with
 * artefacts produced by exactly the code that ships in the .exe.
 *
 *   rom     ROM to run
 *   bios    BIOS dump path, or NULL to boot the HLE fastboot path
 *   outdir  directory to write into (must exist)
 *   frames  total frames to emulate
 *   pngs    array of frame numbers to capture (1-based: after N frames)
 *   npng    number of entries in pngs
 *   wavsec  seconds of audio to write (0 disables the WAV)
 *
 * Returns 0 on success. */
int export_run(const char *rom, const char *bios, const char *outdir,
               int frames, const int *pngs, int npng, double wavsec);

/* Sequence export for video assembly: one PNG per frame (frame_000000.png
 * ...) plus a WAV of the whole run.  Uses write_png so the frames come from
 * exactly the same core as the interactive build.
 *
 * keys is an optional script of button presses so a recording can drive the
 * game past menus into gameplay.  Syntax: comma-separated
 *   NAME@FRAME[:HOLD]
 * where NAME is one of A B SELECT START RIGHT LEFT UP DOWN R L, FRAME is the
 * 1-based frame to press it on and HOLD (default 8) is how many frames it is
 * held.  Example: "START@120,A@400:4,START@600".  NULL disables input. */
int export_video(const char *rom, const char *bios, const char *outdir,
                 int frames, const char *keys);

#endif
