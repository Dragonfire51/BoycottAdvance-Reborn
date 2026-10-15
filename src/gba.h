/*
 * gba.h - Game Boy Advance system model.
 *
 * Reconstructed from reverse engineering of the BoycottAdvance 0.2.8 binary
 * and from ARM7TDMI / GBA hardware documentation. Modern SDL2 frontend,
 * complete ARM7TDMI core, correct interrupt/BIOS behaviour so commercial
 * games (e.g. Pokemon) boot, and Win11-friendly audio (SDL2 instead of WINMM).
 */
#ifndef GBA_H
#define GBA_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define GBA_VERSION  "v1.3.2"
#define GBA_NAME     "BoycottAdvance Reborn"
#define GBA_TITLE    GBA_NAME " " GBA_VERSION

#define SCREEN_W 240
#define SCREEN_H 160

/* ---- CPU modes -------------------------------------------------------- */
#define MODE_USR 0x10
#define MODE_FIQ 0x11
#define MODE_IRQ 0x12
#define MODE_SVC 0x13
#define MODE_ABT 0x17
#define MODE_UND 0x1B
#define MODE_SYS 0x1F

/* CPSR bits */
#define CPSR_N  0x80000000u
#define CPSR_Z  0x40000000u
#define CPSR_C  0x20000000u
#define CPSR_V  0x10000000u
#define CPSR_I  0x00000080u
#define CPSR_F  0x00000040u
#define CPSR_T  0x00000020u
#define CPSR_M  0x0000001Fu

/* ---- Memory sizes ------------------------------------------------------ */
#define BIOS_SIZE   0x4000
#define EWRAM_SIZE  0x40000
#define IWRAM_SIZE  0x8000
#define PAL_SIZE    0x400
#define VRAM_SIZE   0x18000
#define OAM_SIZE    0x400
#define ROM_MAX     0x2000000
#define SAVE_MAX    0x20000

/* ---- Interrupt flags --------------------------------------------------- */
#define IF_VBLANK 0x0001
#define IF_HBLANK 0x0002
#define IF_VCOUNT 0x0004
#define IF_TIMER0 0x0008
#define IF_TIMER1 0x0010
#define IF_TIMER2 0x0020
#define IF_TIMER3 0x0040
#define IF_SERIAL 0x0080
#define IF_DMA0   0x0100
#define IF_DMA1   0x0200
#define IF_DMA2   0x0400
#define IF_DMA3   0x0800
#define IF_KEYPAD 0x1000
#define IF_GAMEPAK 0x2000

/* ---- CPU register file -------------------------------------------------- */
enum { BANK_USR=0, BANK_FIQ, BANK_IRQ, BANK_SVC, BANK_ABT, BANK_UND, BANK_NUM };

typedef struct {
    uint32_t r[16];          /* visible general registers, r15 = PC */
    uint32_t bank[6][7];     /* [mode][0]=r8,1=r9,2=r10,3=r11,4=r12,5=r13,6=r14 */
    uint32_t spsr[6];        /* saved program status per mode */
    uint32_t cpsr;
    uint32_t m;
} CpuRegs;

typedef struct {
    int mode;                /* current CPU mode index */
    bool irq_pending;
    bool halt;
    bool stopped;
} CpuState;

/* ---- Timers -------------------------------------------------------------- */
typedef struct {
    bool enabled;
    uint16_t reload;
    uint16_t counter;
    int prescaler;           /* 1,64,256,1024 */
    bool count_up;           /* counting by overflow (timer chaining) */
    bool irq_enable;
    uint32_t clock;          /* sub-cycle accumulator */
} Timer;

/* ---- DMA ------------------------------------------------------------------ */
typedef struct {
    uint32_t src;
    uint32_t dst;
    uint32_t count;
    int src_ctl;
    int dst_ctl;
    int unit;
    int timing;
    bool repeat;
    bool irq_enable;
    bool enabled;
    int word;
} DmaChan;

/* ---- APU state ----------------------------------------------------------- */
#define SAMPLE_RATE 44100
#define APU_SR       44100
#define APU_CYCLES_PER_SAMPLE (16777216 / APU_SR)

typedef struct {
    bool enabled;

    /* channels 1..4 */
    uint16_t nr10, nr11, nr12, nr13, nr14;
    uint16_t nr21, nr22, nr23, nr24;
    uint16_t nr30, nr31, nr32, nr33, nr34;
    uint16_t nr41, nr42, nr43, nr44;
    uint16_t nr50, nr51, nr52;
    uint8_t  wave[16];       /* wave sample RAM 0x4000090..0x9F */

    bool ch1_on;
    uint32_t ch1_phase;      /* 16.16 duty position */
    uint32_t ch1_env_cc;
    int  ch1_vol;
    bool ch1_dir;

    bool ch2_on;
    uint32_t ch2_phase;
    uint32_t ch2_env_cc;
    int  ch2_vol;
    bool ch2_dir;

    bool ch3_on;
    uint32_t ch3_phase;
    uint32_t ch3_len_cc;

    bool ch4_on;
    uint32_t ch4_env_cc;
    int  ch4_vol;
    bool ch4_dir;
    uint32_t ch4_lfsr;
    uint32_t ch4_cc;

    uint64_t env_cc;         /* envelope clock accumulator (CPU cycles) */

    int  sound_cnt[2];        /* SOUNDCNT_H: DMA A/B enable + volume */
    int  fifoa_volume, fifob_volume;
    int32_t fifoa[32], fifob[32];
    int fifoa_head, fifoa_len, fifob_head, fifob_len;
    int32_t fifoa_held, fifob_held;
    int  fifo_asel, fifo_bsel;        /* timer 0/1 selected per FIFO */
    uint32_t fifo_acc_a, fifo_acc_b;  /* cycles until next FIFO pop */

    uint64_t sample_cc;      /* leftover cycles, kept as a fraction */
} Apu;

