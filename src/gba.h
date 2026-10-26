/*
 * gba.h - Game Boy Advance system model.
 *
 * Revisado contra GBATEK. Cambios respecto a v1.4.7:
 *  - Ppu: mapa de registros afines/ventanas/blend corregido, referencias
 *    internas BG2/BG3 de 28 bits, flag HBlank real.
 *  - Apu: reescrito con el layout de registros de GBATEK, frame sequencer,
 *    FIFOs conducidos por los timers y ring buffer para el hilo de audio.
 *  - CpuState: estado de IntrWait (re-ejecución del SWI como el BIOS real).
 *  - GBA: bios_loaded (ya no se deduce de bios[0] != 0).
 */
#ifndef GBA_H
#define GBA_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define GBA_VERSION  "v1.4.15"
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

/* ---- Save hardware types ----------------------------------------------- */
enum {
    SAVE_NONE = 0, SAVE_SRAM, SAVE_FLASH64, SAVE_FLASH128, SAVE_EEPROM
};

/* EEPROM protocol states (GBATEK: bit-serial over the D0 line). */
enum {
    EEPROM_IDLE, EEPROM_RX_CMD, EEPROM_RX_ADDR_R, EEPROM_RX_ADDR_W,
    EEPROM_RX_DATA, EEPROM_TX_DATA, EEPROM_READY
};

#define EEPROM_SIZE 0x2000          /* 8KB = 64Kbit; 4Kbit uses the low 512B */

typedef struct {
    uint8_t  data[EEPROM_SIZE];
    int      addr_bits;             /* 0 unknown, 6 = 4Kbit, 14 = 64Kbit */
    int      state;
    uint16_t addr;
    uint16_t rx_count;
    uint64_t shift;                 /* tx/rx shift register */
    uint16_t tx_count;
    bool     dirty;                 /* a write completed; save to disk */
} Eeprom;

/* Flash command state machine (SST 39VF512 / Macronix MX29L010 protocol). */
enum {
    FLASH_READY, FLASH_CMD1, FLASH_CMD2, FLASH_AUTOSELECT,
    FLASH_PROGRAM, FLASH_ERASE
};

typedef struct {
    int      state;
    int      command;               /* latched command (FLASH_CMD_*) */
    int      bank;                  /* 1M parts: two 64K banks */
    uint8_t  manufacturer;
    uint8_t  device;
} Flash;

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
    uint16_t wait_mask;      /* IRQs awaited by SWI 04h/05h; 0 = any IRQ wakes */
} CpuState;

/* ---- Timers (GBATEK: TMxCNT_L = reload/counter, TMxCNT_H = control) ------ */
typedef struct {
    bool enabled;
    uint16_t reload;         /* valor escrito en TMxCNT_L */
    uint16_t counter;        /* contador actual (lectura de TMxCNT_L) */
    int prescaler;           /* 1,64,256,1024 */
    bool count_up;           /* cascada (no vale para el timer 0) */
    bool irq_enable;
    uint32_t clock;          /* acumulador de ciclos sub-tick */
} Timer;

/* ---- DMA ------------------------------------------------------------------ */
typedef struct {
    uint32_t src;            /* direccion interna actual */
    uint32_t dst;
    uint32_t count;          /* registro DMAxCNT_L (se recarga en cada disparo) */
    int src_ctl;
    int dst_ctl;
    int unit;
    int timing;
    bool repeat;
    bool irq_enable;
    bool enabled;
    int word;
    uint32_t src_latch;      /* valores escritos en DMAxSAD / DMAxDAD */
    uint32_t dst_latch;
} DmaChan;

/* ---- APU state ----------------------------------------------------------- */
#define SAMPLE_RATE 44100
#define APU_SR       44100

typedef struct {
    bool     on;
    int      vol;            /* volumen de la envolvente (0..15) */
    int      env_t;          /* contador de envolvente (ticks de 64 Hz) */
    int      len;            /* contador de longitud (ticks de 256 Hz) */
    int      pos;            /* posicion de duty / muestra de onda */
    uint64_t cc;             /* ciclos de CPU acumulados, punto fijo 16.16 */
} ApuCh;

typedef struct {
    bool     master;         /* SOUNDCNT_X bit 7 */
    uint16_t cnt_l;          /* SOUNDCNT_L 0x080 */
    uint16_t cnt_h;          /* SOUNDCNT_H 0x082 (sin bits de reset) */
    uint16_t bias;           /* SOUNDBIAS  0x088 */
    uint16_t r1l, r1h, r1x;  /* SOUND1CNT_L/H/X 0x60/0x62/0x64 */
    uint16_t r2l, r2h;       /* SOUND2CNT_L/H   0x68/0x6C      */
    uint16_t r3l, r3h, r3x;  /* SOUND3CNT_L/H/X 0x70/0x72/0x74 */
    uint16_t r4l, r4h;       /* SOUND4CNT_L/H   0x78/0x7C      */
    uint8_t  wave[2][16];    /* WAVE_RAM 0x90..0x9F, dos bancos */
    ApuCh    ch[4];
    int      sweep_t, shadow;
    bool     sweep_on;
    uint32_t lfsr;
    uint8_t  lfsr_out;       /* nivel actual del generador de ruido (bit de carry) */
    uint64_t fs_cc;          /* frame sequencer 512 Hz, 16.16 */
    int      fs_step;
    uint64_t sample_acc;     /* ciclos*44100 pendientes de convertir en muestras */
    int8_t   fifo[2][32];
    int      fifo_head[2], fifo_len[2];
    int8_t   fifo_held[2];
    uint64_t fifo_next[2];   /* ciclo de CPU del proximo pop, punto fijo 16.16 */
    int32_t  hp_x[2], hp_y[2];   /* filtro paso alto (quita el DC) */
} Apu;

