# BoycottAdvance Reborn

A modern, clean-room reconstruction of the **BoycottAdvance 0.2.8** Game Boy
Advance emulator (originally written in C by Julien Frelat "Gollum", 2003).

The original source was never released (the project was distributed as
*cardware* — binaries only). This project reconstructs the emulator from
scratch: the reverse-engineered architecture of the original binary guided
the design, but every line of code here is newly written.

## Why a "Reborn" version

The original 0.2.8 build has two well-known problems on modern systems:

| Problem | Cause in 0.2.8 | Fix in Reborn |
|---|---|---|
| No sound on Windows 11 | Used the obsolete `WINMM` (`waveOut*`) API | SDL2 audio backend |
| Pokemon (and other commercial titles) hang on a white screen | Incomplete ARM core (~80% ARM / 95% Thumb) and broken BIOS interrupt chain (`VBlankIntrWait`, `CpuSet`) | Complete ARM7TDMI core + correct SWI/IRQ emulation |

## Features

- Complete ARM7TDMI interpreter: ARM and Thumb instruction sets, all
  condition codes, banked registers, SVC/IRQ/ABT/UND/FIQ modes.
- Correct GBA interrupt model (`IE`/`IF`/`IME`), HBlank/VBlank/VCount IRQs.
- BIOS SWI emulation: `Halt`, `IntrWait`, `VBlankIntrWait`, `Div`, `Sqrt`,
  `CpuSet`, `CpuFastSet`, `RegisterRamReset`, `SoftReset`.
- PPU: text modes 0-2, bitmap modes 3-5, affine backgrounds, OBJ sprites
  (4bpp/8bpp, 1D/2D mapping, flips, palettes), window/blend registers.
- APU: 4 PSG channels (square, wave, noise) + DMA sound FIFOs.
- Memory map: BIOS, EWRAM, IWRAM, I/O, palette, VRAM, OAM, ROM (+ open bus).
- Timers, DMA (all 4 channels), keypad with IRQ.
- Save states, cheats, BIOS fast-boot, screenshots.
- SDL2 video and audio frontend (works on Windows 11, Linux, macOS).

## Building

Requirements: a C99 compiler, SDL2 development files, `make`.

```sh
make            # builds ./boycottadvance-reborn
make clean
```

On Debian/Ubuntu: `sudo apt-get install build-essential libsdl2-dev`
On Windows with MSYS2/MinGW: `pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-SDL2`

### Running

```sh
./boycottadvance-reborn game.gba
```

Place a `gba_bios.bin` (16 KB) next to the binary for full BIOS accuracy;
without it the emulator uses a built-in fast-boot stub.

### Controls

| GBA | Key |
|---|---|
| D-Pad | Arrow keys |
| A / B | `A` / `S` |
| L / R | `F` / `D` |
| Start / Select | `Enter` / `Backspace` |
| Save state | `F2` |
| Load state | `F3` |
| Screenshot | `F5` |
| Pause | `P` |
| About | `F1` |
| History | `F9` |
| Quit | `Esc` |

## Credits and license

This is a **clean-room reimplementation**. It is not derived from the
original BoycottAdvance source (which was never published). All credit for
the original emulator, its design and its GBA research goes to:

- **Julien Frelat "Gollum"** — main emulation code, GBA core, portable
  architecture, GUI, Win32 port, documentation.
- **Gollum, Anarko, Fitzy and the BoycottAdvance contributors** — additional
  opcodes, palette/OAM work, BIOS functions, flash support.

The original BoycottAdvance was released as *cardware*: if you liked it, the
author asked you to send a postcard. Please keep that spirit and credit the
original authors.

BoycottAdvance Reborn is provided as-is for preservation and educational
purposes. No original ROMs or copyrighted BIOS files are included.

