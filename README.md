# BoycottAdvance Reborn

> **Work in progress (WIP).** This emulator is under active development and is
> **not finished**. Some commercial titles still show graphics or audio
> glitches, cycle-accurate timing is still being refined, and features may
> change or break between builds. Do not expect perfect compatibility yet.
> Feedback and bug reports are welcome.

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
  `CpuSet`, `CpuFastSet`, `RegisterRamReset`, `SoftReset`, `GetBiosChecksum`.
- PPU: text modes 0-2, bitmap modes 3-5, affine backgrounds, OBJ sprites
  (4bpp/8bpp, 1D/2D mapping, flips, palettes), window/blend registers.
- APU: 4 PSG channels (square, wave, noise) + DMA Direct Sound FIFOs.
- Memory map: BIOS, EWRAM, IWRAM, I/O, palette, VRAM, OAM, ROM (+ open bus).
- Timers, DMA (all 4 channels), keypad with IRQ.
- Save states, cheats, BIOS fast-boot, screenshots.
- Selectable frame-buffer backends (see below) and selectable sound
  engines (SDL / OpenAL / DirectSound on Windows, SDL2 on Linux/macOS).

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
Direct3D 9, audio through the selected sound engine (see below) and input
through `GetAsyncKeyState` plus the WinMM joystick API. The chosen backend is
stored in `boycottadvance-reborn.ini` (`VideoBackend`); on Linux/macOS it falls
back to SDL Software when the selected backend cannot initialise.

## Sound engines

On Windows the audio output can be chosen from **Options > Sound Engine**.
All three engines are loaded from their DLL at run time, so the executable
keeps its native import table and a missing DLL only disables that engine:

| Engine | Notes |
|---|---|
| SDL | needs `SDL2.dll` next to the executable (or on the `PATH`) |
| OpenAL | needs `OpenAL32.dll` (OpenAL Soft or Creative) |
| DirectSound | native on every Windows since 98, no extra files (default) |

If the selected engine (or its DLL) is unavailable, the emulator falls back
to the built-in WinMM (`waveOut`) path so sound always works. The choice is
stored in `boycottadvance-reborn.ini` (`SoundEngine`).


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
  Emulation, Reset Emulation, Load State From Slot, Save State To Slot,
  Load State, Save State, Exit
- **Window** — Zoom x1..x4, Select FLASHROM ID, Scanline (TV Mode),
  Stretch Fit To Screen, Fullscreen
- **Options** — Frameskip (0..5), Auto Frame Skip, Smooth Emulation,
  Auto Speed Throttle, Show FPS, Show FPS in percent, Sound Enable,
  Enable DSound A/B, Enable GBC Sound, Reverse Sound Stereo,
  Sound Engine (Volumen 0..200 % / Engine SDL / OpenAL / DirectSound),
  Select BIOS File, BIOS Enable, Skip BIOS Boot, Skip Intro Hacks,
  Controllers Configuration
- **Help** — History..., Command Line Options..., About...

