#!/usr/bin/env python3
"""Generate a mode-4 synthetic ROM that exercises the BG2 affine transform (P1)
and BG mosaic (P2), so our renderer can be diffed against mGBA frame by frame.

The 240x160 mode-4 bitmap is a horizontal gradient of palette indices (index =
x & 0xFF); the palette maps each index to a distinct grey. BG2PA/PD scale the
bitmap and BG2X/Y translate it. A renderer that ignores the matrix draws the raw
bitmap; with the transform implemented our output matches mGBA.

Usage: gen_affine_test.py <pa> <pd> <refx> <refy> [mosaic] [out.gba]
"""
import struct, sys

def W(v): return struct.pack('<I', v & 0xffffffff)

class Asm:
    def __init__(self):
        self.ins = []; self.labels = {}
    def L(self, n): self.labels[n] = len(self.ins)
    def add(self, w): self.ins.append(("raw", w, None))
    def ldr(self, rd, rn, imm): self.add(0xE5900000 | (rn << 16) | (rd << 12) | (imm & 0xFFF))
    def str_(self, rd, rn, imm): self.add(0xE5800000 | (rn << 16) | (rd << 12) | (imm & 0xFFF))
    def strh(self, rd, rn, imm):
        v = imm & 0xFFF; lo = v & 0xF; hi = (v >> 4) & 0xF
        self.add(0xE1C000B0 | (rn << 16) | (hi << 8) | (rd << 12) | lo)
    def strb(self, rd, rn, imm):
        v = imm & 0xFFF; lo = v & 0xF; hi = (v >> 4) & 0xF
        self.add(0xE5C00000 | (rn << 16) | (hi << 8) | (rd << 12) | lo)
    def strp(self, rd, rn, imm): self.add(0xE4800000 | (rn << 16) | (rd << 12) | (imm & 0xFFF))
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
                if off < 0 or off > 4095: raise SystemExit(f"adr {off}")
                out += W(0xE28F0000 | (a << 12) | off)
            elif k == "b":
                t = addr[self.labels[b1]]; rel = (t - (addr[i] + 8)) // 4
                out += W(((a & 0xF) << 28) | (0xA << 24) | (rel & 0xFFFFFF))
        return bytes(out)

CODE_OFF = 0x100
DATA_OFF = 0x1000                 # VRAM image lives here in ROM
PAL_OFF  = 0x5000                 # palette image lives here in ROM
VRAM_LEN = 240 * 160              # one mode-4 page
PAL_LEN  = 256 * 2

def make_rom(code, vram, pal, out):
    src = open("/workspace/mgba/cinema/gba/blend/disabled-bg-semitrans-blend/test.gba", "rb").read()
    r = bytearray(0x40000); r[0:0x100] = src[0:0x100]
    r[0xC:0x10] = struct.pack("<I", 0x00000100)
    r[0x18:0x1C] = struct.pack("<I", 0xE25EF004)          # SUBS pc,lr,#4 (IRQ return)
    r[0:4] = struct.pack('<I', 0xEA000000 | ((0x08000000 + CODE_OFF - 0x08000000 - 8) // 4 & 0xFFFFFF))
    r[CODE_OFF:CODE_OFF+len(code)] = code
    r[DATA_OFF:DATA_OFF+len(vram)] = vram
    r[PAL_OFF:PAL_OFF+len(pal)] = pal
    inst = struct.pack('<I', 0xEAFFFFFE)
    for a in range(CODE_OFF + len(code), DATA_OFF, 4): r[a:a+4] = inst
    for a in range(DATA_OFF + len(vram), PAL_OFF, 4): r[a:a+4] = inst
    open(out, "wb").write(bytes(r)); print("wrote", out, "code", len(code))

def gen(pa, pd, refx, refy, mosaic, out):
    a = Asm()
    a.L('start')
    a.adr(2, 'pool')
    a.ldr(0, 2, 28)                              # r0 = 0x04000000
    a.ldr(1, 2, 0); a.str_(1, 0, 0x202)          # IE = 0
    a.ldr(1, 2, 0); a.str_(1, 0, 0x208)          # IME = 0
    # copy VRAM image (ROM 0x08000000+DATA_OFF) -> 0x06000000
    a.ldr(0, 2, 4)                               # dst = 0x06000000
    a.ldr(1, 2, 8)                               # src = image
    a.ldr(4, 2, 12)                              # words
    a.ldr(5, 2, 64)                              # r5 = 4 (byte step)
    a.L('cp')
    a.ldr(3, 1, 0); a.str_(3, 0, 0); a.add_(0, 0, 5); a.add_(1, 1, 5)
    a.subs(4, 4, 1); a.bne('cp')
    # copy palette image -> 0x05000000
    a.ldr(0, 2, 16)                              # dst = 0x05000000
    a.ldr(1, 2, 20)                              # src = palette image
    a.ldr(4, 2, 24)                              # words
    a.L('cp2')
    a.ldr(3, 1, 0); a.str_(3, 0, 0); a.add_(0, 0, 5); a.add_(1, 1, 5)
    a.subs(4, 4, 1); a.bne('cp2')
    # BG2 affine (PB=PC=0 -> pure scale) + reference point
    a.ldr(0, 2, 28)                              # r0 = 0x04000000
    a.ldr(1, 2, 32); a.strh(1, 0, 0x20)          # BG2PA
    a.ldr(1, 2, 36); a.strh(1, 0, 0x26)          # BG2PD
    a.ldr(1, 2, 40); a.strh(1, 0, 0x28)          # BG2X lo
    a.ldr(1, 2, 44); a.strh(1, 0, 0x2A)          # BG2X hi
    a.ldr(1, 2, 48); a.strh(1, 0, 0x2C)          # BG2Y lo
    a.ldr(1, 2, 52); a.strh(1, 0, 0x2E)          # BG2Y hi
    a.ldr(1, 2, 56); a.strh(1, 0, 0x4C)          # MOSAIC
    a.ldr(1, 2, 60); a.strh(1, 0, 0x00)          # DISPCNT = mode4 + BG2
    a.ldr(1, 2, 0);  a.strh(1, 0, 0x04)          # DISPSTAT = 0
    a.L('loop')
    a.b('loop')
    a.L('pool')
    a.pool(0,                                      # 0
           0x06000000, 0x08000000 + DATA_OFF, VRAM_LEN // 4,   # 4,8,12
           0x05000000, 0x08000000 + PAL_OFF, PAL_LEN // 4,     # 16,20,24
           0x04000000, pa & 0xFFFF, pd & 0xFFFF,               # 28,32,36
           refx & 0xFFFF, (refx >> 16) & 0xFFFF,               # 40,44
           refy & 0xFFFF, (refy >> 16) & 0xFFFF,               # 48,52
           mosaic & 0xFFFF, 0x0404, 4)                            # 56,60,64
    vram = bytes(((x & 0xFF) for x in range(240) for _ in range(160)))
    pal = b"".join(struct.pack('<H', ((k & 31) << 10) | ((k & 31) << 5) | (k & 31)) for k in range(256))
    make_rom(a.resolve(0x08000000 + CODE_OFF), bytes(vram), bytes(pal), out)

if __name__ == "__main__":
    pa = int(sys.argv[1], 0); pd = int(sys.argv[2], 0)
    refx = int(sys.argv[3], 0); refy = int(sys.argv[4], 0)
    mosaic = int(sys.argv[5], 0) if len(sys.argv) > 5 else 0
    out = sys.argv[6] if len(sys.argv) > 6 else "/tmp/aff.gba"
    gen(pa, pd, refx, refy, mosaic, out)
