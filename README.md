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

Emulator shortcuts: `F2` save state, `F3` load state, `F5` screenshot,
`P` pause, `F1` about, `F9` history, `F11` fullscreen, `Esc` quit.

## Version history

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

