****************************************************
*                                                  *
*         BOYCOTTADVANCE VERSION 0.2.8             *
*           General User Documentation             *
*                                                  *
****************************************************


Abbreviations used
------------------

AGB/GBA : GameBoy Advance

GUI : Graphical User Interface


Technical documentation
-----------------------

Platforms supported : - Windows 95/98/2000/NT/Me/XP (with or without DirectX)
		      - BeOS R5 (i386)
		      - Linux (RedHat 7.0)
                      - MacOS (Mac OS X)
		      - FreeBSD (4.4)
		      - Win32/DirectX using SDL

Current Version     : 0.2.8

Last Update         : 05/16/2003

Authors             : - Julien FRELAT "Gollum" (julien.frelat@libertysurf.fr)
				Main Emulation Code
				GBA Emulation Code
				Portable Architecture
				GUI Work
				Win32 Port
				Optimizations
				Fixes
				Web Maintainer
				Documentation Maintainer
				English support

		      - Anarko (anarko@telia.com)
				Warning Fixes
			 	Very Initial Help

		      - Simon Singh
				Some Opcodes
				Some Fixes
				Help

		      - Richard Bannister
				MacOS Port
				Warning Fixes
			 	
		      - Niels Wagenaar
				BeOS/Linux/Unix/DirectX Port (using SDL)

                      - Dysfunction
                                Main Emulation Code
                                GBA Emulation Code
				Win32 Port Fixes
                                Win32 Debugger Improvements
				Win32 Configuration
				Win32 Options & Command Line
				Optimizations
				Fixes
				Help

                      - Perfect Dark
                                Win32 Port Improvements (Throttle/FrameSkip)
                                DirectX Port (FullScreen/Filtering/Joystick)

                      - Gil Pedersen
                                Emulation core Optimizations


What is it ?
------------

BoycottAdvance is a Gameboy Advance emulator entirely written in C.
This version is dedicated to Windows and uses both Win32(GDI)/DirectX display.
DirectX usage is highly recommended as it is faster than GDI.


How do I use it ?
-----------------