File > Load ROM... opens a built-in ROM browser: it lists folders and
`.gba` / `.agb` / `.bin` files, lets you step into directories (including
outside the executable's folder) with the mouse or the Up/Down/Enter keys,
and remembers the last folder you loaded a ROM from.

Keyboard: `A`=A, `S`=B, `F`=L, `D`=R, `Enter`=Start, `Backspace`=Select,
arrows = D-Pad, `F1`..`F9`=load state slot 0..8, `Shift`+`F1`..`F9`=save into
that slot, `F10`=screenshot, `F11`=fullscreen, `F12`=About, `P`=pause,
`Esc`=quit.

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

### Linux packages (.deb / .rpm)

`make packages` builds distributable packages of the Linux frontend:

```sh
make packages
# dist/boycottadvance-reborn_<version>_amd64.deb
# dist/boycottadvance-reborn-<version>-1.x86_64.rpm
```

The `.deb` needs `dpkg-deb` (from `dpkg`); the `.rpm` is built only when
`rpmbuild` is installed, otherwise it is skipped. Both install the binary and
its `gba_bios.bin` under `/usr/lib/boycottadvance-reborn/`, a launcher symlink
in `/usr/bin`, a desktop entry and a 32x32 icon.

### Android

The Android port reuses the same C99 emulation core; only the frontend and
the video/audio backends are new (OpenGL ES 2.0 for video, `AudioTrack` for
sound, a native touch-control overlay and the app's own file browser).

```sh
cd android
./gradlew assembleDebug      # -> app/build/outputs/apk/debug/app-debug.apk
```

Requirements: JDK 17+, the Android SDK with `platforms;android-34`,
`build-tools;34.0.0`, `ndk;26.1.10909125` and `cmake;3.22.1`. Point
`android/local.properties` at your SDK (`sdk.dir=...`). The APK ships the
`arm64-v8a`, `armeabi-v7a` and `x86_64` ABIs.

The port has the same features as the desktop builds (ROM and BIOS loading
through its own file browser, reset, ten savestate slots, sound toggles,
scanline/smoothing, fast-forward, ROM info) and adds on-screen GBA controls:
a D-pad, A/B, L/R and SELECT/START, all multi-touch. ROMs and BIOS images
are picked with the in-app browser, which starts in the app's folder, the
shared storage or `Download`.

The JNI layer is verified without a device: `tools/android_jni_test.sh`
compiles the very same `jni_bridge.c` for the desktop, loads it from a JVM
and checks that the frame it produces is **bit-identical** to the desktop
build's output for the same ROM.

### Running

```sh
./boycottadvance-reborn game.gba
```

Place a `gba_bios.bin` (16 KB) next to the binary for full BIOS accuracy;
without it the emulator uses a built-in fast-boot stub.

### Controls

Key mappings are configurable from **Options > Controls > Input
Configuration...**, which opens the same window the original
BoycottAdvance used: a **Keyboard** page and a **Joystick** page, one row
per GBA button, with **OK** / **Cancel**. Bindings are stored in
`boycottadvance-reborn.ini` (one `Key_<button>` / `JoyGBA_<button>` entry
per button). Defaults:

| GBA | Key |
|---|---|
| D-Pad | Arrow keys |
| A / B | `A` / `S` |
| L / R | `F` / `D` |
| Start / Select | `Enter` / `Backspace` |

Emulator shortcuts: `F1`..`F9` load save-state slots 0..8, `Shift`+`F1`..`F9`
save into that slot, `F10` screenshot, `F11` fullscreen, `F12` about,
`P` pause, `Esc` quit. **Hold `TAB` to fast-forward** while it is down, and
`-` / `=` change the output volume in 10 % steps (0..200 %).

## Version history

- **v1.4.61** - The mixer half of v1.4.59 is back on its own, without the
  pacing that broke that release. When the output ring runs dry the mixer used
  to hold the last sample for up to 186 ms before fading, which leaves a DC
  level sitting there exactly where the next loud sound begins: its attack is
  cut and the step is heard. Short gaps (under 2.2 ms) still hold the sample,
  because fading on every one of those is the constant crackle of v1.4.38;
  longer ones fade to silence in 4.4 ms and fade back in with the data.
  `tools/apu_mix_test.c` covers it (9 checks, 2 of them fail on the old code).
  Measured on Medabots AX at the "Medabots! Robattle!" line: the mix saturates
  the DAC range in 1.46 s of every 30 (the voice attack lands while the music
  already clips), so the next suspect if this does not fix it is the mixing
  level, not the delivery.
- **v1.4.60** - Reverted the audio-delivery changes of v1.4.59, which made
  things worse: the "refill the ring when it is low" pacing could skip the
  frame wait over and over, so the emulator ran unthrottled (games at 2x,
  frame skip, glitched tracks in Digimon Racing and Eragon) and the audio was
  chopped. The mixer change of that release (fade instead of holding the last
  sample) also turned a chronic underrun into dropouts. Both are gone: the
  output path is back to v1.4.58 behaviour. v1.4.59 was withdrawn. The "kjj"
  before a loud voice is still open and is being chased with a savestate of the
  exact moment plus the matching ROM.
- **v1.4.58** - The window caption is the fixed English title only:
  `BoycottAdvance Reborn v1.4.58`. The cartridge name and game code are gone
  from it, so nothing from the ROM reaches the title (they were the strings that
  came back as CJK glyphs on the affected systems), and the caption no longer
  changes at all after start-up. It is still drawn by the emulator with a named
  face (`Segoe UI`, falling back to `Tahoma`/`Arial`) because the system caption
  font on those machines maps ASCII to CJK.
- **v1.4.57** - The output volume chosen with `-` / `=` on the keyboard is
  written to the INI straight away; only the menu entries went through the
  action path that saves, so the percentage was lost on exit.
- **v1.4.56** - Back to the v1.4.49 CPU timing, plus the caption fixes.
  - **Reverted** the two timing changes that came with v1.4.50: the HLE of
    `IntrWait`/`VBlankIntrWait` no longer returns on an unrelated IRQ (it parks
    the CPU until the awaited source, as the BIOS loop does), and `SWI 02h..05h`
    no longer jump into the BIOS vector. Both advanced the game clock (games ran
    too fast - Medabots AX - and Digimon Racing ended up on a white screen).
  - Caption: drawn with the system **menu font** instead of a named "Segoe UI"
    (on a machine whose title-bar font renders ASCII as CJK, the menu bar still
    draws fine), the strip ends where the menu bar starts (measured with
    `GetMenuBarInfo`, so the themed menu bar is never covered), and the frame is
    repainted when a menu closes so the labels do not stay blank.
- **v1.4.55** - Fixes the caption strip painted by v1.4.54: the height was
  taken as "everything above the client area", and the menu bar is part of the
  non-client area, so the caption fill also covered the menu (blank blue strip,
  items reappearing only while hovered). The menu bar height is now measured
  with `GetMenuBarInfo()` and subtracted.
- **v1.4.54** - The window caption is drawn by the emulator itself (native
  Windows build). On some systems the non-client area comes out as garbage: the
  text is plain ASCII, yet the frame shows CJK glyphs and so does the taskbar
  button. The caption strip is now painted with a font the emulator creates
  (`Segoe UI`), over the system caption colours, and the title no longer carries
  the fps (it repainted the frame dozens of times a second and the OSD already
  shows it). `Help > About...` prints the caption string and the code units
  Windows stored for it. Set `OwnCaption 0` in `ps1emu.ini`/`boycottadvance.ini`
  to go back to the stock caption.
- **v1.4.53** - Digimon Racing renders its track, the halt/IntrWait SWIs run the
  real BIOS loop, and no text comes out garbled any more (the OSD, and the window
  title of both frontends).
  - Window title: the caption is now **ASCII only**. A ROM name with an accent or
    a Japanese/Chinese name used to reach the OS text layer as UTF-8 and be
    decoded with the system codepage, so `Pokemon` with an accent came out as
    `PokÃ©mon` on a Western codepage and as CJK glyphs on a Japanese or Chinese
    one. `fb_ascii_fold()` folds the accented Latin letters to their base letter
    and replaces anything else with `?`, and the native build widens the result
    to UTF-16 by hand, so no codepage conversion is involved at all.
  - `fb_text()` (`src/fb.c`) indexed `font8x8_basic`, which only holds the first
    128 code points, with the raw byte. Anything >= 0x80 (a UTF-8 `e` with an
    accent in a ROM file name, the Shift-JIS title of a Japanese cartridge) read
    past the end of the table and drew whatever global came next, which looked
    like random CJK glyphs. UTF-8 is decoded now, the accented Latin letters fold
    to ASCII (`Pokemon` stays readable) and the rest draws as `?`.
  - `g->rom_title` is sanitised to printable ASCII when the cartridge is loaded,
    so the *ROM Info* dialog cannot print raw header bytes.
  - Native Windows build: the window is Unicode (`CreateWindowExW`) but its title
    was set with `SetWindowTextA`; it now goes through `SetWindowTextW` with the
    ANSI codepage Windows actually hands us.
  - `tools/fb_text_test.c` covers the folding and the out-of-range bytes.
  - Affine backgrounds honour the 256-colour tile flag (`BGxCNT` bit 7): 64-byte
    tiles with one byte per pixel instead of 32-byte tiles with a nibble per
    pixel, so the track floor and terrain no longer used the wrong tile.
  - `aff_angle()` (`src/bios.c`) indexed the sine table with the low byte of the
    1.15 angle and `SWI 0Eh`/`0Fh` read it as `(int8_t)read8 << 8`; now the
    angle is a halfword and the index is `(angle >> 8) & 0xFF` with the sine
    negated (`cos(x+90) = -sin(x)`). The affine band of the track was rotated.
  - `SWI 02h..05h` (Halt/Stop/IntrWait/VBlankIntrWait) are BIOS loops that sleep
    in `HALTCNT`, re-check `IF` and sleep again. With a BIOS dump loaded they now
    enter vector `0x08` instead of being emulated by HLE, so the game clock
    matches hardware: the Digimon Racing race timer reads 01:36 like mGBA (it
    read 01:40 before) while frames 2200-2350 stay pixel-identical.
  - The HLE `IntrWait` no longer parks the CPU, so a VCount/HBlank handler runs
    during a `VBlankIntrWait` (Digimon Racing arms its per-scanline DMA there and
    lost the affine band otherwise).
  - Frame pacing: `frame_wait()` sleeps the frame budget and spins the last
    millisecond, because `SDL_Delay`/`Sleep` have 1 ms granularity and returned
    early (frames ran ~4 % fast). The window title now shows the game code.
  - `tools/run_gba_tests.py`: gba-tests verdicts (9 pass, 0 fail), SHA-256
    goldens of our own frames and an optional mGBA pixel-diff report.
- **v1.4.49** - Fast-forward with TAB held, and output volume.
  - Holding `TAB` runs the emulation unthrottled until the key is released
    (the existing *Auto Speed Throttle* toggle still works as a sticky
    fast-forward). SDL latches the key from its event loop; the native Win32
    build polls `VK_TAB`.
  - New *Options > Sound Engine > Volumen* submenu (0, 25, 50, 75, 100, 150,
    200 %) with the active value checked, plus `-` / `=` for 10 % steps. The
    setting is stored as `VolumePercent` in the INI.
  - The gain is applied when the samples leave the APU (`apu_set_volume()` in
    `src/apu.c`), so the callback, the fast-forward path and the WAV dumps all
    match. Above 100 % the mix saturates instead of wrapping (it can clip).
  - `tools/volume_test.c` renders the same 240 frames at 0/25/50/100/200 % from
    a fresh reset and checks every sample: 0 % silent, 50 % and 25 % exactly
    half/quarter, 200 % exactly doubled and clipped.
- **v1.4.48** - The picture follows the window size.
  - Both frontends scaled the picture with the fixed zoom (`ScreenZoom`)
    unless *Stretch Fit To Screen* was on, so resizing or maximizing the
    window left the game at its original size in the middle of the client
    area.
  - New `fb_fit_scale()` (`src/fb.c`) always fits the 240x160 picture into
    the client area keeping the 3:2 aspect: resizing, maximizing and
    fullscreen now enlarge the game. The factor snaps to a whole multiple
    from 1x up so nearest-neighbour keeps every pixel the same size;
    *Stretch Fit To Screen* fills the area exactly instead.
  - Unit test: `tools/scale_test.c`. Checked on the SDL build under Xvfb: a
    1000x700 window draws the picture at 960x640 (4x), and 1000x666 with
    stretch on, instead of the old fixed 720x480.

- **v1.4.47** - Fix the *Save State To Slot* submenu labels.
  - `build_menu()` reused one label for both slot submenus, so the save
    submenu advertised the load shortcut (`F1`..`F9`) instead of
    `Shift`+`F1`..`F9`. `slot_text()` now takes a `for_save` flag and keeps
    one buffer per variant; `sync_menu()` picks it from the item id range.
  - Verified by building both Windows targets without warnings and by
    checking the new label strings in the binaries.

- **v1.4.46** - Save-states by slot in both frontends. **UI only, the core is
  untouched** (`src/gba.c`, `src/mem.c`, `src/arm.c`, `src/ppu.c`, `src/apu.c`
  and `src/bios.c` are byte-identical to v1.4.45).
  - **Ten slots, one file each.** The state path is now `<rom>.ss<N>` for slot
    `N` in 0..9. Previously `F2`/`F3` and the *Load/Save State* items always
    used whichever slot `File > Choose State Slot` had selected, so a save and
    a load could not target two different points without going through the
    menu first.
  - **Shortcuts.** `F1`..`F9` load slots 0..8 and `Shift`+`F1`..`F9` save into
    them. Nine keys cannot address ten slots, so slot 9 stays menu-only. The
    displaced actions moved rather than disappeared: `F10` screenshot (was
    `F5`), `F12` About (was `F1`); History and fullscreen keep their menu
    entries and `F11`.
  - **Submenus with the save date.** *File > Load State From Slot* and *File >
    Save State To Slot* list all ten slots; each label carries its shortcut and,
    when the file exists, the date and time it was written (`GetFileAttributesExA`
    + `FileTimeToLocalFileTime`), or `empty`. The labels are rebuilt on
    `WM_INITMENUPOPUP`, so opening the menu shows the current times without
    any per-frame cost.
  - **Same behaviour on the SDL frontend** (`src/main.c`): same paths, same
    submenus, same keys, same slot ids. Also removed a dead `F5` branch there
    (`F5` was already taken by a duplicate `F11` handler, so History was
    unreachable from the keyboard).
  - **Verified on the real `.exe` under Wine, not just by reading code:**
    `Shift+F2` wrote `digimon.gba.ss1` and `Shift+F3` wrote `digimon.gba.ss2`
    (614496 bytes each, different contents), `F2` did not overwrite anything,
    `StateSlot` was persisted to the INI, and pressing `F1` after letting the
    game run restored the exact saved frame: **74 pixels** differ from the
    pre-save screenshot versus **54561** before loading. Note the Wine test
    environment has no Windows fonts, so menu *text* renders as blocks there;
    the item actions themselves work.
  - Builds: Linux, win64 and win32 clean; both Windows binaries still import
    only native DLLs (COMDLG32, d3d9, GDI32, KERNEL32, msvcrt, SHELL32, USER32,
    WINMM — no SDL2). Regression suite unchanged.

- **v1.4.45** - Audit of the video/audio paths against GBATEK and VBA-M, plus a
  correctness fix in the HLE of the BIOS division SWI. **No rendering or
  mixing behaviour changed**: every other reported point was verified as
  already correct, and the evidence is recorded here so it is not "fixed"
  again by mistake.
  - **`src/bios.c` `swi_div32` (real fix).** GBATEK "SWI 06h - Div" specifies
    `r0 = Number DIV Denom`, `r1 = Number MOD Denom`, `r3 = ABS(quotient)`.
    Our HLE wrote the quotient to `r1` as well, leaving `r3 = 0` on the normal
    path and on `INT_MIN / -1`; division by zero returned `r0 = dividend`
    instead of the `+-1` the ARM7TDMI leaves. Matches mGBA `_Div`
    (`src/gba/bios.c`) line for line now. Verified with 8/8 synthetic cases
    (`-1234/10 -> -123, -4, +123`, `+-x/0 -> +-1, x, 1`, `INT_MIN/-1 ->
    INT_MIN, 0, INT_MIN`).
  - **Colour effects (`src/ppu.c`) — verified identical to VBA-M.** Alpha
    blend, brighten and darken were compared exhaustively against
    `gfxAlphaBlend` / `gfxIncreaseBrightness` / `gfxDecreaseBrightness`
    (`gbaGfx.h`): **0 mismatches** over 557056 brightness cases (all 32768
    colours x 17 coefficients, both directions) and 13.7M sampled blend cases.
  - **BG2 affine in bitmap modes, affine mosaic, reference-point reload, window
    wrap, OBJ 2D wrap, scanline order** — all already implemented and matching
    GBATEK/VBA-M (`render_bitmap` applies PA/PC + mosaic and yields transparent
    outside the bitmap; `render_affine_bg` rewinds both `rx` and `ry` by the
    mosaic row; `io_write16` reloads `refx/refy` with the 28-bit sign-extended
    value on every `BG2X/Y` write, so HDMA Mode-7 effects work mid-frame;
    `in_win_x` wraps when `x1 > x2`; HBlank DMA is gated to `line < 160`).
  - **Audio — verified.** The FIFO is clocked by the real timer period
    (`fifo_timer_period`, phase in 32.32 so `STEP_FP/period` does not truncate),
    which is equivalent to sampling one byte per overflow; 8-bit register
    writes merge with the raw `io_raw` shadow instead of the masked read, so
    M4A's byte writes to `0x04000064`/`0x65` keep their bits; the FIFO reset
    clears `fifo_prev`/`fifo_interp`/`fifo_frac`; the wave length is only loaded
    on `0x72`/trigger. PSG level is `2^(vol-1)` and the final gain `x48`, as in
    mGBA. Measured on Digimon Racing, real BIOS, 600 frames: **443012 samples =
    exactly 600 frames** at 44100 Hz (738.35/frame), zero ring-buffer drops,
    peak 19200 matching mGBA sample for sample.
  - **Windows frontend** — the D3D9 path already resets the device on resize
    and handles `D3DERR_DEVICELOST`/`DEVICENOTRESET`; GDI and D3D both letterbox
    a centred 3:2 rect instead of stretching to the client; samplers are
    explicit `D3DTEXF_POINT`.
  - **Verification (headless, same core as the .exe).** Digimon Racing with the
    real BIOS, 600 frames: **0 px difference (0.00%)** against mGBA at f60,
    f120, f280, f300, f320, f360, f380, f400, f420, f450, f480, f500, f520,
    f540, f570. Frame-clock drift **+0.0000%** (168537600 cycles over 600
    frames, exactly 228 x 1232 per frame).
    Method note: mGBA's PPM dumps from `tools/mgbaharness.c` are **RGB**; the
    BGR permutation documented in `tools/fb_diff.py` must NOT be applied, or a
    pixel-perfect frame reads as 96% different.
  - **Regression suite unchanged, proved per test.** `tools/gba_test_suite.py`
    against alyosha-tas/gba-tests (133 ROMs) gives **76 PASSED / 48 FAILED /
    9 UNKNOWN / 133** both before and after this release, and diffing the full
    failure list *including the failure codes* shows **zero differences**: the
    same 48 failures with the same codes, so no test was traded for another.
    (`SUITE_ROOT=/tmp/at-tests`, `HH=/tmp/hh`.) Note that jsmolka/gba-tests,
    the other suite this repo's notes mention, only contains 13 ROMs.
  - All three targets build clean and both Windows binaries still import only
    native DLLs (COMDLG32, d3d9, GDI32, KERNEL32, msvcrt, SHELL32, USER32,
    WINMM — no SDL2).

- **v1.4.44** - Core: fixed games that ran **far too fast** (Medabots AX -
  Rokusho, and others). The HLE of the BIOS wait routines (SWI `IntrWait`/
  `VBlankIntrWait`, `src/arm.c`) released the CPU on *any* unrelated enabled
  interrupt, so when the game's own interrupt handler ran during the wait the
  HLE returned from the SWI without the awaited flag being pending. The game's
  frame loop then executed at full CPU speed and advanced many in-game frames
  per emulated frame. Measured on Medabots AX (real BIOS, same ROM, comparing
  the DISPCNT transition into the gameplay): with the bug the transition to
  `3040` happened at frame **878** vs mGBA's **1250** (~20x too fast); with the
  fix it lands at frame **1240**. On the fastboot path it now matches the
  documented baseline again (f973 vs mGBA f979). The wait now stays asleep on
  `wait_mask` until its own flag arrives, as the real BIOS loop does, and only
  a deliverable unrelated IRQ (enabled, `IME`, CPSR I clear) preempts it.
  Verified: Medabots AX real-BIOS f1240 vs mGBA f1250, Emerald/RetroRegen
  0.00% frame-identical, Digimon Racing (the v1.4.40 regression target) 0.00%
  with and without the documented key script, Fire Emblem closer to mGBA than
  before; `gba_test_suite.py` unchanged at 76 PASSED / 48 FAILED / 9 UNKNOWN.

- **v1.4.43** - Fixed the distorted (harsh, "scratchy") audio on every
  platform, including Android. `gba_reset()` zeroed the whole APU state but
  never called `apu_reset()`, the only place that sets `SOUNDBIAS` to its
  power-on value (0x0200, GBATEK "4000088h - SOUNDBIAS"). With `SOUNDBIAS = 0`
  the output stage's ADC limiter (`sample += bias; clamp to [0, 0x3FF];
  sample -= bias`, the `_applyBias` model from mGBA) clamped every negative
  half-cycle to zero, so the signal came out **unipolar**: min was always 0 and
  ~36% of the waveform was gone. That is what made the sound hard and scratchy.
  With the bias restored the output is bipolar again. Measured with the
  headless harness on the same core the executables use (Pokemon Emerald, 601
  frames): min 0 -> -7920, negative fraction 0.000 -> 0.361, RMS 966 -> 1316,
  against mGBA's -8448 / 0.350 / 1329 (RMS ratio 0.990). A new regression test
  (`tools/test_bias.c`) pins the reset value and the explicit SOUNDBIAS write.

- **v1.4.42** - Android: fixed every APK after the first one being
  uninstallable. The CI built with `assembleDebug` and a runner's default
  debug keystore is regenerated on each run, so each release was signed with a
  *different* key. Android refuses to install an update whose signature does
  not match the installed app, so the phone silently kept running the old
  build: fixes shipped in a new APK (like the v1.4.41 frame pacing) never
  took effect, and the game stayed too fast with the audio crackling. The APK
  is now always signed with the same stable key (`android/keystore/`, or the
  `ANDROID_KEYSTORE_*` secrets in CI), and the version is bumped
  (`versionCode 9`, `versionName 1.4.42`) so the update is applied.

- **v1.4.41** - Android: fixed the game running far too fast with the sound
  off (or when the audio device fails to open). The emulation loop was paced
  only by the audio device: with nothing draining the samples it free-ran at
  full CPU speed. It now enforces a wall-clock budget of 16.7427 ms per frame
  (59.7275 Hz) whenever the audio device is not the clock, so the game always
  plays at real speed. Also on Android: the top HUD bar (ROM / menu / status)
  can now be collapsed with a small on-screen arrow button, which toggles
  between a down arrow (bar visible) and an up arrow (bar hidden); the choice
  is remembered across sessions.

- **v1.4.40** - Core: fixed per-scanline effects (HDMA/VCount) being applied
  one frame late. The HLE of the BIOS wait routines (SWI `IntrWait`/
  `VBlankIntrWait`, `src/arm.c`) treated the awaited-interrupt mask as a
  hardware filter: while a `VBlankIntrWait` was outstanding only the VBlank
  flag released the CPU, so a VCount-match handler was not served until
  VBlank. Games that rewrite BG scroll from a VCount interrupt then had all
  their writes land in the VBlank line instead of the match scanlines. The
  mask is a *software* loop in the real BIOS: it HALTs, any enabled interrupt
  wakes it and runs its handler, and the loop re-HALTs until the awaited flag
  is pending. The HLE now reproduces that: it wakes on any enabled interrupt
  and keeps the wait armed until the awaited source arrives. Verified against
  mGBA with the same inputs (Digimon Racing, real BIOS): the per-scanline
  `BG0HOFS` writes now land on the same scanlines and hold the same values as
  mGBA, and the framebuffer difference at the effect frames dropped from
  ~20% of pixels to 0.00% (residual 1.4% at two frames from an unrelated
  sprite). The CPU cycle count and all other titles are unchanged; the
  regression suite stays at its baseline.

- **v1.4.39** - Audio: fixed the constant crackle introduced in v1.4.38.
  The declick fade (added in v1.4.38 to silence the pause pop) triggered on
  *any* short gap in the sample buffer, not just a real pause. With the
  producer and the consumer running at slightly different rates the ring dips
  below one segment routinely, so the fade ran continuously and modulated the
  amplitude of the output: a constant buzz while playing. It now only fades
  once the buffer has stayed empty for a whole ring (`EMPTY_PAUSE` =
  `RING_FRAMES`, ~186 ms), which is a real pause (P, a menu) and never an
  ordinary underrun; short gaps keep the plain sample-and-hold and do not
  crackle. Measured with the same core and the app's producer/consumer loop:
  under starvation the envelope dips drop back from 463/min to the baseline
  210/min (the v1.4.37 figure), while the pause still fades to exact silence
  (a held DC of -1152 becomes 0, resume step 3). The DirectSound refill
  hardening from v1.4.38 (event-driven refill, high priority, lost-buffer
  recovery) is unchanged.

- **v1.4.38** - Audio: fixed the click when pausing and resuming.
  The mixer (`apu_mix`, `src/apu.c`) repeated the last sample while the ring
  buffer was empty (a pause, or an underrun), so the output sat at a constant
  DC level; resuming then stepped back to sound with a pop. It now fades to
  silence over ~5.8 ms when the buffer runs dry and fades back up when data
  returns, like VBA-M (`audio_sdl.cpp` emits silence when its buffer is
  empty). The fade is neutral during normal playback (the ring never empties,
  so the gain stays at unity). Measured with the same core: the paused region
  goes from a held DC of -1536 to exact silence, and the resume step (3504) is
  below the waveform's own maximum step (4608). The DirectSound engine
  (`src/audio_out.c`) also no longer crackles under load or after a device
  reset: its refill thread waits on its segment events with
  `WaitForMultipleObjects` instead of polling every 5 ms, runs at
  `THREAD_PRIORITY_TIME_CRITICAL` (as the waveOut thread already did) and
  recovers a lost buffer with `Restore` on `DSERR_BUFFERLOST`. No change to
  the core loop (`apu_mix` is only called by the audio consumer, never by
  `apu_step`), so video output is untouched.

- **v1.4.37** - DMA3 video capture (Start Timing=3).
  GBATEK ("Video Capture Mode"): with Start Timing=3, DMA3 transfers `CNT_L`
  units **per scanline**, starting at VCOUNT=2 and stopping at VCOUNT=161,
  where the enable flag clears itself. The core did not handle this mode at
  all (only DMA1/DMA2 were used, as the sound FIFOs, which also use Start
  Timing=3), so any title that streams a bitmap into VRAM this way got an
  empty picture. Implemented in `src/mem.c` (`dma_run` clears the enable at
  VCOUNT=161; new `dma_capture_start`) and `src/gba.c` (per-scanline trigger).
  No regression: Digimon Racing, Pokemon Emerald and Fire Emblem are
  pixel-identical at frames 60/300/600 before and after the change.

- **v1.4.36** - BIOS SWI 0Dh (GetBiosChecksum).
  Implemented the BIOS call that returns the checksum of the BIOS ROM. It was
  missing, so titles that identify the console this way read a wrong value.
  The Retro Regen GBA Test Suite's SYSTEM ID check now reports
  `MODEL: GBA FAMILY (AGB BIOS)` instead of `MODEL: UNKNOWN BIOS`.

- **v1.4.34** - Android APK built with -O2.
  The Android port is built by Gradle with the CMake "Debug" build type, whose
  default flags are just `-g` with no optimization. The emulator core is an
  interpreter, so at `-O0` it barely reached real time on a fast desktop CPU
  (measured with the same core: 1200 frames of Digimon Racing take 18.9 s at
  `-O0`, i.e. 1.06x real time, versus 5.2 s at `-O2`, 3.85x). On a phone, which
  is slower, `-O0` falls well below 60 fps: the emulation runs behind, the
  `AudioTrack` starves (crackle/"rayado") and the frame pacing stutters
  (flicker). The desktop builds were never affected because `Makefile` and
  `Makefile.win32` already use `-O2`; only the APK shipped unoptimized. The
  native target now forces `-O2` (`android/app/src/main/cpp/CMakeLists.txt`).

- **v1.4.33** - battery saves (.sav) and per-game savestates.
  Two user-visible bugs, both rooted in the savestate format. (1) **Loading a
  savestate from another game overwrote the backup type**: the state stored
  `save_type` (and `rom_size`) and `gba_load_state` copied them straight in,
  but never restored `save_size`. Loading a state from a flash game into an
  SRAM game (or vice versa) left `save_type` and `save_size` inconsistent, so
  the next `.sav` write/read used the wrong length - the save silently stopped
  working, and a mismatched state could crash. The cartridge description
  (`save_type`/`save_size`) is now **re-derived from the loaded ROM** after a
  state load (`gba_recompute_cart`) and never taken from the file. (2) the
  state now carries the **game code + ROM size** in its header and is refused
  unless it belongs to the ROM it is loaded into. Additionally the savestates
  are now **per game**: stored next to the ROM as `<rom>.ss<slot>` instead of a
  single shared `state<N>.sav` for every title, so Load State can never pull
  another game's state. Switching ROMs flushes the previous game's battery
  before its state is replaced. State format bumped to v8.

- **v1.4.32** - savestate robustness + verified graphics/audio audit.
  Two real savestate bugs, both able to leave a **blank screen**:
  (1) `gba_load_state` never checked the file size, so a truncated or
  corrupted state (interrupted write, half copy, file from another build) was
  accepted as valid and loaded zeroed memory; each field is now bounds-checked
  against the bytes actually read and a bad file is rejected (`-1`), with a
  message in the frontends instead of a silent blank screen. (2) the Windows
  frontend built the state path as a **bare relative name**, so it landed in
  the process working directory rather than next to the executable (where the
  `.ini` already lives); Save State and Load State could then resolve to
  different files on a shortcut / drag-and-drop launch. The state now lives
  next to the executable. Also re-verified the whole graphics/audio audit
  (P1-P6, A1-A6, V1-V2) against **GBATEK**, mGBA and VBA-M with headless
  captures from the shipped core: frames 60/300/600 are pixel-identical to
  mGBA (0 px) and the audio stream is exactly 44100 Hz (443012 samples per
  600 frames = 738.36/frame). Windows builds are still SDL-free.

- **v1.4.31** - Android audio fix.
  The Android app drained a **fixed 735 samples per frame**, but the core
  produces `44100*280896/16777216 = 738.35` (the GBA frame is 280896 cycles at
  16.777216 MHz, not 1/60 s). The surplus (~3.35/frame) filled the APU ring
  buffer, and from then on `ring_push` **dropped the overflow every frame** -
  measured 7414 dropped samples over 6000 frames (~74/s), the crackle heard in
  the APK. Each frame now drains **whatever the ring actually holds** (chunked
  to the 1024-sample buffer), like the desktop frontends; measured 3 drops and
  an empty ring. The `tools/mgbaharness.c` comparison harness also now emits
  PCM (mGBA's `opts.volume` defaults to 0 and `loadConfig` reads `core->opts`),
  so core audio can be diffed against mGBA.

- **v1.4.30** - Android port.
  A working **Android app** built from the same C99 emulation core. The core
  is untouched; the port adds a portable frontend wrapper (`acore.c`, the
  settings/ROM/state/audio glue shared with the desktop), a JNI bridge, an
  OpenGL ES 2.0 video backend, `AudioTrack` sound, the app's **own file
  browser** for picking ROMs and BIOS images, and **on-screen GBA controls**
  (D-pad, A/B, L/R, SELECT/START, multi-touch). The APK ships the
  `arm64-v8a`, `armeabi-v7a` and `x86_64` ABIs. CI builds it
  (`.github/workflows/android-apk.yml`) and a JNI parity check
  (`tools/android_jni_test.sh`) proves the Android path drives a
  **bit-identical** frame to the desktop build.
  Alongside the port, the emulation core picked up several fixes:
  - **Real-time clock (Seiko S-3511)**: Pokemon Emerald/Ruby/Sapphire no
    longer show "the internal battery has run dry". The chip was detected
    but `rtc_reg` returned a masked register id (`0xC4 & 0x0E` = 4) that
    never matched the `0xC4/0xC6/0xC8` cases, so every GPIO write was
    silently dropped and no command was ever decoded. With the correct
    register id the game exchanges its four real commands (`C6` control,
    `E6` datetime, `C6`, `A6` time) and gets a valid BCD date.
  - **Audio pacing (SDL build)**: `audio_avail()` read
    `SDL_GetQueuedAudioSize`, which is always 0 when the device is opened
    in callback mode, so the throttle loop never waited and the emulator
    ran flat out. It now reads the APU ring buffer.
  - **Audio sample clock**: `apu_step` returns the cycles the FIFO DMA
    charges to `g->cycle` during `gen_sample`, so the sample count matches
    the hardware rate exactly (deficit 0.0793% -> 0%).
  - **Save states** include the RTC; the save-type heuristic uses the
    VBA-M markers (`EEPROM_` / `SRAM_` / `FLASH1M_` / `FLASH512_` /
    `FLASH`), which covers Fire Emblem's `SRAM_F_V`.

- **v1.4.27** - Linux packages and an Android port study.
  **`make packages`** builds distributable **`.deb`** and **`.rpm`** packages
  of the Linux frontend: the binary and its `gba_bios.bin` go under
  `/usr/lib/boycottadvance-reborn/`, with a launcher symlink in `/usr/bin`,
  a desktop entry and a 32x32 icon. The `.rpm` is built only when `rpmbuild`
  is available. CI now builds and publishes both packages with each release.
  Added **`docs/android-port.md`**, a feasibility study for an Android port:
  the emulation core is platform-independent C99, so the work is a new
  frontend/backends, not a core rewrite.

- **v1.4.26** - graphics fixes: several sprite/background glitches were fixed
  after an audit against GBATEK, mGBA and VBA-M.
  **BIOS affine-set fixed point (`BgAffineSet` / `ObjAffineSet`, SWI 0x0E/0x0F)**:
  the routines multiplied the 8.8 scale by the 1.15 cosine and shifted right by
  7, producing a 16.16 value that saturated to `0x7FFF`. Any non-trivial scale
  turned the OAM affine matrix into `0x7FFF` and pushed the sprites out of
  range. GBATEK (and both mGBA and VBA-M) produce an **8.8** element as
  `(sx*c)>>15`. This was the missing **"GAME FREAK presents"** logo in the
  Pokemon Emerald intro (logo pixels 2 -> 583, matching VBA-M).
  **`CpuFastSet` (SWI 0x0C) always copies 32-bit words**: it was treating bit 26
  of the control word as a 16/32-bit size selector and copying halfwords when
  clear. GBATEK is explicit that `CpuFastSet` runs in 32-byte blocks of
  `LDMIA/STMIA r2-r9` and rounds the wordcount up to a multiple of 8 words.
  Medabots AX loaded its palette with a "16-bit" `CpuFastSet`, so palette
  entries 24-63 stayed at zero and the screen diverged a stable ~12.3% from
  frame 1000 on. After the fix the palette and VRAM are byte-identical to mGBA
  and the diff is **0.00%** (frames 1000-1500).
  **Window ranges wrap (P4)**: `WIN0H/WIN1H/WIN0V/WIN1V` with `x1 > x2` /
  `y1 > y2` now wrap around the screen edge instead of collapsing to an empty
  window, as GBATEK describes and VBA-M implements.
  **Normal-mode SIO** transfers now complete on their exact deadline.
  Also adds a headless capture mode: `-video <dir> <frames> [-keys "..."]`
  renders one PNG per frame plus a stereo WAV from the same core the binary
  uses, so gameplay can be recorded (and diffed against mGBA) without a display.
- **v1.4.25** - audio during fast-forward, plus a timing refinement. **Audio with
  the throttle off**: switching "Speedup Throttle" off used to mute the sound
  (the emulation runs far ahead of real time, so the mixer dropped every sample
  to stop the ring overflowing). The sound now keeps playing: the ring is
  decimated to its newest window (`apu_drain_latest`), so long intros are still
  audible while the game runs fast. **HBlank IRQ margin**: the HBlank interrupt
  is now served 6 cycles after the nominal line boundary, matching mGBA
  (`src/gba/video.c _startHblank`, "GBARaiseIRQ(..., cyclesLate - 6)"). Without
  the margin a tight HBlank handler (per-scanline raster effects) reads its
  registers one scanline early. The DMA is not delayed, only the IRQ; the frame
  clock stays exact at 280896 cycles. Both BIOS boot paths now resolve
  `gba_bios.bin` next to the executable, not only in the working directory.
- **v1.4.24** - **Frame Skip is honoured**. The `FrameSkip` / `AutoFrameSkip`
  INI keys and the Frame Skip menu entries were read and stored but never
  consulted by the render loop, so the option did nothing. Only the blit
  (compose + present) is now skipped every Nth frame; the CPU, PPU and APU
  still run every frame, so audio and timers are unaffected. Auto raises the
  target when the previous frame missed 60 Hz, as VBA-M does. Defaults (0/0)
  keep the previous behaviour. Also adds `tools/headless.c`, which renders
  frames 60/300/600 to PNG plus a stereo WAV from the same core the binary uses.
- **v1.4.23** - video and audio fixes from a second review against GBATEK and
  VBA-M. **HBlank flag (F1)**: `DISPSTAT` bit 1 always read 0 because nothing
  ever set `ppu.hblank`; a ROM that polls it for mid-frame raster effects saw no
  HBlank at all. It is now cleared at the start of every scanline and set during
  HBlank, as GBATEK (4000004h: "toggled in all lines, 0..227") requires.
  **VBlank timing (F2)**: the VBlank IRQ, the VBlank DMA and the frame counter
  fired on line 159, one scanline early, so the game's VBlank handler (OAM /
  scroll / palette copy) ran during the last visible line and corrupted it. They
  now fire on line 160, matching GBATEK ("V-Blank flag set in line 160..226, not
  227"), mGBA (`GBA_VIDEO_VERTICAL_PIXELS`) and VBA-M (`VCOUNT == 160`).
  **Colour expansion (F5)**: 5->8 bit now replicates the top bits, so white maps
  to 0xFF instead of 0xF8, in both the blitter and the F5 screenshot.
  **Scaling (F5)**: the picture snaps to a whole multiple when it fits, so
  nearest-neighbour scaling no longer produces uneven, shimmering pixel columns.
  **Audio pacing (F3)**: the loop is paced by the sound-ring level instead of a
  coarse 16 ms `Sleep`, which overflowed the ring (dropped samples) or starved
  it (repeated last sample); fast-forward mutes the audio instead of emitting
  overflow garbage, and the waveOut refill thread runs at time-critical
  priority. Verified headless (frames 60/300/600 PNG + 5 s WAV): the jsmolka
  gba-tests and alyosha-tas suites go from 74 to 76 PASSED (`ppu/start_up_vbl_irq`
  and `ppu/start_up_vbl_irq_halt` now pass) with no regressions; audio produces
  220592 samples per 299 frames (44100.7 Hz) with `ringdrop=0`.
- **v1.4.22** - PPU and audio fixes found by a code review against GBATEK and
  VBA-M. **Affine BG in the bitmap modes (3/4/5)**: BG2 was drawn without the
  rotation/scaling matrix, so Mode-7-style effects showed the raw bitmap. It
  now goes through `PA`/`PC` around the reference point `BG2X`/`BG2Y`, with the
  mosaic grid, and pixels outside the bitmap are transparent (GBATEK "BG
  Rotation/Scaling"). **Mosaic of affine BGs**: the reference point is rewound
  along both axes to the start of the mosaic row; before only the vertical term
  was subtracted, matching mGBA's `BACKGROUND_BITMAP_INIT`. **8-bit writes to
  the sound registers** no longer lose the other byte: the read-modify-write
  used the masked read (which hides the write-only frequency/envelope bits), so
  a byte write such as `0x04000064` dropped the frequency low byte; it now
  merges with a raw shadow of the register. **Sound channel 3** no longer
  reloads its length on every `0x070` write (the length is set at `0x072` and
  on trigger). **FIFO reset** (`SOUNDCNT_H` bits 11/15) clears the whole
  resampler state, and the Direct Sound phase is now 32.32 so the resampling
  has no truncation drift. **Direct3D 9**: the device is Reset when the window
  is resized and `D3DERR_DEVICELOST`/`D3DERR_DEVICENOTRESET` are handled, so
  the picture is no longer cropped after a resize. The picture keeps a 3:2
  aspect ratio (letterboxed in black, integer scale when it fits) and uses
  point filtering instead of stretching to the whole window. The save-state
  format version is bumped to 5 (the I/O raw shadow was added).

- **v1.4.20** - fixes the Pokemon Emerald intro (the "GAME FREAK presents"
  screen was missing, Groudon and Kyogre did not appear, and the background
  was distorted). Root cause: the BIOS decompression SWIs (LZ77, run-length,
  Huffman, BitUnPack and the difference filters) charged their whole cost to
  the CPU clock in a single step — up to 385058 cycles, about 1.37 frames.
  The CPU jumped hundreds of scanlines at once and the HBlank/VBlank
  interrupts of those lines were never dispatched, so the game lost its
  scanline timing and the intro animation desynchronised. The cost is now
  drained across the frame (the total is identical to mGBA, which re-issues
  the SWI every 18 cycles through its `biosStall`), so the PPU keeps the pace
  and each line interrupt lands on its scanline. Measured cycle drift fell
  from +306848 cycles per frame to 0-2. Verified against mGBA: frames 400 and
  1000 are pixel-identical in BIOS boot, and frame 2420 (the red scene) now
  matches mGBA instead of showing a flat background with no creature. The
  save-state format version is bumped to 3 (the CPU state grew four bytes).

- **v1.4.19** - verification release, no emulation changes. The PPU is
  confirmed bit-exact against mGBA: injecting mGBA's exact VRAM/PAL/OAM
  state and I/O registers into our renderer reproduces its frame with 0
  differing pixels, on both Pokemon Emerald and Medabots AX, under the HLE
  fast boot and a real BIOS boot. The remaining frame difference on Emerald
  is state written by the CPU (a boot-path timing artefact), not a render
  bug. Also re-confirms the LCD window registers at the GBATEK offsets
  (WIN0H/WIN1H/WIN0V/WIN1V = 0x040/0x042/0x044/0x046, WININ/WINOUT =
  0x048/0x04A) and corrects an internal note that still documented the
  shifted mapping of the v1.4.13 regression.

- **v1.4.18** - 2D OBJ character mapping now wraps the tile column inside the
  32-tile block that holds the sprite's base tile, instead of carrying into the
  following block. A sprite whose base tile sits near a block boundary read the
  wrong tile on one side. Found and fixed against the mGBA cinema test
  `obj/2d-wrap` (128 wrong pixels -> 0); the 1D path used by Pokemon Emerald is
  unchanged. Verified against the fleroviux gba-tests suite with no regression.

- **v1.4.17** - the PSG (pulse/noise/wave) channels were mixed at half the
  level of mGBA, so menu blips and sound effects sounded too quiet against the
  Direct Sound. The master volume (SOUNDCNT_H bits 0-1) now applies the same
  gain mGBA uses (25% => x0.5, 50% => x1, 100% => x2, 200% => x4) and the final
  output gain is x48 like mGBA's `GBAAudioSample`. On a synthetic PSG tone the
  AC RMS now matches mGBA within 2% at every duty (was 0.65x). Verified against
  the fleroviux gba-tests suite with no regression.

- **v1.4.16** - the Direct Sound crackle is fixed. The Direct Sound FIFO
  is now resampled with linear interpolation, matching mGBA. The 8-bit DAC
  clocked by a timer (13.4 kHz in Pokemon Emerald) was previously played back
  by repeating each byte at 44100 Hz, a sample-and-hold with no filter that
  injected high-frequency energy at every step - the audible "scratchy" audio.
  On a 5-minute Pokemon Emerald capture the amplitude jumps over 8000 fall
  from 3568 to 0, in line with mGBA (973), and the RMS now matches. The FIFO
  is clocked by its timer at the sample level, and WAITCNT is decoded per
  GBATEK. The frame loop uses absolute per-line cycle deadlines. The default
  is now a real BIOS boot (HLE fast boot is opt-in). Verified against the
  fleroviux gba-tests suite with no regression.

- **v1.4.15** — CPU, memory and save fidelity pass, plus a native audio fix.
  The SPSR access rules now follow GBATEK (MRS from SPSR in User or System
  mode returns the CPSR, MSR to SPSR is ignored there, and the mode field
  bit 4 reads back as 1 for the invalid `0xxnnb` encodings). The ARM7 BIOS
  prefetch latch is modelled: GBATEK documents that the CPU retains the word
  after the last instruction fetched inside the BIOS and returns it for a
  BIOS read issued from outside the region, so HLE SWIs pin it like the real
  vector. Thumb `POP {}` applies the ARM7TDMI quirk (transfers R15, `SP +=
  0x40`). DMA now costs the real bus time per GBATEK (`2N+2(n-1)S+xI`) and
  those cycles are charged to the timers, which keep counting while the DMA
  holds the bus. The first layer of the bus cycle model landed: WAITCNT
  derived waitstate tables and the GamePak prefetch buffer, so a straight-line
  fetch is sequential and a branch pays the non-sequential cost (GBATEK
  "Memory Map"); it is deliberately neutral for rendering. The flash `data#`
  polling inversion was removed, so a programmed byte reads back as written
  and an erased sector as `FFh` (GBATEK "GBA Cart Backup Flash ROM"). On
  Windows, waveOut now refills from a worker thread instead of the `WOM_DONE`
  callback (MSDN forbids waveOut calls from the callback), which removes
  crackling on drivers that corrupt the stream. Verified with the fleroviux
  gba-tests CPU/PPU suite (75/133 passing) and by keeping Pokemon Emerald,
  Fire Emblem and Medabots AX bit-identical frame to frame.

- **v1.4.14** — fixes the v1.4.13 window regression and the PSG duty table.
  v1.4.13 had shifted the window registers by four bytes
  (`WIN0H/WIN1H/WIN0V/WIN1V` at `0x40/42/44/46`, `WININ/WINOUT` at `0x48/4A`),
  corrupting every windowed layer: dialogue boxes, menus and fade transitions
  in Pokemon Emerald, Medabots AX and Fire Emblem. They are back at the GBATEK
  addresses (`WIN0H/WIN1H/WIN0V/WIN1V` at `0x40/42/44/46`, `WININ/WINOUT` at
  `0x48/4A`). The PSG wave duty patterns follow GBATEK (the pulse starts at
  the first step); the old Pan Docs table produced split pulses and gave menu
  blips the wrong timbre. OBJ tiles in bitmap modes 3-5 index the 16K OBJ area
  at `0x06014000` with a 9-bit slot (no underflow), and the DISPSTAT VBlank
  flag is set on lines 160-226 only, not 227 (GBATEK).

- **v1.4.13** — the OBJ character base in bitmap modes and the window
  registers. OBJ tiles 512-1023 in modes 3/4/5 are fetched from OBJ VRAM
  `0x06014000` (GBATEK) instead of wrapping to `0x06010000`, and the window
  registers were moved to the GBATEK addresses (this is the shift v1.4.14
  corrected). Adds the `synt_our`/`synt_ref` framebuffer comparison harnesses
  and `gen_blend_test.py` (synthetic mode 3/4 blend/BLDY test ROMs).


- **v1.4.12** — the OBJ mosaic effect now renders per GBATEK: the
  per-object mosaic bit (`attr0` bit 12) and the OBJ H/V block sizes from
  the MOSAIC register (bits 8–15) are honoured, the grid is aligned to the
  screen and each block is colorized by its upper-left covered pixel,
  matching mGBA's software renderer.
- **v1.4.11** — games no longer run too fast, and the BIOS wait
  interrupts wake on the right event. The BIOS SWIs (decompression,
  `CpuSet`, `Div`) and the DMA channels charged no CPU cycles, so loaders
  that copy large amounts of data finished almost instantly: Medabots AX
  Rokusho entered gameplay on emulated frame 18 while mGBA needs 979.
  SWI cycle costs and per-transfer DMA cycles are now accounted as GBATEK
  describes ("CpuSet", "Div", "DMA Transfers"), and Medabots loading now
  reaches gameplay on frame 973 with frames 100-899 identical to mGBA.
  Pokemon Emerald follows the reference (576/600 identical DISPCNT
  frames, steady-state 100%). `IntrWait`/`VBlankIntrWait` now sleep
  until only the awaited interrupt flag is raised: an unrelated IRQ no
  longer re-enters the BIOS wait loop every scanline, keeping the
  perceived clock in step with hardware. Verified with the fleroviux
  gbatests CPU/PPU suite (99.6-100% identical VRAM vs mGBA).

- **v1.4.10** — graphics fix for Pokemon Emerald: vertical white/green
  bands over the scene. The BIOS decompression SWIs wrote one halfword
  per output byte in their VRAM variants and stepped the destination by
  two bytes, spreading the stream over twice the range and leaving the
  odd byte of every halfword unwritten (VRAM ended up full of `0x0011`
  words, i.e. a one-pixel grid). GBATEK "LZ77UnCompVram" packs two bytes
  into each halfword, so a shared destination helper now packs the byte
  pairs for LZ77 (`0x11`/`0x12`), run-length (`0x14`/`0x15`, with the
  trailing halfword padding) and the difference filters, whose dispatch
  also passed the wrong width flags (`0x16` is 8-bit to WRAM, `0x17`
  8-bit to VRAM, `0x18` 16-bit to 16-bit). Verified by diffing against
  mGBA on the same ROM: VRAM differing bytes at frame 900 went from
  30905 to 0, and the framebuffer shows the scene.

- **v1.4.9** — fix for the console input and the window feature
  registers. The keypad mask is now kept active-low exactly like the
  real `KEYINPUT` register (a double inversion used to report "all
  buttons held" when nothing was pressed, which made a directional pad
  act as permanently pressed). `WIN0H`/`WIN1H` clamp `X2 > 240` or
  `X1 > X2` to `X2 = 240`, and `WIN0V`/`WIN1V` clamp `Y2 > 160` to
  `Y2 = 160`, matching the on-chip window hardware (GBATEK "LCD I/O
  Window Feature").

- **v1.4.8** — fidelity pass over a clean re-implementation of the
  GBA model, checked against GBATEK. The PPU register map for the
  affine/rotation, window and blend registers is corrected, BG2/BG3
  internal coordinates are kept at 28 bits, and DISPSTAT reports a
  real HBlank flag. The APU was rewritten around the GBATEK register
  layout: PSG channels run on exact `16*(2048-f)` / `8*(2048-f)`
  clock dividers, the LFSR uses the `32`/`64*r << s` term, the FIFOs
  are clocked by timer overflows, and the master volume now applies
  SOUNDCNT_L. The BIOS `IntrWait` (SWI 0x04) implements the real
  `clear`/`irq` semantics, `Div` (SWI 0x06) no longer has undefined
  behaviour on `0x80000000 / -1` or `x / 0`, ARM and Thumb multiplies
  only update N and Z (leaving C/V intact), and `MSR SPSR` is blocked
  in USR/SYS mode where no SPSR exists. Save/state and the parallel
  flash chip (byte program to the real destination, immediate bank
  switch) are hardened, and the Windows binaries remain native (no
  SDL2). Verified with Pokemon Emerald in fastboot and real-BIOS boot:
  the display comes up (DISPCNT `1F40`) and the frame loop is stable.
- **v1.4.7** — the BIOS decompression SWIs are implemented (`0x10`
  BitUnPack, `0x11/0x12` LZ77 / LZ77UnCompVram, `0x13` Huffman,
  `0x14/0x15` RLE, `0x16/0x17/0x18` difference filters). They were the
  last missing piece of the boot path: Pokemon Emerald loads its whole
  graphics set through `LZ77UnCompVram` (`SWI 0x12`) on the frame after
  the BIOS hands off, and with no handler the transfer wrote nothing, so
  VRAM stayed empty and the screen went black right after the logo. With
  the decompressors in place the intro runs: DISPCNT and the framebuffer
  now track a reference emulator through the whole boot animation
  instead of freezing on a blank screen. Also lands the parallel flash
  backup chip (JEDEC autoselect, program, sector/chip erase, bank
  switch) that Emerald probes at startup.
- **v1.4.6** — the black screen after the logo is fixed. The BIOS
  `Halt` / `IntrWait` helper suspends the CPU by writing HALTCNT at
  `0x04000301`, but the address test used the wrong mask
  (`0x0FFFFFFF` leaves `0x4000301`), so the write was treated as an
  ordinary byte, the CPU never slept, and the BIOS spun forever in its
  halt loop instead of jumping into the cartridge. With the mask fixed
  the BIOS hands off to the ROM: the display-control sequence now
  matches a reference emulator exactly and the game turns the screen on.
  Also: a halted CPU wakes as soon as an enabled IRQ is pending, DISPSTAT
  reports the live VBlank/HBlank/VCount bits, and VBlank/HBlank-scheduled
  DMA (with correct repeat-address reload) runs each line. On Windows,
  closing the window no longer leaves the process behind.
- **v1.4.5** — backup memory. Save type is now detected from the marker
  strings Nintendo's save drivers embed in the ROM image (`EEPROM_V`,
  `SRAM_V`, `FLASH_V`, `FLASH512_V`, `FLASH1M_V`) instead of always
  reporting "none". Serial EEPROM is implemented: the 4Kbit/64Kbit chip
  is bit-serial on the D0 line and only reachable through DMA, so the DMA
  transfer count selects the protocol phase (9/17 read address, 68 read
  data, 73/81 write, 1 ready poll) and the address width is locked in on
  the first command. Medabots (Metabee and Rokusho) keep their save on
  EEPROM and used to stall in the backup driver before the title screen.
  Battery saves are now written to `<rom>.sav` on exit and on reset and
  read back when the ROM is opened.
- **v1.4.4** — a CPU core fix. The ARM barrel shifter applied the
  *immediate* rules to register-specified shifts, so a shift count of 0
  was treated as `#32` (`LSR`/`ASR`) or `RRX` (`ROR #0`). On ARM a
  register-specified shift with count 0 performs no shift and leaves the
  carry untouched. The BIOS decompressor (`LSR ip, fp, r4` with `r4=0`)
  was the first casualty: it corrupted the low bits of every word it
  wrote to VRAM, so the boot image and the game's initial data came out
  wrong. Fixed by giving `barrel_shift()` a `reg` flag and returning the
  value unchanged when `reg && count == 0`. The first I/O divergence
  against a reference implementation (write #99742, frame 1) is gone, and
  the real BIOS now boots the ROM.
- **v1.4.3** — two core fixes that unblock commercial titles at boot.
  `MSR` to `CPSR` changed the CPU mode without banking `r13`/`r14`, so the
  real BIOS inherited a stale `lr` and looped forever at `0x120`; it now
  routes through the same mode switch an exception return uses. And fast
  boot now starts from the register state the real BIOS leaves after POST
  (forced blank, VBlank armed, `POSTFLG` set, BIOS default IRQ vector), so
  games that defer their display writes — Pokemon Emerald among them — no
  longer deadlock waiting for a VBlank handler that could never run. Emerald
  now applies `DISPSTAT=0x9628`, arms its own interrupts and runs its main
  loop instead of resetting to the BIOS.
- **v1.4.2** — **File > Choose State Slot** now lists the ten save-state
  slots (`Slot 0` … `Slot 9`) and checks the active one, matching the
  original BoycottAdvance; `Load State` / `Save State` use the selected
  slot and the choice persists as `StateSlot` in
  `boycottadvance-reborn.ini`.
- **v1.4.1** — the Controls menu now matches the original
  BoycottAdvance: **Options > Controls > Input Configuration...** opens a
  single window with a **Keyboard** page and a **Joystick** page, one row
  per GBA button (`Button A`, `Button B`, `Button L`, `Button R`,
  `SELECT`, `START`, `UP`, `DOWN`, `LEFT`, `RIGHT`) plus **OK** / **Cancel**,
  instead of one menu entry per button. Also fixed the Windows config
  file name — it wrote the old `BoycottAdvance.ini` and now reads/writes
  `boycottadvance-reborn.ini` like the rest of the project — and the
  Windows binaries ship as a GUI application (no console window).
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

