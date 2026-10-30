#!/usr/bin/env python3
"""Generate a synthetic Direct Sound test ROM.

Sets up DMA1 (timing 3) feeding FIFO A from an EWRAM sawtooth table, clocked
by Timer 0, then idles.  The APU output is therefore a known sawtooth whose
frequency = timer_rate / table_length, which makes any FIFO/DMA/mixing error
visible as a wrong or glitched waveform when compared against mGBA.

Usage: gen_sound_test.py <out.gba> [timer_period] [cnt_h] [table_words]
"""
import struct
import sys


def W(v):
    return struct.pack('<I', v & 0xFFFFFFFF)


class Asm:
    def __init__(self):
        self.ins = []
        self.labels = {}

    def L(self, n):
        self.labels[n] = len(self.ins)

    def add(self, w):
        self.ins.append(("raw", w, None))

    def ldr(self, rd, rn, imm):
        self.add(0xE5900000 | (rn << 16) | (rd << 12) | (imm & 0xFFF))

    def str(self, rd, rn, imm):
        self.add(0xE5800000 | (rn << 16) | (rd << 12) | (imm & 0xFFF))

    def strh(self, rd, rn, imm):
        v = imm & 0xFFF
        lo = v & 0xF
        hi = (v >> 4) & 0xF
        self.add(0xE1C000B0 | (rn << 16) | (hi << 8) | (rd << 12) | lo)

    def adr(self, rd, label):
        self.ins.append(("adr", rd, label))

    def b(self, label, cond=0xE):
        self.ins.append(("b", cond, label))

    def pool(self, *ws):
        for w in ws:
            self.ins.append(("poolw", w, None))

    def resolve(self, base):
        out = bytearray()
        n = len(self.ins)
        addr = [base + i * 4 for i in range(n)]
        for i, (k, a, b1) in enumerate(self.ins):
            if k == "raw":
                out += W(a)
            elif k == "poolw":
                out += W(a)
            elif k == "adr":
                t = addr[self.labels[b1]]
                off = t - (addr[i] + 8)
                if off < 0 or off > 4095:
                    raise SystemExit(f"adr {off}")
                out += W(0xE28F0000 | (a << 12) | off)
            elif k == "b":
                t = addr[self.labels[b1]]
                rel = (t - (addr[i] + 8)) // 4
                out += W(((a & 0xF) << 28) | (0xA << 24) | (rel & 0xFFFFFF))
        return bytes(out)