"BoycottAdvance" is the name of the executable program.
You need Gameboy Advance Roms, ".GBA ".AGB" or ".BIN" files, in order to run it.
(Don't ask me for roms, please use the Net !)


What is emulated ?
------------------

- ARM7TDMI with 32-bit ARM CPU support (80%) and 16-bit THUMB CPU support (95%) 
- Real sync emulation for HBlanks and VBlanks 
- Pseudo cycle counting
- Real sync framerate (60Hz or 60FPS) 
- GFX Mode 0 
	* 16 colors support 
	* 256 colors support 
	* BG0/BG1/BG2/BG3 support 
	* 256x256 up to 512x512 tile maps support 
	* Default priorities support 
	* Horizontal/Vertical flipping support 
	* Horizontal/Vertical offset support 
	* FadeIn/FadeOut Effect support
	* Alpha-blending Effect support
	* Backdrop Color Effect support
	* Windows 0/1 support
        * Rotation/Zoom
- GFX Mode 1 
	* 16 colors support 
	* 256 colors support 
	* BG0/BG1 support 
	* 256x256 up to 512x512 tile maps support 
	* Default priorities support 
	* Horizontal/Vertical flipping support 
	* Horizontal/Vertical offset support 
        * Rotation/Zoom
- GFX Mode 3 
	* 32768 colors support 
	* Horizontal/Vertical Mosaic Effect support 
- GFX Mode 4 
	* 256 out of 32768 colors support 
	* Double buffering support 
- GFX Mode 5 
	* 32768 colors support 
	* Double buffering support 
- OAM support 
	* GFX Mode 0 support 
	* Extra 256 colors support 
	* Horizontal/Vertical position 
	* Horizontal/Vertical flip 
	* 8x8 up to 64x64 sprite size support 
	* 1D/2D sprite display support 
        * Rotation/Zoom
- 256 colors line-per-line palette support
- Full DMA support
- Most BIOS features
- GBC Sound Emulation
- PCM Sound Emulation
- Timer Emulation
- System ROM support 
- External/Internal RAM support 
- Hardware IO support 
- Palette/Video/OAM RAM support 
- Up to 8MB ROM support 
- Cartridge RAM support 
- ROM Mirrors support 
- HBL/VBL emulation 
- Full Keypad support 


What are the special features ?
-------------------------------

- ARM CPU debugger (70%) and THUMB CPU debugger (100%) 
	* Step by step feature 
	* Jump instruction feature 
	* Run to next instruction feature 
	* Jump to memory address 
	* Run to breakpoint address 
	* Particular memory addresses to choose 
- ARM CPU registers & VBL counter display 
- Screen (Mode 4) display 
- BG/OBJ Layer enable display
- Window for BG/Bitmap Palette (256 colors) display 
- Window for OBJ/Objects Palette (256 colors) display 
- Running screen switch display 
- Memory browser 
- Dynamic resizing 
- Zoom x1, x2, x3, x4 
- Center screen display option for Mode 5 
- Stretch/Fit Screen display option for Mode 5 
- Pause & Reset emulation 
- Vertical sync option 
- Frame skip support
- Tweaking speed support
- Keyboard/Joystick support & configuration 
- ROM Analysis & Information 
- GZip/Zip files support (no need to uncompress roms)
- BIN/AGB/GBA files loading 
- Drop files loading 
- Command Line Support (boycottadvance -help)
- INI file support
- Auto-remember emulator options, windows positions/sizes


What is planned ?
-----------------

	X.XX:
	- Multiplayer support
	- TCP/IP support
	- Full GBA support
	- Fix last bugs in ARM7TDMI core
	- Optimizations (ARM7TDMI core, GFX core, GBA core)
	- Add screenshots loading/saving
	- Better compatibility rate (play better commercial roms)


What is new ?
-------------

Project started February 2001

	0.2.8 THE BOYCOTTADVANCE RELOADED",
	- (WIN32) Added skip intro hacks option (Gollum)
	- Fixes intro hacks when reloading a rom (Gollum)
	- Improved sound quality by rewriting mixing engine (Gollum)
	- Added a skip intro hacks option (you can combine it with skip BIOS option) (Gollum)
	- Fixed a huge bug for LDM opcodes [Golden Sun 2/Dragon Ball Z] (Gollum)
	- A few optimizations for Thumb CPU (Gollum)
	- Added BIOS call 1F [Metroid Fusion] (Gollum)

	0.2.7 THE TWO TOWERS REBIRTH VERSION
	- (WIN32) Fixes joystick default buttons (Gollum)
	- (WIN32) Fixes keyboard default keys (Gollum)
	- (WIN32) Start cleaning up the code (Gollum)
	- (WIN32) Add dozens of maker company name for ROM information (Gollum)
	- (WIN32) Add real name, maker code, game code, language for ROM information (Gollum)
	- (WIN32) Fixes default values in INI file (Gollum)
	- (WIN32) Add command line options help (Gollum)
	- (WIN32) Add/Fixes Autorun support (Gollum)
	- (WIN32) Fixes BIOS file selections (Gollum)
	- (WIN32) Fixes ROM file selections (Gollum)
	- (WIN32) Start fixing resources for easier localization by fans (Gollum)
	- Fixes BIOS call A [Sonic Advance/Harry Potter] (Gollum)
	- Fixes a BIOS sound call [Phantasy Star Collection] (Gollum)
	- Add support for EEPROM 4K/64K autodetection [Super Mario Yoshi Island] (Gollum)
	- Add support for gzip/zip BIOS file (Gollum)

	0.2.6 LITTLE GAME: FIND ME IN A RECENT WONDERFUL MOVIE ;-)
	- (WIN32) Add option to select FLASHROM ID (Gollum)
	- (WIN32) Add support for MagicKeyboard TM [DISABLED] (Gollum)
	- (WIN32) Add Sound Recording in MP3 file format (Gollum)
	- (WIN32) Add option to select a BIOS file (Gollum)
	- (WIN32) Fixes WAV recording to allow sound logging for each game (Gollum)
	- (WIN32) Fixes movies support to allow a movie for each game (Gollum)
	- (WIN32) Add support for movies (Gollum)
	- (WIN32) Add nine more slots for load/save states (Gollum)
	- (WIN32) Improve the news & history dialog (Gollum)
	- (WIN32) Add reverse stereo support (Gollum)
	- (WIN32) Change joystick initialization (Gollum)
	- (WIN32) Add/Fixes Sound Recording in WAV file format (Gollum)
	- (WIN32) Fixes command line support thanks to FinalBurn's Dave (Gollum)
	- Add support for FLASHROM identification (Gollum)
	- Fixes FLASHROM identification [Magical Vacation/Golden Sun] (Gollum)
	- Fixes MSR opcode [PocketNes] (Gollum)
	- Add BIOS call 4 [Jimmy Neutron: Boy Genius] (Gollum)
	- Fixes HBLANK interrupts thanks to Forgotten [Advance GTA/Hot Wheels: Burnin' Rubber/Andrew May Mode 7] (Gollum)
	- Fixes DMA transfers in 16/32 bits (Gollum)
	- Fixes BIOS memory access [Rocket Power Dream Scheme/The Wild Thornberrys: Chimp Chase/Ogre Tactics] (Gollum)
	- Fixes SRAM/FLASHROM emulation [Golden Sun] (Gollum)
	- Fixes FLASHROM emulation (Gollum)
	- Fixes misaligned 16bit memory reads (Gollum)
	- Fixes Mode 5 display [Engine Software Voxel] (Gollum)
	- Fixes registers initialization [Eurasia Intro Normal] (Gollum)
	- Fixes misaligned memory reads [Predator Development's FMV Player] (Gollum)
	- Fixes ROM loading again (Gollum)
	- Add support for 128Mbits roms (requires 8MB extra memory) [Yu-Gi-Oh! Duel Monsters 6 Expert 2/Snap Kids] (Gollum)
	- Fixes BAM movie support palette bug (Gollum)
	- Improve BAM movie support to start at any time (Richard Bannister)
	- Add support for movie recording/replaying in BAM file format (Gollum)
	- Version Numbers are now MacOS compliant (Richard Bannister)
	- Fixes sound buffer for SDL portability (Niels Wagenaar)
	- Add reverse stereo support (Gollum)
	- Fixes source code for portability issues (Niels Wagenaar)

	0.23b DYSFUNCTION & GOLLUM'S ABDUCTION BY SANTA CLAUS
	- (WIN32) Add Speedup Features (Gollum)
	- (WIN32) Add Stretch Fit To Screen for DirectX Fullscreen (Gollum)
	- (WIN32) Fixes ROM Information for first ROM loaded (Gollum)
	- (WIN32) Add/Fixes Scanline (TV Mode) support for DirectX Window/Fullscreen (Gollum)
	- (WIN32) Fixes Autospeed Throttle when using FrameSkip (Gollum)
	- (WIN32) Add frameskips 4/5 for low computers (Gollum)
	- (WIN32) Add VBA compatible FPS display (Gollum)
	- (WIN32) Major rework to the program Debugger (Dysfunction)
	- (WIN32) Fixes GetNumberDialog for large numbers (Dysfunction)
	- (WIN32) Reworked debugger code (Dysfunction)
	- (WIN32) Single step debugging now works properly (Dysfunction)
	- (WIN32) Runto/Breakpoint debugging now works properly (Dysfunction)
	- (WIN32) Register window now displays RUN/STOP/BKPNT RUN status
	- (WIN32) Add support for breakpoints & runto functions (Dysfunction)
	- (WIN32) All registers can now be edited during debug (Dysfunction)
	- Add support for MagicLight TM [DISABLED] (Gollum)
	- Add large EEPROM support [Super MarioAdvance 2] thanks to the VisualBoyAdvance author (Forgotten/Gollum)
	- Fixes GBA initialization [PocketNes] (Gollum)
	- Fixes BIOS setting [Sonic Advance] (Gollum)
	- Add/Fixes BIOS 1 [Mr. Driller 2] (Gollum)
	- Add a little memory hack [Lucky Luke - Wanted] (Gollum)
	- Disable new mainloop (sometimes buggy) (Gollum)
	- Remove a cpu hack [Ogre Tactics needs BIOS to work] (Gollum)
	- Optimize BIOS B/C (Gollum)
	- Fixes VBLANK emulation [Rocket Power Dream Scheme] (Gollum)
	- Add again layer debugging for Modes 0/1 (Gollum)
	- Add some opcodes [Doom/Denki Blocks] (Dysfunction)
	- Rewrite mainloop to support working debugger (Dysfunction)
	- Modify all cycle wasting to be under CPU code (Dysfunction)
	- Multiple breakpoint support (Dysfunction)
	- Optimize Rotation/Scale display for Modes 0/1/2 (Gollum)
	- Fixes BIOS 13 [Atlantis/Tetris Worlds] (Gollum)
	- Fixes GZip/Zip loading with multiple extensions (Gollum)
	- Fixes THUMB opcodes A0-A7 (Gollum)
	- Fixes OAM display in 16/256 colors for all Modes (Gollum)
	- Add/Fixes BIOS 16/17/18 [Atlantis/Tetris Worlds] (Gollum)

	0.22b GOLLUM IS BACK AND SAFE ;-)
	- (WIN32) Add nice fullscreen DirectX support (Perfect Dark)
	- (WIN32) Improve joystick responses (Perfect Dark)
	- (WIN32) Add nice bilinear filtering for stretched display on some videocards (Perfect Dark)
	- (WIN32) Add DirectX/GDI dynamic switch (Perfect Dark)
	- (WIN32) Fixes support for GBA/BIN/AGB inside GZip/Zip files (Gollum)
	- (WIN32) Add GZip/Zip loading (Gollum)
	- (WIN32) Add support for DirectX (Perfect Dark)
	- (WIN32) Gfx code restructure for DirectDraw (Perfect Dark)
	- (WIN32) Fixes Mode x1 display in debug mode (Gollum)
	- (WIN32) Change main menu (Dysfunction)
	- (WIN32) Add BiosFastboot/BiosLoad options (Dysfunction)
	- (WIN32) Improve again debugger (Dysfunction)
	- Optimize ARM core (Gil Pedersen)
	- Optimize (15-20% faster) THUMB core (Gil Pedersen)
	- Optimize DMA transfers (Richard Bannister)
	- Optimize cycle count (Richard Bannister)
	- Optimize ARM/THUMB mainloops (Richard Bannister)
	- Add support for realtime BIOS enable/disable (Richard Bannister)
	- Fixes backdrop color for Mode 4 (Gollum)
	- Fixes GBC sound volume to get closer real GBA sound (Gollum)
	- DRAMATICALLY improve the PCM sound quality (SOUND IS NOW PERFECT !) (Gollum)
	- Improve global sound volume to prevent crashes and noise (Gollum)
	- Fixes palette restoration (Dysfunction who should do some packing ;-)
	- Optimize BG Rotation/Scale (Gollum)
	- Optimize ARM prefetch stuff (Dysfunction)
	- Rewrite GFX core to support any pixel format (Dysfunction)
	- Fixes save states for extra information (new COT format) (Richard Bannister)
	- Add support for GZip/Zip files (Richard Bannister)
	- Optimise ARM opcodes to remove needless shifts (Dysfunction)
	- Fixes AlphaBlending effect for BG (Gollum)
	- Fixes BG Rotation/Scale outside bounds (Gollum)
	- Fixes again Tactics Ogre (Gollum)
	- Rewrite gfx code to support 16/32 bit modes (Dysfunction)
	- Add option for Mode5 centering (Dysfunction)
	- Add ARM opcode to fix many games (Gollum/Dysfunction)
	- Fixes UMULL/SMULL/SMLAL opcodes (Gollum)
	- Fixes BIOS Call C with unaligned access (Gollum)
	- Fixes BIOS Call 12 (Gollum)
	- Fixed History (Gollum)
	- Add support for 16-bit display at compiletime (Richard Bannister)
	- Add support for Fastboot (skip BIOS intro) & BiosLoad options (Richard Bannister)
	- SIO Tx/IRQ function implemented (Dysfunction)
	- Fixes LSL opcode with S set [Namco Museum Advanced] (Dysfunction)
	- Fixes IRQ to interrupt DMA (Dysfunction)
	- Remove unaligned 32-bit memory reads [F-Zero] (Dysfunction)
	- Fixes writes to key registers [Repton demo] (Dysfunction)
	- Add new framebuffer API & structures to prepare for DirectDraw (Perfect Dark)
	- Fixes corrupt screens using real BIOS (Dysfunction)
	- Fixes unauthorized 8-bit memory writes to VRAM/OAM/PALETTE (Dysfunction)
	
	0.21b GOLLUM'S ABDUCTION BY ALIENS
	- (WIN32) Add Sound Enable/Disable option (Dysfunction)
	- (WIN32) Add Frame Limiter & Auto Frame Skip (Perfect Dark)
	- (WIN32) Add Joystick configuration (Perfect Dark)
	- (WIN32) Add/Improve FPS display (Perfect Dark)
	- (WIN32) Add/Fixes breakpoint support (Dysfunction)
	- (WIN32) Fixes sound crash at exit (Perfect Dark)
	- (WIN32) Add Joystick support (Perfect Dark)
	- (WIN32) Remove multi-thread code (Perfect Dark)
	- (WIN32) Add sound support code (SMY)
	- (WIN32) Add +/- keys to scroll disassembly window (Dysfunction)
	- (WIN32) Fixes PC update in debugger  (Dysfunction)
	- (WIN32) Remove too buggy tweaking speeds (Dysfunction)
	- Fixes save state (new COT format) (Dysfunction)
	- Optimize GBC sound emulation (Dysfunction)
	- Optimize Rotation/Scale (Dysfunction)
	- Add an option to enable/disable writes in ROM (Dysfunction)
	- Fixes MSR opcode (Simon/Dysfunction)
	- Fixes Mode5 bank switching (Simon)
	- Fixes SWI return (Dysfunction)
	- Add cycle count support for DMA processes (Dysfunction)
	- Add buggy Direct Sound (PCM) implementation (Dysfunction)
	- Fixes DMA to stop multi DMA trigger (Dysfunction)
	- Fixes DSound mode using DMA (Dysfunction)
	- Optimize CV flags in ARM/THUMB emulation (Richard Bannister/Dysfunction)
	- Add recursion limit protection in ARM/THUMB emulation (Richard Bannister)
	- Add Timer support (Dysfunction)
	- Optimize hardware memory writes (Richard Bannister)
	- Optimize mainloop CPU access (Richard Bannister)
	- Fixes context switch ARM/THUMB with new optimisation (Dysfunction)
	- Fixes palette restoration (Richard Bannister)
	- Optimize sound emulation (Richard Bannister)
	- Add sound emulation for Mac  (Richard Bannister)
	- Fixes BIOS call 11 writing beyond end of VRAM (Dysfunction)
	- Add GameboyColor Compatible sound emulation (SMY)
	- Optimize palette rendering (Richard Bannister)
	- Add buggy cycle count for BIOS functions (Dysfunction)
	- Add a global cycle counter (Dysfunction)
	- Add Timer functions (Dysfunction)
	- Seperate cpu core in three modules (Dysfunction)
	- Rewrite all gfx modes code (Dysfunction)
	- Add 32-bit unaligned memory reads (Dysfunction)
	- Improve GBA Reset code (Dysfunction)
	- Fixes Castlevania missing background (Dysfunction)
	- Add memory allocation tests (Richard Bannister)
	- Fixes save states (new COT format) (Richard Bannister)
	- Add support for real BIOS loading (Dysfunction)
	- Add raw interrupts for BIOS (Dysfunction)
	- Add some ARM opcodes to support real BIOS (Dysfunction)
	- Add support for little endian computers (Richard Bannister)
	- Fixes memory reads/writes (Dysfunction)
	- Add back unaligned memory reads (Dysfunction)
	- Optimize BG16 rendering (Richard Bannister)
	- Optimize flags emulation (Richard Bannister)
	- Modified cycle counts for ARM/THUMB (Dysfunction)
	- Optimised BG access for all gfx functions (Dysfunction)
	- Fixes BG2/3 Rotation/Scale for wraparound/transparent support (Dysfunction)
	- Add Rotation/Scale support for OAM/BG2/BG3  (Dysfunction)
	- Add a few LDM/STM ARM opcodes to support BIOS (Dysfunction)
	- Ignore now all co-processor opcodes as we have no co-pro in a real GBA (Dysfunction)
	- Separate BIOS emulation into new files (Dysfunction)
	- Add BIOS calls E/F thanks to the VisualBoyAdvance author (Forgotten/Dysfunction)
	- Fixes AlphaBlending support for backdrops (Gollum)

	0.20b:
	- (WIN32) Add a free game to this package: PONG-FIGHTER 1.0 (Guyfawkes)
	- (WIN32) Add some comments for tweaking speeds (Gollum)
	- (WIN32) Add load/save state options (Gollum)
	- (WIN32) Remove autorun by default thanks to Sir Jaguar (Gollum)
	- Add buggy OAM AlphaBlending effect (Gollum/Simon) 
	- Add buggy OAM FadeIn/FadeOut effect (Gollum/Simon)
	- Only use SAV files if needed (not for demos or games without saves) (Gollum)
	- Add a small protection scheme to prevent leaked betas (I hope ;-) (Gollum)
	- Add support for load/save STATE [smaller than VGBA's] (Gollum)
	- Add support for SAV files [VGBA-Compatible] (Gollum)
	- Add SRAM emulation support (Gollum)
	- Add finally BIOS call 13 [Bomberman Story/Mario 1 Game/Winning Post] (Gollum)
	- Fixes sprite center when using rotation/scale sprites (Gollum)
	- Fixes a small bug in ARM core with interrupts (Gollum)
	- Complete rebuild of all source files (Keith Wilkins)
	- Rewrite mainloop (Keith Wilkins)
	- Remove HBLANK from VBLANK period (Keith Wilkins)
	- Update mainloop with breakpoint set (Keith Wilkins)
	- Add support for keyboard interrupts (Keith Wilkins)
	- Rewrite IRQ handling (Keith Wilkins)
	- Optimize memory writes (Keith Wilkins)
	- Optimize DMA write checks (Keith Wilkins)
	- Add a new memory handler to handle IRQ/DMA (Keith Wilkins)
	- Add DMA interrupts (Keith Wilkins)
	- Add repeatable DMA (Keith Wilkins)
	- Add support for HBLANK/VBLANK for DMA (Keith Wilkins)
	- Add basic support for HBLANK interrupts (Gollum)
	- Fixes Windows support for Mode 0 (Gollum)
	- Fixes FlashROM emulation [Top Gear All Japan GT Championship] (Gollum)
	- Fixes Mode 5 display (Gollum)
	- Ignore BIOS calls E/F (Gollum)
	- Optimize memory reads/writes (Gollum)
	- Add instruction logs for better debugging (Gollum)
	- Add full FlashROM emulation (Gollum)
	- Add SMULL/SMLAL opcodes (Gollum)
	- Fixes UMULL opcode (Gollum)
	- Fixes External RAM support for THUMB core (Gollum)
	- Fixes 32-bit memory writes using misaligned addresses (Gollum)
	- Fixes External RAM support for ARM core (Gollum)
	- Fixes VCOUNT MATCH interrupts (Gollum)
	- Add HBLANK evaluation (Gollum)
	- Fixes VCOUNT MATCH evaluation (Gollum)
	- Add basic support for VCOUNT match interrupts (Gollum)
	- Add VCOUNT evaluation (Gollum)
	- [NOT YET ADDED] Add OAM alpha-blending support (Simon)
	- [NOT YET ADDED] Add timers support (Simon)
	- Add reload for DMA (Keith Wilkins)
	- Fixes crash in Mario Advance - Mario game - (Keith Wilkins)
	- Rewrite all DMA stuff (Keith Wilkins)
	- Fixes OAM support for Modes 2/3/4/5 thanks to Guyfawkes (Gollum)

	0.19b:
	- (WIN32) Fixes GUI initialization with options (Keith Wilkins)
	- (WIN32) Autoremember speed and frameskip settings (Keith Wilkins)
	- (WIN32) Add support for tweaking speed (Gollum)
	- (WIN32) Add support for frameskipping (Gollum)
	- (WIN32) Fixes immediate exit crash bug (Gollum)
	- (WIN32) Improve command line debug (Keith Wilkins)
	- (WIN32) Fixes definitively the reset bug (Keith Wilkins)
	- (WIN32) Redesign a bit menus (Gollum)
	- (WIN32) Add a Load & Run *special lamer* option (Gollum)
	- (WIN32) Fixes options menu (Gollum)
	- (WIN32) Add accelerators for main menu (Keith Wilkins)
	- (WIN32) Fixes display in -nodebug mode (Gollum)
	- (WIN32) Fixes normal zoom display (Gollum)
	- (WIN32) Fixed exit crash bug, race condition on emu thread (Keith Wilkins)
	- (WIN32) Add/Fixes BoycottAdvance icon (Gollum)
	- (WIN32) Autoremember autofire, vsync, show FPS (Keith Wilkins)
	- (WIN32) Autoremember keyboard configuration (Keith Wilkins)
	- (WIN32) Fixes rom path if cancel option choosed (Keith Wilkins)
	- (WIN32) Add -debug/-nodebug(default) options to show/hide debug menus (Keith Wilkins)
	- (WIN32) Add support for persistant windows (Keith Wilkins)
	- (WIN32) Autoremember last rom path (Keith Wilkins)
	- (WIN32) Improve command line handling (Keith Wilkins)
	- (WIN32) Add -autosave option to autosave an INI file when exits (Keith Wilkins)
	- (WIN32) Autoremember zoom level (Keith Wilkins)
	- (WIN32) Autoremember windows positions (Keith Wilkins)
	- (WIN32) Add -writeini option to autogenerate an INI file (Keith Wilkins)
	- (WIN32) Add support for INI file (Keith Wilkins)
	- (WIN32) Add -autorun option (Keith Wilkins)
	- (WIN32) Add support for command line (type boycottadvance -help) (Keith Wilkins)
	- (WIN32) Fixes Status Message update (Keith Wilkins)
	- Add support for Windows 0/1 in Mode 0 (Gollum)
	- Optimize memory reads/writes out of bounds (Keith Wilkins)
	- Add OBJ support for Mode 2/3/4 (Keith Wilkins/Gollum)
	- Optimize OBJ support (Keith Wilkins/Gollum)
	- Add tweaking speed support (Gollum)
	- Add frameskip support (Gollum)
	- Fixes VBLANK interrupt (Gollum)
	- Fixes BG and OBJ support for Mode 1 (Gollum)
	- Optimize 16 colors rendering for BG in Mode 0 (Gollum)
	- Rewrite BG support for Mode 0/1 (Gollum)
	- Add a few ARM opcodes to support PocketNes (Gollum)
	- Fixes (aarghh!) all garbage gfx stuff in Mode 0 (Gollum)
	- Optimize color effects in Mode 0 (Gollum)
	- Optimize backdrop color effects in Mode 0 (Gollum)
	- Add basic EEPROM emulation thanks to the VisualBoyAdvance author (Forgotten/Gollum)
	- Add backdrop color effects in Mode 0 (Gollum)
	- Fixes a bug for DMA3 transfer (Gollum)
	- Optimize memory read/writes (Gollum)

	0.18b:
	- (WIN32) Add BG/OBJ layers option (Gollum)
	- Fixes BIOS emulation for function 5 (Gollum)
	- Fixes OBJ support in Mode 0 (Gollum)
	- Fixes Horizontal/Vertical offset for BG in Mode 0 (Gollum)
	- Fixes FLASH ROM support thanks to Ben-J (Gollum)
	- Fixes BIOS emulation for function 14/15 (Gollum)
	- Fixes (aarrghhhhhh!!!!) interrupts (Gollum)
	- Change the VBLANK place in main loop (Gollum)
	- Add preliminary logs ala VGBA for better debugging (Gollum)
	- Improve cycle count for ARM & THUMB cores (Gollum)
	- Add preliminary alpha-blending for BG in Mode 0 (Gollum)
	- Ignore BIOS 9/A (Gollum)
	- Fixes reset after crashing (Gollum)
	- Add preliminary FadeIn/FadeOut Effect for BG in Mode 0 (Gollum)
	- Optimize BG support for Mode 0 (Gollum)
	- Fixes again BG/OBJ in Mode 0/1 (Gollum)
	- Fixes BIOS emulation for function 8 (Gollum)
	- Add BIOS emulation for function 7 (Gollum)
	- Fixes BIOS emulation for function 6 (Gollum)
	- Add support for BG/OBJ layers (Gollum)
	- Fixes BG for Mode 0 (Gollum)
	- Fixes OBJ for Mode 0 (Gollum)
	- Add DMA1/DMA2 transfer support (Gollum)
	- Fixes ADC/SBC in THUMB emulator (Gollum)
	- Fixes memory outside bounds (Gollum)
	- Rewrite memory core (Gollum)
	- Add Flash RAM support (Gollum)
	- Fixes ROM mirrors (Gollum)
	- Fixes Flags debugging (Gollum)
	- Fixes POP/PUSH in thumb debugger (Gollum)
	- Add BIOS emulation for functions 8/11/12 (Gollum)
	- Add a few opcodes (Gollum)
	- Fixes SPSR registers (Gollum)
	- Add MULL opcode (Gollum)
	- Fixes SWI in debugger (Gollum)
	- Fixes BIOS emulation for functions B/C (Gollum)
	- Ignore BIOS functions 1/2 (Gollum)
	- Add BIOS emulation for functions B/C/14/15 (Gollum)
	- Fixes DMA0/DMA3 transfer reinitialization (Gollum)
	- Add preliminary DMA0 transfer support in 16/32-bit (Gollum)
	- Fixes OBJ support in Mode 0/3 (Gollum)
	- Add VBLANK interrupt (Gollum)
	- Fixes clear screen for BG in Mode 0/1/2 (Gollum)
	- Add BIOS emulation for functions 5/6 (Gollum)
	- Add a few more opcodes to THUMB emulator (Gollum)
	- Fixes some bugs in THUMB emulator (Simon)
	- Fixes a small bug in OBJ rendering thanks to Jim Bagley (Gollum)
	- Add basic OAM support for Mode 3 (Gollum)
	- Fixes hardware initialization (Gollum)
	- THUMB emulator should be 90% complete now! (Gollum)
	- THUMB debugger should be 100% complete now! (Gollum)
	- Fixes (argh!) LDM/STM opcodes (Gollum)
	- Fixes (?) VBLANK Status (Gollum)
	- Add unaligned memory reads support (Gollum)
	- Add support for mode states (Gollum)
	- Fixes T state in CPSR (Gollum)
	- Add debugging for special CPU mode and bits (Gollum)
	- Add preliminary interrupt support for VBLANK (Gollum)
	- Add preliminary Mode 2 Emulation (Gollum)
	- Fixes bugs in THUMB emulator (Gollum)
	- Add MSR opcode (Gollum)
	- Add many opcodes to THUMB emulator (Gollum)
	- Add many opcodes to THUMB debugger (Gollum)
	- Fixes (argh!) all opcodes with R15 as destination register (Gollum)
	- Fixes (argh?) STM opcodes with R15 in register list (Gollum)
	- Fixes (argh!) S flag for CMN/CMP opcodes (Gollum)
	- Fixes (argh!) carry flag for all arithmetic opcodes (Gollum)

	0.17b R3:
	- (WIN32) Add a Show FPS option (Gollum)
	- (WIN32) Add autofire support for A/B/LS/RS buttons (Gollum)
	- (WIN32) Fixes multiple file loading (Gollum)
	- (WIN32) Drop file loading is now thread-safe (Gollum)
	- (WIN32) Loading is now thread-safe (Gollum)
	- (WIN32) Reset option is now thread-safe but sometimes freezes (Gollum)
	- (WIN32) Clear screen when loading a new demo with different Mode (Gollum)
	- (WIN32) Fixes debugging features with THUMB mode (Gollum)
	- (WIN32) Clear screen when switching display for Mode 5 (Gollum)
	- (WIN32) Add OBJ Palette display in separate window (Gollum)
	- (WIN32) Reduce a bit some events code (Gollum)
	- (WIN32) Add particular addresses to select for memory dump (Gollum)
	- [DISABLED] Fade In/Fade Out Effect for BG in Mode 0 (Gollum)
	- [DISABLED] Mosaic Effect for BG in Mode 0 (Gollum)
	- Add preliminary 16 colors support for OBJ in Mode 1 (Gollum)
	- Add basic OAM support for Mode 4 (Gollum)
	- Optimize Mosaic Effect for Mode 3 (Gollum)
	- Fixes 512x256 and 512x512 screen sizes for BG in Mode 1 (Gollum)
	- Fixes 512x256 and 512x512 screen sizes for BG in Mode 0 (Gollum)
	- Fixes (argh!) screen sizes for BG in Mode 1 (Gollum)
	- Fixes (argh!) screen sizes for BG in Mode 0 (Gollum)
	- Add preliminary 16 colors support for OBJ in Mode 0 (Gollum)
	- Add EORS opcode (Simon)
	- Fixes memory reset (Gollum)
	- Optimize rendering according to current Mode (Gollum)
	- Add a line-per-line engine for Mode 1 (Gollum)
	- Add a line-per-line engine for Mode 3 (Gollum)
	- Add a line-per-line engine for Mode 5 (Gollum)
	- Add a line-per-line engine for Mode 4 (Gollum)
	- Add preliminary priorities for OBJ in Mode 0 (Gollum)
	- Fixes horizontal clipping for OBJ in Mode 0 (Gollum)
	- Add priorities for OBJ against BG in Mode 0 (Gollum)
	- Add priorities for BG in Mode 0 (Gollum)
	- Fixes a position bug for flipped OBJ in Mode 0 (Gollum)
	- Add a line-per-line engine for OBJ in Mode 0 (Gollum)
	- Add a line-per-line engine for BG in Mode 0 (Gollum)
	- Optimize rendering in Mode 5 (Gollum)
	- Optimize BG rendering for BG in Mode 0 (Gollum)
	- Start THUMB debugger (Gollum)
	- Fixes (argh!) keyboard support to support multiple keys at the same time (Gollum)
	- Fixes (argh!) ADCS/SBCS opcodes with reg shifts (Gollum)
	- Fixes (argh?) LDRB/LDRW/LDRH opcodes with Rd=Rn (Gollum)
	- Fixes out of bounds memory reads for DMA transferts (Gollum)
	- Fixes opcode fetching in Internal RAM (Gollum)
	- Fixes (argh!) ADD/MOV/MVN with PC, thanks to NoahFex (Gollum)
	- Add preliminary 16 colors support for BG in Mode 1 (Gollum)
	- Add preliminary 16 colors support for BG in Mode 0 (Gollum)
	- Fixes a few opcodes (Gollum)
	- Add a few opcodes (Simon)
	- Fixes all multiple opcodes (Gollum)
	- Fixes (argh!) immediate rotates with S flag (Gollum)
	- Fixes (argh!) MOVS/MVNS opcodes (Gollum)
	
	0.16b :
	- (WIN32) Add BG Palette display in separate window (Gollum)
	- (WIN32) Fixes/Improve all windows (Gollum)
	- (WIN32) Add an option to switch display for Mode 5 (Gollum)
	- (WIN32) Add a Run To Breakpoint Address feature (Gollum)
	- (WIN32) Add/Fixes debugging features in GUI (Gollum)
	- (WIN32) Change Rom Info window (Gollum)
	- (WIN32) Change Registers window (Gollum)
	- (WIN32) Remove non-working Windows keys (Gollum)
	- (WIN32) Clear/Rewrite Keyboard Configuration management (Gollum)
	- (WIN32) Change Keyboard Configuration window according to GBA keys (Gollum)
	- Start THUMB emulation (Gollum)
	- Add a few opcodes to debugger (Gollum)
	- Add all LDRB/LDRW/STRB/STRW opcodes in debugger core (Gollum)
	- Add all LDRB/LDRW/STRB/STRW opcodes in emulator core (Gollum)
	- Fixes a few opcodes (Gollum)
	- Add support for 1D or 2D OBJ mapping (Gollum)
	- Fixes breakpoint emulation loop (Gollum)
	- Add a line-per-line palette engine for OBJ (Gollum)
	- Optimize selected GFX Mode display (Gollum)
	- Optimize Mode 1 display (Gollum)
	- Optimize Mode 0 display (Gollum)
	- Optimize rendering for BG in Mode 0 (Gollum)
	- Add extra fake OAM RAM for Fitzy demo (Gollum)
	- Fixes Hardware Registers memory size (Gollum)
	- Add stretch/fit screen display for Mode 5 (Gollum)
	- Add screen center display for Mode 5 (Gollum)
	- Add double buffer support in Mode 5 (Gollum)
	- Add Mode 5 Emulation (Gollum)
	- Optimize fetch opcode (Gollum)
	- Start optimizing palette rendering (Gollum)
	- Add preliminary Mode 1 Emulation (Gollum)
	- Optimize Mosaic Effect with zero as value for BG in Mode 3 (Gollum)
	- Optimize Mosaic Effect for BG in Mode 3 (Gollum)
	- Add Mosaic Effect for BG in Mode 3 (Gollum)
	- Add Mosaic Effect for BG in Mode 0 (Gollum)
	- Fixes rendering for OBJ in Mode 0 (Gollum)
	- Fixes Horizontal/Vertical flip for OBJ in Mode 0 (Gollum)
	- Add multiple sizes support for OBJ in Mode 0 (Gollum)
	- Optimize rendering for BG in Mode 0 (Gollum)
	- Optimize rendering for OBJ in Mode 0 (Gollum)
	- Add Horizontal/Vertical flip for OBJ in Mode 0 (Gollum)
	- Add very basic OAM support for Mode 0 (Gollum)
	- Add/Fixes tilemap sizes for BG in Mode 0 => much slower! (Gollum)
	- Add/Fixes DMA3 transfer support with address control flags (Gollum)
	- Fixes a few opcodes (Gollum)
	- Add extra fake RAM for AFire demo (Gollum)
	- Add ROM mirroring support (Gollum)
	- Add System ROM support (Gollum)
	- Add Cartridge RAM support (Gollum)
	
	0.15b :
	SPEED TESTS: BoycottAdvance (fastest)/VGBA/iGBA/EloGBA
	- (WIN32) Fixes FPS display at startup (Gollum)
	- Fixes a crashing bug in debugger (Gollum)
	- Fixes a few opcodes with S bit set (Gollum)
	- Add/Fixes THUMB emulation mode detection (Gollum)
	- Fixes GUI keyboard support to mix with keybad support (Gollum)
	- Add/Fixes keypad initialization (Gollum)
	- Fixes keypad support for L & R buttons (Gollum)
	- Add keypad support thanks to Simon (Gollum)
	- Add a few opcodes (Gollum)
	- Reverse VBL/non-VBL periods to fix Movie demo (Gollum)
	- Add a few opcodes (Gollum)
	
	0.14b :
	- (WIN32) Fixes default normal zoom (Gollum)
	- (WIN32) Fixes version number (Gollum)
	- Simplify some macros (Gollum)
	- Add a few LDRH opcodes with writeback (Gollum)
	- Add a few LDRH opcodes (Gollum)
	- Start writing new debugger core (Gollum)
	
	0.13b :
	- (WIN32) Fixes again exit menu feature in Release mode (Gollum)
	- Add 256x256 tile maps support for BG in Mode 0 (Gollum)
	- Add default priorities for BG in Mode 0 (Gollum)
	- Fixes Horizontal/Vertical offset for BG in Mode 0 (Gollum)
	- Fixes BG enable/display for BG in Mode 0 (Gollum)
	- Fixes (argh!) halfoffsets in both debugger/emulator (Gollum)
	- Add/Fixes Mode 0 reset at each frame (Gollum)
	- Add Horizontal/Vertical offset for BG in Mode 0 (Gollum)
	- Add Horizontal/Vertical flip for BG in Mode 0 (Gollum)
	- Add BG0/BG1/BG2/BG3 support for Mode 0 (Gollum)
	- Add preliminary Mode 0 Emulation (Gollum)
	- Fixes many opcodes without updated PC (Gollum)
	- Fixes many opcodes without prefetch (Gollum)
	- Add External RAM support (Gollum)
	- Fixes LDM/STM opcodes in debugger (Gollum)
	- Add/Fixes opcodes in debugger (Gollum)
	- Add preliminary Mode 3 Emulation (Gollum)
	- Optimize(???) 8-bit memory reads (Gollum)
	- Optimize memory bounds checking (Gollum)
	- Optimize memory writes (Gollum)
	- Optimize rotation registers (Gollum)
	- Add fixes for demos which exceed memory bounds (Chaos89/Stars demos) (Gollum)
	- Optimize DMA transfers (Gollum)
	- Optimize memory reads but remove memory bounds checking (Gollum)
	- Optimize flag emulation (Gollum)
	- Optimize palette rendering (Gollum)
	- Add a few LDM/STM opcodes (Gollum)
	
	0.12b :
	COMPATIBILITY:
	ConsoleDev/Plasma/Chaos89/Rotozoomer/Copperbars/Fire/Stars demos
	- (WIN32) Start now in normal mode (zoom x1) (Gollum)
	- (WIN32) Fixes drop files loading (Gollum)
	- (WIN32) Fixes exit menu feature (Gollum)
	- (WIN32) Clarify code and fixes a few things in the GUI (Gollum)
	- (WIN32) Add GBA/AGB file extension to ROM loading (Gollum)
	- (WIN32) Fixes/improve run to next instruction feature (Gollum)
	- (WIN32) Fixes switch back to emulation screen after screen/gfx debugging (Gollum)
	- (WIN32) Fixes reset feature (Gollum)
	- Add/Fixes some multiple-encoding opcodes (Gollum)
	- Fixes (aargh!) ADDS/ADCS/SUBS/RSBS opcodes (Gollum)
	- Add a few LDM/STM opcodes in debugger (Gollum)
	- Add Register list in debugger (Gollum)
	- Add S bit support for shift operations (Gollum)
	- Add an accurate breakpoint emulation mode (Gollum)
	
	0.11b :
	COMPATIBILITY:
	ConsoleDev/Plasma/Chaos89/Rotozoomer/Copperbars/Fire demos
	- (WIN32) Add a Debug Registers window (Gollum)
	- (WIN32) Fixes correct size for zooms mode (Gollum)
	- (WIN32) Add new icons from Champipi (Gollum)
	- (WIN32) Improve initialization (Gollum)
	- (WIN32) Fixes/improve memory dump feature (Gollum)
	- (WIN32) Fixes menu features (Gollum)
	- (WIN32) Add a palette debug feature (Gollum)
	- (WIN32) Add a screen debug feature (Gollum)
	- (WIN32) Add a run to next instruction feature (Gollum)
	- (WIN32) Fixes menus & options for this version (Gollum)
	- (WIN32) Add/fixes vsync option (Gollum)
	- Optimize macros for STRH opcodes (Gollum)
	- Add/fixes macros for all LDR/STR/STRH opcodes with writeback (Gollum)
	- Rewrite/Optimize flag emulation (Gollum)
	- Add a few ADC/AND/EOR/RSB opcodes with S bit set (Gollum)
	- Add overflow flag support for a few opcodes (Gollum)
	- Fixes STMFD opcode (Gollum)
	- Add LDMFD/STMFD opcodes (Gollum)
	- Fixes/improve a few opcodes (Gollum)
	- Add macros for all LDR/STR/LDRH/STRH opcodes (Gollum)
	- Optimize instruction fetch (Gollum)
	- Fixes a small VBL bug (Gollum)
	- Add a few LDR/STRH/LDRH opcodes (Gollum)
	- Add carry flag support for a few opcodes (Gollum)
	- Add a line-per-line palette engine (Gollum)
	- Fixes double buffer support in Mode 4 (Gollum)
	- Add a few ignored opcodes (Gollum)
	- Add MVN/CMN/RSB opcodes (Gollum)
	- Add double buffer support in Mode 4 (Gollum)
	- Add Internal RAM support (Gollum)
	- Add a few STR/LDR/STRH/LDRH/LDRB/STRB (Gollum)
	- Fixes MUL opcode (Gollum)
	- Optimize gfx screen display (Gollum)
	- Fixes CMP opcodes (Gollum)
	- Add a few MUL/SUBS/BIC/BICS opcodes (Gollum)
	- Optimize condition codes emulation (Gollum)
	- Optimize opcodes in Always condition (Gollum)
	- Optimize condition codes debugging (Gollum)
	- Add/fixes some opcodes in the emulator core (Gollum)
	- Add/fixes some opcodes in the debugger core (Gollum)
	- Add some defines to make code easy to read (Gollum)
	- Add condition codes debugging (Anarko)
	- Fixes portable debugging features (Anarko)
	- Add condition codes emulation (Anarko) 
	- Add a few ADD/CMP/SUB/MOV opcodes (Anarko)
	- Add macros for easy cpu flags emulations (Anarko)
	- Add registers & flags initialization (Anarko)
	- Add CPSR & flags support (Anarko)
	- Change PALETTE into PALRAM to not confuse MsDos port (Anarko)
	- First demo screen shows with Consoledev demo !!!
	- Add 256 colors palette emulation mode (DEFAULT) (Gollum)
	- Add Mode 4 emulation (DEFAULT) (Gollum)
	- Add Fake VBL emulation (Gollum)
	- Add DMA3 transfer support in 32-bit (Gollum)
	- Add a few LDR/STR/ADD/AND opcodes (Gollum)
	- Add all B/BL opcodes (Gollum)
	- Fixes all files to be compliant with Portable Cott Architecture (Gollum)
	- Add/Fixes/Improve memory manager (Gollum)
	- Add registers debugging support (Gollum)
	- Fixes some warnings (Anarko)
	- Rename some files to be compliant with Portable Cott Architecture (Anarko)
	- Add a few opcodes to debugger
	
	0.10b :
	- Add Drag & Drop feature for ROM file
	- ROM Loading
	- First executable version


Who I would like to acknowledge ?
---------------------------------

In no particular order
- Dysfunction for fixes, many improvements and great help
- Gil Pedersen for very nice optimizations
- Anarko for help, fixes & advices
- Simon Singh for help, fixes & advices
- Guyfawkes for Pong Fighter !
- Forgotten for help/tips on EEPROM and some BIOS calls
- Dave (Final Burn) for his nice testing
- Karine (I LOVE U!) for site design/logos/icons and her love
- Sir Jaguar/Rico2000/Ben-J for their nice french emulation sites
- Voltarene for his nice screenshots in Zoom x2 ;-)
- GBADEV.ORG for all the useful things (docs, demos)
- Nokturn for help & advices
- 6re9ouINet for his sources
- Lemon for testing on real TS4 hardware
- NoahFex for help on bugs and for its cool Playboy Advance emulator
- Jim Bagley for some tips
- Sebastien for showing me a real GBA with Castlevania...
- All my interviewers...
- All my fans... Thanks again for your postcards and gifts!


Where can I find BoycottAdvance ?
---------------------------------

BoycottAdvance's Official Homepage (English)
http://boycottadvance.emuunlim.com
These pages are maintained by Gollum.


---------------------------------------------------------------------------
The author takes no responsability for what you do with the emulator.
You must own a legal copy of the games.
Don't ask us for roms.
Gameboy, Gameboy Advance are registered trademarks of Nintendo.
Win32, DirectX, Windows 95, Windows 98, Windows 2000, Windows NT are registered trademarks of Microsoft.
