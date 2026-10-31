#!/usr/bin/env python3
"""Synthetic mode-4 ROM that isolates WIN0 horizontal range handling.

The bitmap is a horizontal gradient (palette index = x & 0xFF). WIN0 is enabled
with WIN0H = (x1<<8)|x2. Inside WIN0 only BG2 is shown (WININ bit2); outside,
WINOUT shows nothing (backdrop).  With x1 > x2 the window must WRAP
(inside = x >= x1 || x < x2); clamping instead yields a different image, so the
framebuffer tells which rule the core uses.  Diffed against mGBA.

Usage: gen_window_test.py <x1> <x2> [out.gba]
"""
import struct, sys

def W(v): return struct.pack('<I', v & 0xffffffff)

class Asm:
    def __init__(self): self.ins = []; self.labels = {}
    def L(self, n): self.labels[n] = len(self.ins)
    def add(self, w): self.ins.append(("raw", w, None))
    def ldr(self, rd, rn, imm): self.add(0xE5900000 | (rn << 16) | (rd << 12) | (imm & 0xFFF))
    def str_(self, rd, rn, imm): self.add(0xE5800000 | (rn << 16) | (rd << 12) | (imm & 0xFFF))
    def strh(self, rd, rn, imm):
        v = imm & 0xFFF; lo = v & 0xF; hi = (v >> 4) & 0xF
        self.add(0xE1C000B0 | (rn << 16) | (hi << 8) | (rd << 12) | lo)
    def add_(self, rd, rn, rm): self.add(0xE0800000 | (rn << 16) | (rd << 12) | rm)
    def subs(self, rd, rn, imm): self.add(0xE2500000 | (rn << 16) | (rd << 12) | (imm & 0xFF))
    def adr(self, rd, label): self.ins.append(("adr", rd, label))
    def b(self, label, cond=0xE): self.ins.append(("b", cond, label))
    def bne(self, label): self.b(label, cond=0x1)
    def pool(self, *ws):
        for w in ws: self.ins.append(("poolw", w, None))
    def resolve(self, base):
        out = bytearray(); n = len(self.ins); addr = [base + i * 4 for i in range(n)]
        for i, (k, a, b1) in enumerate(self.ins):
            if k in ("raw", "poolw"): out += W(a)
            elif k == "adr":
                t = addr[self.labels[b1]]; off = t - (addr[i] + 8)
                out += W(0xE28F0000 | (a << 12) | off)
            elif k == "b":
                t = addr[self.labels[b1]]; rel = (t - (addr[i] + 8)) // 4
                out += W(((a & 0xF) << 28) | (0xA << 24) | (rel & 0xFFFFFF))
        return bytes(out)

CODE_OFF = 0x100; DATA_OFF = 0x1000; PAL_OFF = 0x5000
VRAM_LEN = 240 * 160; PAL_LEN = 256 * 2

def make_rom(code, vram, pal, out):
    src = open("/workspace/mgba/cinema/gba/blend/disabled-bg-semitrans-blend/test.gba", "rb").read()
    r = bytearray(0x40000); r[0:0x100] = src[0:0x100]
    r[0xC:0x10] = struct.pack("<I", 0x00000100)
    r[0x18:0x1C] = struct.pack("<I", 0xE25EF004)
    r[0:4] = struct.pack('<I', 0xEA000000 | ((0x08000000 + CODE_OFF - 0x08000000 - 8) // 4 & 0xFFFFFF))
    r[CODE_OFF:CODE_OFF+len(code)] = code
    r[DATA_OFF:DATA_OFF+len(vram)] = vram
    r[PAL_OFF:PAL_OFF+len(pal)] = pal
    inst = struct.pack('<I', 0xEAFFFFFE)
    for a in range(CODE_OFF + len(code), DATA_OFF, 4): r[a:a+4] = inst
    for a in range(DATA_OFF + len(vram), PAL_OFF, 4): r[a:a+4] = inst
    open(out, "wb").write(bytes(r)); print("wrote", out)

def gen(x1, x2, out):
    a = Asm(); a.L('start')
    a.adr(2, 'pool')
    a.ldr(0, 2, 28)                              # r0 = 0x04000000
    a.ldr(1, 2, 0); a.str_(1, 0, 0x202)          # IE = 0
    a.ldr(1, 2, 0); a.str_(1, 0, 0x208)          # IME = 0
    a.ldr(0, 2, 4); a.ldr(1, 2, 8); a.ldr(4, 2, 12); a.ldr(5, 2, 56)
    a.L('cp')
    a.ldr(3, 1, 0); a.str_(3, 0, 0); a.add_(0, 0, 5); a.add_(1, 1, 5)
    a.subs(4, 4, 1); a.bne('cp')
    a.ldr(0, 2, 16); a.ldr(1, 2, 20); a.ldr(4, 2, 24)
    a.L('cp2')
    a.ldr(3, 1, 0); a.str_(3, 0, 0); a.add_(0, 0, 5); a.add_(1, 1, 5)
    a.subs(4, 4, 1); a.bne('cp2')
    a.ldr(0, 2, 28)                              # r0 = 0x04000000
    a.ldr(1, 2, 40); a.strh(1, 0, 0x40)          # WIN0H = (x1<<8)|x2
    a.ldr(1, 2, 44); a.strh(1, 0, 0x44)          # WIN0V = (0<<8)|160
    a.ldr(1, 2, 48); a.strh(1, 0, 0x48)          # WININ = BG2 inside WIN0 + effects
    a.ldr(1, 2, 52); a.strh(1, 0, 0x4A)          # WINOUT = 0 (backdrop outside)
    a.ldr(1, 2, 32); a.strh(1, 0, 0x00)          # DISPCNT = mode4 + BG2 + WIN0
    a.ldr(1, 2, 0);  a.strh(1, 0, 0x04)
    a.L('loop'); a.b('loop')
    a.L('pool')
    a.pool(0,
           0x06000000, 0x08000000 + DATA_OFF, VRAM_LEN // 4,
           0x05000000, 0x08000000 + PAL_OFF, PAL_LEN // 4,
           0x04000000,
           0x2404,                                    # DISPCNT
           0,                                         # (unused)
           ((x1 & 0xFF) << 8) | (x2 & 0xFF),          # WIN0H
           (0 << 8) | 160,                            # WIN0V
           0x0024,                                    # WININ: BG2 + effects inside WIN0
           0x0000,                                    # WINOUT: nothing outside
           4)
    vram = bytes(((x & 0xFF) for _y in range(160) for x in range(240)))
    pal = b"".join(struct.pack('<H', ((k & 31) << 10) | ((k & 31) << 5) | (k & 31)) for k in range(256))
    make_rom(a.resolve(0x08000000 + CODE_OFF), bytes(vram), bytes(pal), out)

if __name__ == "__main__":
    gen(int(sys.argv[1], 0), int(sys.argv[2], 0), sys.argv[3] if len(sys.argv) > 3 else "/tmp/win.gba")
