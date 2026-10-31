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

#endif
