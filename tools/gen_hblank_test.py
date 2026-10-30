#!/usr/bin/env python3
"""Generate a synthetic ROM that counts how often the DISPSTAT HBlank flag
(bit 1) reads as 1 while the ROM polls it in a tight loop.

The ROM waits for VBlank to clear, then polls DISPSTAT 256 times counting bit 1
hits, stores the count in EWRAM[0] and repeats.  A correct emulator sees a
non-zero count (mGBA included); if ppu.hblank is never raised the count is 0.

Usage: gen_hblank_test.py <out.gba>
"""
import struct
import sys


def W(v):
    return struct.pack('<I', v & 0xFFFFFFFF)


def main(out):
    ins = []
    labels = {}

    def L(n):
        labels[n] = len(ins)

    def add(w):
        ins.append(w)

    def ldrh(rd, rn, imm):
        add(0xE1D000B0 | (rn << 16) | (rd << 12) | (((imm >> 4) & 0xF) << 8) | (imm & 0xF))

    def ldr(rd, rn, imm):
        add(0xE5900000 | (rn << 16) | (rd << 12) | (imm & 0xFFF))

    def str(rd, rn, imm):
        add(0xE5800000 | (rn << 16) | (rd << 12) | (imm & 0xFFF))

    def movi(rd, imm):
        add(0xE3A00000 | (rd << 12) | (imm & 0xFF))

    def tst(rd, imm):
        add(0xE3100000 | (rd << 16) | (imm & 0xFFF))

    def addi(rd, rn, imm, cond=0xE):
        add(((cond & 0xF) << 28) | 0x02800000 | (rn << 16) | (rd << 12) | (imm & 0xFFF))

    def subs(rd, rn, imm):
        add(0xE2500000 | (rn << 16) | (rd << 12) | (imm & 0xFFF))

    def b(label, cond=0xE):
        add(("b", cond, label))

    def adr(rd, label):
        add(("adr", rd, label))

    def poolw(w):
        add(("pool", w))

    L('start')
    adr(1, 'pool')
    ldr(1, 1, 0)               # r1 = 0x04000000
    adr(4, 'pool')
    ldr(4, 4, 4)               # r4 = 0x02000000

    L('wait_vbl')
    ldrh(0, 1, 4)              # DISPSTAT
    tst(0, 1)                  # bit0 VBlank?
    b('wait_vbl', 0x0)         # EQ -> keep waiting

    L('wait_clear')
    ldrh(0, 1, 4)
    tst(0, 1)
    b('wait_clear', 0x1)       # NE -> keep waiting until VBlank clears

    movi(2, 0)                 # r2 = hits
    movi(3, 1)
    add(0xE1A03403)            # lsl r3,r3,#8 -> 0x100 (256 iterations)

    L('poll')
    ldrh(0, 1, 4)
    tst(0, 2)                  # bit1 HBlank?
    addi(2, 2, 1, cond=0x1)    # NE: r2++
    subs(3, 3, 1)
    b('poll', 0x1)             # NE -> loop
    str(2, 4, 0)               # EWRAM[0] = hits
    b('wait_vbl')

    L('pool')
    poolw(0x04000000)
    poolw(0x02000000)

    base = 0x08000100
    addr = [base + i * 4 for i in range(len(ins))]
    code = bytearray()
    for i, x in enumerate(ins):
        if isinstance(x, int):
            code += W(x)
        elif x[0] == 'b':
            _, cond, lab = x
            rel = (addr[labels[lab]] - (addr[i] + 8)) // 4
            code += W(((cond & 0xF) << 28) | (0xA << 24) | (rel & 0xFFFFFF))
        elif x[0] == 'adr':
            _, rd, lab = x
            off = addr[labels[lab]] - (addr[i] + 8)
            assert 0 <= off <= 4095, off
            code += W(0xE28F0000 | (rd << 12) | off)
        elif x[0] == 'pool':
            code += W(x[1])

    src = open("/workspace/mgba/cinema/gba/blend/disabled-bg-semitrans-blend/test.gba", "rb").read()
    r = bytearray(0x40000)
    r[0:0x100] = src[0:0x100]
    r[0xC:0x10] = struct.pack("<I", 0x00000100)
    r[0x18:0x1C] = struct.pack("<I", 0xE25EF004)
    r[0:4] = struct.pack('<I', 0xEA000000 | ((0x08000100 - 0x08000000 - 8) // 4 & 0xFFFFFF))
    r[0x100:0x100 + len(code)] = code
    loop = struct.pack('<I', 0xEAFFFFFE)
    for a in range(0x100 + len(code), 0x40000, 4):
        r[a:a + 4] = loop
    open(out, "wb").write(bytes(r))
    print("wrote", out, "code", len(code))


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "/tmp/hblank.gba")
