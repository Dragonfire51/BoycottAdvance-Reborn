#!/usr/bin/env python3
"""Generate a synthetic ROM that records the VCOUNT at which the VBlank IRQ
flag (IF bit 0) first appears.

The ROM disables IME so IF is not consumed, then polls IF; when bit 0 is set it
reads VCOUNT and stores the low byte to EWRAM[0] (and counts events in
EWRAM[4]), then clears IF bit 0.  A correct emulator reports VCOUNT == 160 at
the VBlank flag; an off-by-one reports 159.

Usage: gen_vbl_probe.py <out.gba>
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
        assert 0 <= imm <= 255
        add(0xE1D000B0 | (rn << 16) | (rd << 12) | (((imm >> 4) & 0xF) << 8) | (imm & 0xF))

    def ldr(rd, rn, imm):
        add(0xE5900000 | (rn << 16) | (rd << 12) | (imm & 0xFFF))

    def str(rd, rn, imm):
        add(0xE5800000 | (rn << 16) | (rd << 12) | (imm & 0xFFF))

    def strb(rd, rn, imm):
        add(0xE5C00000 | (rn << 16) | (rd << 12) | (imm & 0xFFF))

    def strh(rd, rn, imm):
        assert 0 <= imm <= 255
        add(0xE1C000B0 | (rn << 16) | (rd << 12) | (((imm >> 4) & 0xF) << 8) | (imm & 0xF))

    def movi(rd, imm):
        add(0xE3A00000 | (rd << 12) | (imm & 0xFF))

    def tst(rd, imm):
        add(0xE3100000 | (rd << 16) | (imm & 0xFFF))

    def addi(rd, rn, imm):
        add(0xE2800000 | (rn << 16) | (rd << 12) | (imm & 0xFFF))

    def b(label, cond=0xE):
        add(("b", cond, label))

    def adr(rd, label):
        add(("adr", rd, label))

    def poolw(w):
        add(("pool", w))

    # pool[0]=EWRAM, pool[1]=IF base, pool[2]=IME, pool[3]=0x04000000
    L('start')
    adr(4, 'pool')
    ldr(4, 4, 0)               # r4 = 0x02000000 (EWRAM)
    adr(1, 'pool')
    ldr(1, 1, 4)               # r1 = 0x04000202 (IF)
    adr(5, 'pool')
    ldr(5, 5, 8)               # r5 = 0x04000208 (IME)
    adr(3, 'pool')
    ldr(3, 3, 12)              # r3 = 0x04000000
    movi(0, 0)
    strh(0, 5, 0)              # IME = 0 so IF is not consumed by an IRQ

    L('loop')
    ldrh(0, 1, 0)              # IF
    tst(0, 1)                  # bit0 VBlank?
    b('loop', 0x0)             # EQ -> keep polling
    ldrh(2, 3, 6)              # VCOUNT
    strb(2, 4, 0)              # EWRAM[0] = VCOUNT low byte
    ldr(6, 4, 4)               # counter
    addi(6, 6, 1)
    str(6, 4, 4)               # EWRAM[4] = count
    movi(0, 1)
    strh(0, 1, 0)              # clear IF bit0
    b('loop')

    L('pool')
    poolw(0x02000000)
    poolw(0x04000202)
    poolw(0x04000208)
    poolw(0x04000000)

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
    main(sys.argv[1] if len(sys.argv) > 1 else "/tmp/vbl.gba")
