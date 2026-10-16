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
| No sound on Windows 11 | Used the obsolete `WINMM` (`waveOut*`) API | SDL2 audio backend on Linux/macOS; native WinMM (`waveOut`) on Windows |
| Pokemon (and other commercial titles) hang on a white screen | Incomplete ARM core (~80% ARM / 95% Thumb) and broken BIOS interrupt chain (`VBlankIntrWait`, `CpuSet`) | Complete ARM7TDMI core + correct SWI/IRQ emulation |

## Features

- Complete ARM7TDMI interpreter: ARM and Thumb instruction sets, all
  condition codes, banked registers, SVC/IRQ/ABT/UND/FIQ modes.
- Correct GBA interrupt model (`IE`/`IF`/`IME`), HBlank/VBlank/VCount IRQs.
- BIOS SWI emulation: `Halt`, `IntrWait`, `VBlankIntrWait`, `Div`, `Sqrt`,
  `CpuSet`, `CpuFastSet`, `RegisterRamReset`, `SoftReset`.
- PPU: text modes 0-2, bitmap modes 3-5, affine backgrounds, OBJ sprites
  (4bpp/8bpp, 1D/2D mapping, flips, palettes), window/blend registers.
- APU: 4 PSG channels (square, wave, noise) + DMA Direct Sound FIFOs.
- Memory map: BIOS, EWRAM, IWRAM, I/O, palette, VRAM, OAM, ROM (+ open bus).
- Timers, DMA (all 4 channels), keypad with IRQ.
- Save states, cheats, BIOS fast-boot, screenshots.
- Selectable frame-buffer backends (see below) and audio (SDL2 on
  Linux/macOS, native WinMM on Windows).

## Frame buffer backends

The whole UI (scaled image, menu bar, dialogs, FPS overlay) is composed in
software and presented through one backend, chosen from
**Options > Frame Buffer**:

| Backend | Platform | Notes |
|---|---|---|
| Auto (SDL) | Linux/macOS | SDL accelerated renderer, software fallback |
| Software (SDL) | Linux/macOS | CPU renderer, no GPU needed |
| OpenGL (SDL) | Linux/macOS | SDL OpenGL renderer |
| Vulkan | Linux/macOS builds | native swapchain; falls back to SDL Software if unavailable |
| GDI | Windows | native `StretchDIBits` blit, no extra runtime files |
| Direct3D 9 | Windows | native textured blit via `d3d9` |

On Windows the build links **no SDL at all**: video goes through GDI or
Direct3D 9, audio through WinMM (`waveOut`) and input through
`GetAsyncKeyState` plus the WinMM joystick API. The chosen backend is
stored in `boycottadvance-reborn.ini` (`VideoBackend`); on Linux/macOS it falls
back to SDL Software when the selected backend cannot initialise.


## GUI

On Windows, BoycottAdvance Reborn uses a **native Win32 menu bar**
attached to the emulator window — the same look as the original
BoycottAdvance 0.2.8 — with native *Open ROM* / *Select BIOS* file
dialogs and native message boxes, so the menu does not steal space from
the game view. On other platforms the frontend composes the same menu bar
in software.

BoycottAdvance Reborn reproduces the original 0.2.8 menu bar (the CPU
debugger is intentionally omitted). Settings are stored in
`boycottadvance-reborn.ini`.

- **File** — Load ROM..., ROM Information..., Auto Run, Run/Pause
  Emulation, Reset Emulation, Choose State Slot, Load State, Save State, Exit
- **Window** — Zoom x1..x4, Select FLASHROM ID, Scanline (TV Mode),
  Stretch Fit To Screen, Fullscreen
- **Options** — Frameskip (0..5), Auto Frame Skip, Smooth Emulation,
  Auto Speed Throttle, Show FPS, Show FPS in percent, Sound Enable,
  Enable DSound A/B, Enable GBC Sound, Reverse Sound Stereo,
  Select BIOS File, BIOS Enable, Skip BIOS Boot, Skip Intro Hacks,
  Controllers Configuration
- **Help** — History..., Command Line Options..., About...

