/* Real-time clock (Seiko S-3511) mapped through the cartridge GPIO port at
 * 0x080000C4..C8.  Pokemon Emerald/Ruby/Sapphire probe it and show "the
 * internal battery has run dry" when it does not answer.
 *
 * Protocol ported from mGBA src/gba/cart/gpio.c (_rtcReadPins / _rtcBeginCommand
 * / _rtcProcessByte / _rtcOutput / _rtcUpdateClock) and GBATEK "GBA Cartridge
 * GPIO" / "RTC".  Pins: bit0 = SCK, bit1 = SIO, bit2 = CS.
 */
/* localtime_r es POSIX, no ISO C: hay que pedirlo antes de cualquier include
   para que -std=c99 lo declare (si no, se asume int y el anio sale mal). */
#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif
#include <string.h>
#include <time.h>
#include "gba.h"
#include "mem.h"

#if defined(_WIN32)
#define localtime_r(t, tm) localtime_s((tm), (t))
#endif

/* Number of data bytes each command exchanges (index = command number). */
static const int RTC_BYTES[8] = { 0, 0, 7, 0, 1, 0, 3, 0 };

static unsigned rtc_bcd(unsigned v) {
    return (v % 10) | ((v / 10 % 10) << 4);
}

static void rtc_update_clock(GBA *g) {
    Rtc *r = &g->rtc;
    time_t t = time(NULL);
    struct tm date;
    localtime_r(&t, &date);
    r->time[0] = (uint8_t)rtc_bcd(date.tm_year - 100);   /* years since 2000 */
    r->time[1] = (uint8_t)rtc_bcd(date.tm_mon + 1);
    r->time[2] = (uint8_t)rtc_bcd(date.tm_mday);
    r->time[3] = (uint8_t)rtc_bcd(date.tm_wday);
    r->time[4] = (uint8_t)rtc_bcd((r->control & 0x40) ? date.tm_hour : date.tm_hour % 12);
    r->time[5] = (uint8_t)rtc_bcd(date.tm_min);
    r->time[6] = (uint8_t)rtc_bcd(date.tm_sec);
}

static void rtc_output_pins(GBA *g, unsigned pins) {
    Rtc *r = &g->rtc;
    r->pin_state = (uint8_t)((r->pin_state & r->direction) | (pins & ~r->direction & 0xF));
}

static void rtc_begin_command(GBA *g) {
    Rtc *r = &g->rtc;
    uint32_t cmd = r->bits;
    if ((cmd & 0xF) == 0x6) {                    /* magic */
        r->command = cmd;
        r->bytes_remaining = RTC_BYTES[(cmd >> 4) & 7];
        r->command_active = true;
        switch ((cmd >> 4) & 7) {
        case 0: r->control = 0; break;           /* force reset */
        case 2: case 6: rtc_update_clock(g); break;  /* datetime / time */
        default: break;
        }
    }
    r->bits = 0;
    r->bits_read = 0;
}

static void rtc_process_byte(GBA *g) {
    Rtc *r = &g->rtc;
    if (((r->command >> 4) & 7) == 4) r->control = (uint8_t)r->bits;
    r->bits = 0;
    r->bits_read = 0;
    if (--r->bytes_remaining <= 0)
        r->bytes_remaining = RTC_BYTES[(r->command >> 4) & 7];
}

static unsigned rtc_output_bit(GBA *g) {
    Rtc *r = &g->rtc;
    uint8_t out = 0xFF;
    switch ((r->command >> 4) & 7) {
    case 4: out = r->control; break;
    case 2: case 6: out = r->time[7 - r->bytes_remaining]; break;
    default: break;
    }
    return (out >> r->bits_read) & 1;
}

/* Sampled on every GPIO data/direction write: shifts a command bit in on the
   SCK rising edge and a data bit out on the falling edge. */
static void rtc_read_pins(GBA *g) {
    Rtc *r = &g->rtc;
    rtc_output_pins(g, r->pin_state & 2);

    if (!(r->pin_state & 4)) {                   /* CS low aborts the transfer */
        r->bits_read = 0;
        r->bytes_remaining = 0;
        r->command_active = false;
        r->command = 0;
        r->sck_edge = true;
        r->sio_output = true;
        rtc_output_pins(g, 2);
        return;
    }

    if (!r->command_active) {
        rtc_output_pins(g, 2);
        if (!(r->pin_state & 1)) {
            r->bits &= ~(1u << r->bits_read);
            r->bits |= (uint32_t)((r->pin_state & 2) >> 1) << r->bits_read;
        }
        if (!r->sck_edge && (r->pin_state & 1)) {
            if (++r->bits_read == 8) rtc_begin_command(g);
        }
    } else if (!(r->command & 0x80)) {           /* write command: SIO is data */
        rtc_output_pins(g, 2);
        if (!(r->pin_state & 1)) {
            r->bits &= ~(1u << r->bits_read);
            r->bits |= (uint32_t)((r->pin_state & 2) >> 1) << r->bits_read;
        }
        if (!r->sck_edge && (r->pin_state & 1)) {
            if (((r->bits >> r->bits_read) & 1) ^ ((r->pin_state & 2) >> 1))
                r->bits &= ~(1u << r->bits_read);
            if (++r->bits_read == 8) rtc_process_byte(g);
        }
    } else {                                     /* read command: SIO is output */
        if (r->sck_edge && !(r->pin_state & 1)) {
            r->sio_output = rtc_output_bit(g);
            if (++r->bits_read == 8) {
                if (--r->bytes_remaining <= 0)
                    r->bytes_remaining = RTC_BYTES[(r->command >> 4) & 7];
                r->bits_read = 0;
            }
        }
        rtc_output_pins(g, r->sio_output << 1);
    }
    r->sck_edge = !!(r->pin_state & 1);
}

void rtc_init(GBA *g) {
    Rtc *r = &g->rtc;
    memset(r, 0, sizeof *r);
    r->sck_edge = true;
    r->sio_output = true;
    r->control = 0x40;                           /* 24h mode */
}

void rtc_write(GBA *g, uint32_t reg, uint16_t value) {
    Rtc *r = &g->rtc;
    switch (reg) {
    case 0xC4:                                   /* data (pin latch) */
        r->write_latch = value & 0xF;
        r->pin_state = (uint8_t)((r->pin_state & ~r->direction) | (r->write_latch & r->direction));
        rtc_read_pins(g);
        break;
    case 0xC6:                                   /* direction */
        r->direction = value & 0xF;
        r->pin_state = (uint8_t)((r->pin_state & ~r->direction) | (r->write_latch & r->direction));
        rtc_read_pins(g);
        break;
    case 0xC8:                                   /* control: read/write enable */
        r->read_write = value & 1;
        break;
    default:
        break;
    }
}

uint16_t rtc_read(GBA *g, uint32_t reg) {
    Rtc *r = &g->rtc;
    if (!r->read_write) return 0;                /* GPIO not readable */
    switch (reg) {
    case 0xC4: return r->pin_state;
    case 0xC6: return r->direction;
    case 0xC8: return r->read_write;
    default:   return 0;
    }
}