/* ---- PPU state ------------------------------------------------------------- */
typedef struct {
    uint16_t dispcnt;
    uint16_t dispstat;          /* solo bits escribibles: 3,4,5 y LYC (8-15) */
    uint16_t vcount;
    uint16_t bgcnt[4];
    uint16_t bghofs[4];
    uint16_t bgvofs[4];
    int16_t  pa[2], pb[2], pc[2], pd[2];   /* BG2 = [0], BG3 = [1] */
    int32_t  bgx[2], bgy[2];               /* BGxX/BGxY escritos (28 bit con signo) */
    int32_t  refx[2], refy[2];             /* referencias internas por scanline */
    uint16_t winh[2], winv[2];
    uint16_t winin, winout;
    uint16_t mosaic;
    uint16_t bldcnt, bldalpha, bldy;
    bool     hblank;                       /* estado HBlank de la scanline actual */
    uint16_t keycnt;
    uint16_t keyinput;

    uint16_t frame[SCREEN_W*SCREEN_H];     /* output RGB555 (R = bits 0-4) */
} Ppu;

/* ---- GBA core --------------------------------------------------------------- */
typedef struct GBA {
    uint8_t  bios[BIOS_SIZE];
    bool     bios_enabled;
    bool     bios_fastboot;
    bool     bios_loaded;         /* se cargo un volcado real de la BIOS */
    bool     skip_intros;         /* SkipIntroHacks: enter ROM without BIOS wait */
    uint32_t bios_prefetch;       /* GBATEK: valor que devuelve una lectura a la BIOS
                                     desde fuera de ella (0x0DC+8, 0x188+8, 0x134+8) */
    uint32_t bios_last_pc;        /* ultima PC ejecutada dentro de la BIOS; 0xFFFFFFFF
                                     cuando no se esta ejecutando la BIOS */

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
    int      save_type;           /* SAVE_* */
    Eeprom   eeprom;              /* serial EEPROM (save_type == SAVE_EEPROM) */
    Flash    flash;               /* parallel flash (SAVE_FLASH64/128) */
    bool     save_dirty;          /* battery save needs flushing to disk */

    uint8_t  io[0x400];           /* I/O registers window */

    CpuRegs  cpu;
    CpuState cst;

    uint32_t cycle;               /* absolute cycle counter (16.78MHz) */

    /* ---- bus: region waitstates + GamePak prefetch buffer ----------------
       GBATEK "Memory Map" / "GamePak Prefetch".  Indexed by the top nibble of
       the address (region).  ns = first (non-sequential) access, s = following
       (sequential) accesses, 16/32 = access width.  A 32-bit unit on the
       16-bit GamePak bus is ns16 + 1 internal + s16.  The prefetch buffer
       holds up to 8 halfwords; `prefetch_pc` is the address of the next word
       the buffer will deliver, and 0xFFFFFFFF means "empty". */
    int      bus_ns16[16], bus_s16[16], bus_ns32[16], bus_s32[16];
    bool     prefetch_enable;     /* WAITCNT bit 14 */
    uint32_t prefetch_pc;         /* next address the buffer delivers */

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

    /* keypad (input): activo en bajo, igual que KEYINPUT (0 = pulsado) */
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
bool gba_save_battery(GBA *g, const char *rom_path);
void gba_load_battery(GBA *g, const char *rom_path);

void gba_run_frame(GBA *g);          /* run one frame; render into ppu.frame */
bool gba_step(GBA *g, uint32_t max_cycles); /* internal stepping helpers */

/* memory */
uint8_t  gba_read8(GBA *g, uint32_t addr);
uint16_t gba_read16(GBA *g, uint32_t addr);
uint32_t gba_read32(GBA *g, uint32_t addr);
void gba_write8(GBA *g, uint32_t addr, uint8_t v);
void gba_write16(GBA *g, uint32_t addr, uint16_t v);
void gba_write32(GBA *g, uint32_t addr, uint32_t v);

/* IO helpers. `keys` es activo en bajo (0 = pulsado), como KEYINPUT. */
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
void ppu_frame_start(GBA *g);
void ppu_render_line(GBA *g, int y);
void ppu_render_frame(GBA *g);       /* compat: renderiza las 160 lineas */

/* apu */
void apu_open(void);
void apu_close(void);
void apu_reset(GBA *g);
void apu_step(GBA *g, uint32_t cycles);                 /* genera muestras al ring */
void apu_mix(GBA *g, int16_t *stream, int nframes);     /* callback de audio */
int  apu_available(void);                              /* muestras listas en el ring */
int  apu_drain(int16_t *stream, int maxframes);          /* vacia el ring (tests) */
void apu_fifo_write(GBA *g, int ch, const uint8_t *bytes, int nbytes);
void apu_timer_overflow(GBA *g, int timer, uint32_t n);
uint16_t apu_read_reg(GBA *g, uint32_t reg);
void apu_write_reg(GBA *g, uint32_t reg, uint16_t v);

/* frontend utils */
uint32_t get_ticks_us(void);
void sleep_ms(int ms);

#endif