File > Load ROM... opens a built-in ROM browser: it lists folders and
`.gba` / `.agb` / `.bin` files, lets you step into directories (including
outside the executable's folder) with the mouse or the Up/Down/Enter keys,
and remembers the last folder you loaded a ROM from.

Keyboard: `A`=A, `S`=B, `F`=L, `D`=R, `Enter`=Start, `Backspace`=Select,
arrows = D-Pad, `F1`=About, `F2`/`F3`=save/load state, `F5`=screenshot,
`F9`=History, `F11`=fullscreen, `Esc`=quit.

## Building

Requirements: a C99 compiler and `make`; SDL2 development files are needed
for the Linux/macOS frontend only (the Windows build is SDL-free).

```sh
make            # Linux/macOS: SDL2 frontend -> ./boycottadvance-reborn
make clean
```

On Debian/Ubuntu: `sudo apt-get install build-essential libsdl2-dev`

### Windows (native, no SDL2)

The Windows build uses a MinGW-w64 cross toolchain and links only native
libraries (`gdi32`, `d3d9`, `winmm`, `comdlg32`, `user32`):

```sh
make -f Makefile.win32                 # x86_64 -> boycottadvance-reborn-x86_64.exe
make -f Makefile.win32 ARCH=i686       # i686   -> boycottadvance-reborn-i686.exe
```

On Windows with MSYS2/MinGW: `pacman -S mingw-w64-x86_64-gcc`

### Running

```sh
./boycottadvance-reborn game.gba
```

Place a `gba_bios.bin` (16 KB) next to the binary for full BIOS accuracy;
without it the emulator uses a built-in fast-boot stub.

### Controls

Key mappings are configurable from **Options > Controls** and stored in
`boycottadvance-reborn.ini` (one `Key_<button>` entry per button). Defaults:

| GBA | Key |
|---|---|
| D-Pad | Arrow keys |
| A / B | `A` / `S` |
| L / R | `F` / `D` |
| Start / Select | `Enter` / `Backspace` |

Emulator shortcuts: `F2` save state, `F3` load state, `F5` screenshot,
`P` pause, `F1` about, `F9` history, `F11` fullscreen, `Esc` quit.

## Version history

- **v1.4.0** — the Windows x86/x64 build no longer uses SDL2 at all. It
  now relies solely on the native APIs the original BoycottAdvance used:
  a GDI/Direct3D 9 window, WinMM (`waveOut`) audio, `GetAsyncKeyState`
  input and the full native Win32 menu bar (every original menu command
  except Debug). The joystick mapping now covers the D-pad/POV hat, so
  the four directions can be bound to the hat as well as to buttons.
- **v1.3.2** — the Windows build now uses the original BoycottAdvance
  look: a real Win32 menu bar attached to the window plus native *Open
  ROM* / *Select BIOS* dialogs and message boxes (the software menu bar
  remains on other platforms).
- **v1.3.1** — three core CPU/IO fixes that make real games boot and
  render: the Thumb decoder mishandled *add/subtract* (format 2,
  `0x1800-0x1FFF`) and *mov/cmp/add/sub immediate* (format 3,
  `0x2000-0x3FFF`), so `ADD Rd,#imm` and register `ADD/SUB` silently did
  the wrong thing (e.g. a `word-copy` loop never advanced, hanging the
  ROM); `MSR` with an **immediate** operand (`0x0320F000`) was not
  recognised, so the condition-flag tests failed; and the video I/O
  registers were offset by two bytes (`DISPSTAT`, `VCOUNT`, `BG0CNT`…
  `BG3CNT`), which broke VBlank detection and every background layer.
  Verified with the jsmolka ARM/Thumb/Memory/BIOS suites ("All tests
  passed") and the PPU `shades`/`stripes` tests.
- **v1.3.0** — fixed the `MLA`/`MUL` destination-register decode (the
  multiply result landed in the wrong register, so address maths — and
  therefore almost every game — rendered wrong or nothing at all); fixed
  the APU envelope clock and square/noise/wave mixing, and added DMA
  Direct Sound FIFOs; configurable key mappings saved to the `.ini`;
  new **Options > Frame Buffer** backends (SDL Software/OpenGL, Windows
  GDI and Direct3D 9, Vulkan).
- **v1.2.0** — ARM `LDR`/`LDM` with PC, PPU bitmap gating, built-in ROM
  browser.
- **v1.0.0** — first public clean-room release.

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