def make_rom(code, out):
    src = open("/workspace/mgba/cinema/gba/blend/disabled-bg-semitrans-blend/test.gba", "rb").read()
    r = bytearray(0x40000)
    r[0:0x100] = src[0:0x100]
    r[0xC:0x10] = struct.pack("<I", 0x00000100)
    r[0x18:0x1C] = struct.pack("<I", 0xE25EF004)
    r[0:4] = struct.pack('<I', 0xEA000000 | ((0x08000100 - 0x08000000 - 8) // 4 & 0xFFFFFF))
    r[0x100:0x100 + len(code)] = code
    inst = struct.pack('<I', 0xEAFFFFFE)
    for a in range(0x100 + len(code), 0x40000, 4):
        r[a:a + 4] = inst
    open(out, "wb").write(bytes(r))
    print("wrote", out, "code", len(code))


def build(out, timer_period=380, cnt_h=0x0304, table_words=8):
    a = Asm()
    a.L('start')
    a.adr(2, 'pool')
    a.ldr(0, 2, 4*0)          # r0 = 0x04000000 (IO base, regs < 0x100)
    a.ldr(4, 2, 4*1)          # r4 = 0x04000100 (timer regs)
    a.ldr(3, 2, 4*2)          # r3 = 0x02000000 (EWRAM)

    # sawtooth table: byte i = i * 256/len, packed 4 bytes per word
    table = []
    total = table_words * 4
    for k in range(table_words):
        w = 0
        for j in range(4):
            w |= ((k * 4 + j) * 256 // total & 0xFF) << (8 * j)
        table.append(w)

    # pool: [0]=IO base, [1]=timer base, [2]=EWRAM base, then the table,
    # then the one-shot config words.
    pool = [0x04000000, 0x04000100, 0x02000000] + table
    i_dad = len(pool); pool.append(0x040000A0)          # DMA1 destination = FIFO A
    i_cnt = len(pool); pool.append(table_words)          # DMA1 word count
    i_sch = len(pool); pool.append(cnt_h)                # SOUNDCNT_H
    i_sx  = len(pool); pool.append(0x0080)               # SOUNDCNT_X master on
    i_tr  = len(pool); pool.append(0x10000 - timer_period)  # Timer0 reload
    i_tc  = len(pool); pool.append(0x0080)               # Timer0 CNT_H: enable, /1
    i_dch = len(pool); pool.append(0xB660)               # DMA1 CNT_H: on, timing 3, 32b, repeat

    # copy the sawtooth table into EWRAM
    for k in range(table_words):
        a.ldr(1, 2, 4*(3 + k))
        a.str(1, 3, 4 * k)
    # DMA1 source/destination/count
    a.ldr(1, 2, 4*2);      a.str(1, 0, 0xBC)               # SAD = EWRAM
    a.ldr(1, 2, 4*i_dad);  a.str(1, 0, 0xC0)               # DAD = FIFO A
    a.ldr(1, 2, 4*i_cnt);  a.str(1, 0, 0xC4)               # CNT_L
    # sound + timer config
    a.ldr(1, 2, 4*i_sch);  a.strh(1, 0, 0x82)              # SOUNDCNT_H
    a.ldr(1, 2, 4*i_sx);   a.strh(1, 0, 0x84)              # SOUNDCNT_X
    a.ldr(1, 2, 4*i_tr);   a.strh(1, 4, 0x00)              # Timer0 reload
    a.ldr(1, 2, 4*i_tc);   a.strh(1, 4, 0x02)              # Timer0 CNT_H
    a.ldr(1, 2, 4*i_dch);  a.strh(1, 0, 0xC6)              # DMA1 CNT_H (enable last)
    a.L('loop')
    a.b('loop')
    a.L('pool')
    a.pool(*pool)
    make_rom(a.resolve(0x08000100), out)


def build_psg(out, nr21=0x80, nr22=0xF1, freq=0x7C0):
    """Synthetic PSG square-channel test (the menu "blip").

    Channel 2 is triggered with a decreasing envelope, which is exactly what
    Pokemon writes for its menu cursor sound.  Nothing else runs, so the
    captured PCM is a pure square wave whose amplitude must decay from the
    initial volume to zero over `step` ticks of the 512 Hz frame sequencer.
    """
    a = Asm()
    a.L('start')
    a.adr(2, 'pool')
    a.ldr(0, 2, 4*0)              # r0 = 0x04000000 (IO base)

    pool = [0x04000000]
    i_sx = len(pool); pool.append(0x0080)   # SOUNDCNT_X: master on
    i_sl = len(pool); pool.append(0xFF77)   # SOUNDCNT_L: max vol, all ch both sides
    i_sh = len(pool); pool.append(0x0002)   # SOUNDCNT_H: PSG 100%
    i_n21 = len(pool); pool.append((nr22 << 8) | nr21)  # NR21|NR22 at 0x068
    i_n24 = len(pool); pool.append(freq | 0x8000)       # NR23/NR24 + trigger

    a.ldr(1, 2, 4*i_sx);  a.strh(1, 0, 0x84)
    a.ldr(1, 2, 4*i_sl);  a.strh(1, 0, 0x80)
    a.ldr(1, 2, 4*i_sh);  a.strh(1, 0, 0x82)
    a.ldr(1, 2, 4*i_n21); a.strh(1, 0, 0x68)   # duty + length + volume + envelope
    a.ldr(1, 2, 4*i_n24); a.strh(1, 0, 0x6C)   # frequency + trigger
    a.L('loop')
    a.b('loop')
    a.L('pool')
    a.pool(*pool)
    make_rom(a.resolve(0x08000100), out)


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "psg":
        out = sys.argv[2] if len(sys.argv) > 2 else "/workspace/BA/tmp_psg.gba"
        nr22 = int(sys.argv[3], 0) if len(sys.argv) > 3 else 0xF1
        freq = int(sys.argv[4], 0) if len(sys.argv) > 4 else 0x7C0
        build_psg(out, nr22=nr22, freq=freq)
    else:
        out = sys.argv[1] if len(sys.argv) > 1 else "/workspace/BA/tmp_ds.gba"
        tp = int(sys.argv[2]) if len(sys.argv) > 2 else 380
        ch = int(sys.argv[3], 0) if len(sys.argv) > 3 else 0x0304
        tw = int(sys.argv[4]) if len(sys.argv) > 4 else 8
        build(out, tp, ch, tw)
