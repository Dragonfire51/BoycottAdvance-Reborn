#!/usr/bin/env python3
"""Generate a tiny, self-contained GBA ROM for the Android/JNI smoke test.

The CI has no commercial ROM, but the JNI parity check needs *a* ROM to load
and run. This builds a minimal valid cartridge whose code sets DISPCNT to
bitmap mode 3, fills the 240x160 framebuffer with a fixed pattern and then
spins forever. The output is deterministic, so the frame produced through the
JNI layer must be bit-identical to the one produced by the same core on the
desktop - which is exactly what tools/android_jni_test.sh checks.

Usage: python3 tools/gen_android_smoke.py out.gba
"""
import struct
import sys

ROM_SIZE = 0x8000          # 32 KB
ENTRY = 0x08000000         # cartridge ROM base


def arm_ldr_pc(rd, pc_rel):
    """LDR rd, [pc, #pc_rel]  (pc = instr+8)."""
    assert 0 <= pc_rel < 4096 and pc_rel % 4 == 0
    return 0xE59F0000 | (rd << 12) | pc_rel


def arm_strh(rd, rn):
    """STRH rd, [rn]."""
    return 0xE1C000B0 | (rn << 16) | (rd << 12)


def arm_strh_post(rd, rn):
    """STRH rd, [rn], #2."""
    return 0xE0C000B2 | (rn << 16) | (rd << 12)


def arm_subs_imm(rd, imm):
    """SUBS rd, rd, #imm."""
    return 0xE2500000 | (rd << 16) | (rd << 12) | (imm & 0xFF)


def arm_b(off):
    """B pc+off."""
    return 0xEA000000 | ((off >> 2) & 0xFFFFFF)


def arm_bne(off):
    """BNE pc+off."""
    return 0x1A000000 | ((off >> 2) & 0xFFFFFF)


def build():
    # Layout: instructions first, then a literal pool.
    #   r0 = 0x04000000 (DISPCNT)   r1 = 0x0403 (mode 3, BG2 on)
    #   r0 = 0x06000000 (VRAM)      r1 = 0x001F (red)   r2 = 38400 (halfwords)
    # fill: STRH r1,[r0],#2 ; SUBS r2,r2,#1 ; BNE fill ; hang: B hang
    n_instr = 10
    lit_base = n_instr * 4

    lits = [0x04000000, 0x00000403, 0x06000000, 0x0000001F, 38400]
    lit_off = {v: lit_base + i * 4 for i, v in enumerate(lits)}

    def ldr(rd, val, at):
        # PC-relative offset from this instruction to the literal.
        target = lit_off[val]
        return arm_ldr_pc(rd, target - (at + 8))

    code = []
    code.append(ldr(0, 0x04000000, len(code) * 4))
    code.append(ldr(1, 0x00000403, len(code) * 4))
    code.append(arm_strh(1, 0))
    code.append(ldr(0, 0x06000000, len(code) * 4))
    code.append(ldr(1, 0x0000001F, len(code) * 4))
    code.append(ldr(2, 38400, len(code) * 4))
    fill_idx = len(code)
    code.append(arm_strh_post(1, 0))
    code.append(arm_subs_imm(2, 1))
    code.append(arm_bne((fill_idx - (len(code))) * 4))   # to fill
    hang_idx = len(code)
    code.append(arm_b(0))                                # b hang (self)

    out = bytearray()
    for w in code:
        out += struct.pack("<I", w)
    for v in lits:
        out += struct.pack("<I", v)

    # Header: branch to the entry point at the start of the ROM.
    header = bytearray(0xC0)
    struct.pack_into("<I", header, 0, arm_b(0))          # b 0x08000000
    title = b"BARS SMOKE"
    header[0xA0:0xA0 + len(title)] = title
    header[0xAC:0xB0] = b"BARS"                          # game code
    header[0xB0:0xB2] = b"01"                            # maker code
    header[0xB2] = 0x96                                  # fixed value

    rom = bytes(header) + bytes(out)
    if len(rom) < ROM_SIZE:
        rom = rom + b"\xFF" * (ROM_SIZE - len(rom))
    return rom[:ROM_SIZE]


def main():
    if len(sys.argv) < 2:
        print("usage: gen_android_smoke.py out.gba", file=sys.stderr)
        return 2
    data = build()
    with open(sys.argv[1], "wb") as f:
        f.write(data)
    print("wrote %s (%d bytes)" % (sys.argv[1], len(data)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
