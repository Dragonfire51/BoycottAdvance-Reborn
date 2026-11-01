#!/usr/bin/env python3
"""Synthetic mode-0 ROM isolating the OBJ 2D character-mapping column rule (P5).

A 16x8 16-colour OBJ with base tile 31 sits at x=50.  Under "wrap within the
32-tile block" (mGBA maskLo/maskHi, our current code) the right half reads
tile 0; under "carry into the next block" (VBA-M gfxDrawSprites, c + (x>>3)*32)
it reads tile 63.  Tile 31 (left half) is green, tile 0 is red, tile 63 is blue,
so the right half of the sprite tells which rule the core uses.

Usage: gen_obj2d_test.py [out.gba]
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

CODE_OFF = 0x100; DATA_OFF = 0x1000; PAL_OFF = 0x6000

def make_rom(code, vram, pal, oam, out):
    src = open("/workspace/mgba/cinema/gba/blend/disabled-bg-semitrans-blend/test.gba", "rb").read()
    r = bytearray(0x40000); r[0:0x100] = src[0:0x100]
    r[0xC:0x10] = struct.pack("<I", 0x00000100)
    r[0x18:0x1C] = struct.pack("<I", 0xE25EF004)
    r[0:4] = struct.pack('<I', 0xEA000000 | ((CODE_OFF - 8) // 4 & 0xFFFFFF))
    r[CODE_OFF:CODE_OFF+len(code)] = code
    r[DATA_OFF:DATA_OFF+len(vram)] = vram
    r[PAL_OFF:PAL_OFF+len(pal)] = pal
    inst = struct.pack('<I', 0xEAFFFFFE)
    for a in range(CODE_OFF + len(code), DATA_OFF, 4): r[a:a+4] = inst
    for a in range(DATA_OFF + len(vram), PAL_OFF, 4): r[a:a+4] = inst
    r[PAL_OFF+len(pal):PAL_OFF+len(pal)+len(oam)] = oam
    open(out, "wb").write(bytes(r)); print("wrote", out)

def gen(out):
    NW = 64                              # 64 tiles * 32 B (4bpp) = 2048 B
    vram = bytearray(NW * 32)
    def fill(t, nib): vram[t*32:(t+1)*32] = bytes([(nib << 4) | nib]) * 32
    fill(0, 1)                           # tile 0  -> red  (wrap result)
    fill(31, 2)                          # tile 31 -> green (left half)
    fill(63, 3)                          # tile 63 -> blue (carry result)
    pal = bytearray(1024)
    def setp(i, rgb): struct.pack_into('<H', pal, i*2, rgb)
    setp(0x100, 0x0000)                  # OBJ idx0 transparent
    setp(0x101, 0x001F)                  # red
    setp(0x102, 0x03E0)                  # green
    setp(0x103, 0x7C00)                  # blue
    oam = struct.pack('<HHHH', 80, 50 | (1 << 14), 31, 0)   # 16x8, base tile 31
    a = Asm(); a.L('start')
    a.adr(2, 'pool')
    a.ldr(0, 2, 0)                               # r0 = 0x04000000
    a.ldr(1, 2, 44); a.str_(1, 0, 0x202)         # IE = 0
    a.ldr(1, 2, 44); a.str_(1, 0, 0x208)         # IME = 0
    a.ldr(0, 2, 4); a.ldr(1, 2, 8); a.ldr(4, 2, 12); a.ldr(5, 2, 44)
    a.L('cp')
    a.ldr(3, 1, 0); a.str_(3, 0, 0); a.add_(0, 0, 5); a.add_(1, 1, 5)
    a.subs(4, 4, 1); a.bne('cp')
    a.ldr(0, 2, 16); a.ldr(1, 2, 20); a.ldr(4, 2, 24)
    a.L('cp2')
    a.ldr(3, 1, 0); a.str_(3, 0, 0); a.add_(0, 0, 5); a.add_(1, 1, 5)
    a.subs(4, 4, 1); a.bne('cp2')
    a.ldr(0, 2, 28); a.ldr(1, 2, 32); a.ldr(4, 2, 36)
    a.L('cp3')
    a.ldr(3, 1, 0); a.str_(3, 0, 0); a.add_(0, 0, 5); a.add_(1, 1, 5)
    a.subs(4, 4, 1); a.bne('cp3')
    a.ldr(0, 2, 0)                               # r0 = 0x04000000
    a.ldr(1, 2, 40); a.strh(1, 0, 0x00)          # DISPCNT = mode0 + OBJ (2D map)
    a.ldr(1, 2, 44); a.strh(1, 0, 0x04)          # DISPSTAT = 0
    a.L('loop'); a.b('loop')
    a.L('pool')
    a.pool(0x04000000,                            # 0
           0x06010000, 0x08000000 + DATA_OFF, NW * 32 // 4,   # 4,8,12
           0x05000000, 0x08000000 + PAL_OFF, len(pal) // 4,   # 16,20,24
           0x07000000, 0x08000000 + PAL_OFF + len(pal), 2,    # 28,32,36
           0x1000,                                            # 40 DISPCNT
           4)                                                 # 44 step
    make_rom(a.resolve(0x08000000 + CODE_OFF), bytes(vram), bytes(pal), oam, out)

if __name__ == "__main__":
    gen(sys.argv[1] if len(sys.argv) > 1 else "/tmp/obj2d.gba")