/* ---- PPU state ------------------------------------------------------------- */
typedef struct {
    uint16_t dispcnt;
    uint16_t dispstat;
    uint16_t vcount;
    uint16_t bgcnt[4];
    uint16_t bgvofs[4];
    uint16_t bghofs[4];
    int16_t  bgaffx[2], bgaffy[2];      /* affine ref points */
    int16_t  bgaffpa[4], bgaffpb[4];    /* per BG (2 affine BGs) */
    int16_t  bgaffpc[2], bgaffpd[2];    /* affine deltas (vertical) */
    uint16_t winh[2], winv[2];
    uint16_t winin[2];
    uint16_t winout;
    uint16_t mosaic;
    uint16_t bldcnt;
    uint16_t blendalpha;
    uint16_t blendy;
    uint16_t keycnt;
    uint16_t keyinput;

    uint16_t frame[SCREEN_W*SCREEN_H];  /* output RGB555 */
    uint16_t bgpri[SCREEN_W*SCREEN_H];
    uint16_t bgalpha[SCREEN_W*SCREEN_H];
} Ppu;

/* ---- GBA core --------------------------------------------------------------- */
typedef struct GBA {
    uint8_t  bios[BIOS_SIZE];
    bool     bios_enabled;
    bool     bios_fastboot;

    uint8_t  ewram[EWRAM_SIZE];   /* 0x02000000 */
    uint8_t  iwram[IWRAM_SIZE];   /* 0x03000000 */
    uint8_t  bios_shadow[BIOS_SIZE];

    uint8_t  vram[VRAM_SIZE];     /* 0x06000000 */
    uint8_t  oam[OAM_SIZE];       /* 0x07000000 */
    uint8_t  pal[PAL_SIZE];       /* 0x05000000 */

    uint8_t  rom[ROM_MAX];
    uint32_t rom_size;
    uint32_t rom_mask;
    char     rom_title[16];

    uint8_t  save_mem[SAVE_MAX];
    uint32_t save_size;
    int      save_type;           /* 0 none, 1 sram, 2 flash, 3 flash128, 4 eeprom */

    uint8_t  io[0x400];           /* I/O registers window */

    CpuRegs  cpu;
    CpuState cst;

    uint32_t cycle;               /* absolute cycle counter (16.78MHz) */

    /* IO registers (also directly readable/writable via io[]; keep explicit for clarity) */
    uint16_t ie, if_reg;
    bool     ime;
    uint16_t keypad;
    uint16_t dma_hi_cache;

    Timer    timer[4];
    DmaChan  dma[4];

    Apu      apu;
    Ppu      ppu;

    uint32_t frame_lines;         /* rendering progress (lines) */
    uint64_t frame_count;

    /* keypad (input) */
    uint16_t key_input;

    /* cheats list */
    struct Cheat {
        uint32_t addr;        /* 24-bit form */
        uint16_t value;
        uint32_t stored;
        bool     active;
        bool     is_gameshark;
    } cheats[100];
    int n_cheats;

    int      frameskip;
    int      frame_skip_counter;
    int      throttle;
    bool     paused;

    /* savestate buffer (for state save/load API) */
    uint8_t  *state_buf;
    size_t   state_size;
} GBA;

/* ---- API ------------------------------------------------------------------- */
void gba_init(GBA *g);
void gba_reset(GBA *g);
void gba_load_bios(GBA *g, const char *path);
bool gba_load_rom(GBA *g, const char *path);
void gba_set_savetype(GBA *g, int type);

void gba_run_frame(GBA *g);          /* run one frame; render into ppu.frame */
bool gba_step(GBA *g, uint32_t max_cycles); /* internal stepping helpers */

/* memory */
uint8_t  gba_read8(GBA *g, uint32_t addr);
uint16_t gba_read16(GBA *g, uint32_t addr);
uint32_t gba_read32(GBA *g, uint32_t addr);
void gba_write8(GBA *g, uint32_t addr, uint8_t v);
void gba_write16(GBA *g, uint32_t addr, uint16_t v);
void gba_write32(GBA *g, uint32_t addr, uint32_t v);

/* IO helpers */
void gba_update_keypad(GBA *g, uint16_t keys);
uint16_t gba_key_debounced(GBA *g);

/* cheats */
void gba_add_cheat(GBA *g, uint32_t addr, uint16_t value);
void gba_clear_cheats(GBA *g);
void gba_apply_cheats(GBA *g);

/* savestates */
int gba_save_state(GBA *g, const char *path);
int gba_load_state(GBA *g, const char *path);

/* ppu */
void ppu_render_frame(GBA *g);

/* apu */
void apu_open(void);
void apu_close(void);
void apu_mix(GBA *g, int16_t *stream, int nframes);
void apu_fifo_write(GBA *g, int ch, const uint8_t *bytes, int nbytes);

/* frontend utils */
uint32_t get_ticks_us(void);
void sleep_ms(int ms);

#endif