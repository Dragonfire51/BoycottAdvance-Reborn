#!/usr/bin/env python3
"""Run the alyosha-tas/gba-tests suite against the headless harness.

Each ROM renders its verdict as mode-4 BG2 text; the harness dumps VRAM and the
verdict is decoded from the 8x8 glyphs in lib/glyphs.asm (the ROM expands each
glyph bit to one 0/1 byte per pixel).

Env:
  SUITE_ROOT  gba-tests checkout      (default /tmp/gba-tests)
  HH          headless harness binary (default /tmp/hh)
  BIOS        GBA BIOS image          (default <repo>/gba_bios.bin)
  FRAMES      frames per ROM          (default 400)
  OUT         scratch dump directory  (default /tmp/gba-suite-out)
"""
import glob
import os
import re
import subprocess
import sys

ROOT = os.environ.get("SUITE_ROOT", "/tmp/gba-tests")
HH = os.environ.get("HH", "/tmp/hh")
BIOS = os.environ.get("BIOS", os.path.join(os.path.dirname(__file__), "..", "gba_bios.bin"))
FRAMES = int(os.environ.get("FRAMES", "400"))
OUT = os.environ.get("OUT", "/tmp/gba-suite-out")


def load_glyphs():
    txt = open(os.path.join(ROOT, "lib", "glyphs.asm")).read()
    words = []
    for m in re.finditer(r"dw\s+([0-9A-Fa-fx,]+)", txt):
        for v in m.group(1).split(","):
            words.append(int(v.strip(), 16))
    glyphs = {}
    for g in range(96):
        raw = b"".join(words[g * 2 + k].to_bytes(4, "little") for k in range(2))
        blk = bytearray()
        for row in range(8):
            b = raw[row]
            for c in range(8):
                blk.append((b >> c) & 1)
        glyphs[bytes(blk)] = chr(32 + g)
    return glyphs


GLYPHS = load_glyphs()


def read_text(vram, y0, x0, n):
    out = []
    for c in range(n):
        x = x0 + c * 8
        blk = bytearray()
        for row in range(8):
            off = (y0 + row) * 240 + x
            blk += vram[off:off + 8]
        blk = bytes(blk)
        best, bestscore = "?", -1
        for pat, ch in GLYPHS.items():
            s = sum(1 for a, b in zip(pat, blk) if a == b)
            if s > bestscore:
                bestscore, best = s, ch
        out.append(best if bestscore >= 60 else "?")
    return "".join(out)


def verdict(prefix):
    try:
        vram = open(prefix + ".vram.bin", "rb").read()
    except OSError:
        return None, ""
    # The text starts at a per-ROM column; sweep a few to be safe.
    for x0 in (56, 58, 60, 62, 64):
        txt = read_text(vram, 76, x0, 48).replace("\x7f", " ")
        if "All tests passed" in txt:
            return "PASS", ""
        m = re.search(r"Failed test (\d+)", txt)
        if m:
            return "FAIL", m.group(1)
    txt = read_text(vram, 76, 56, 48).replace("\x7f", " ").replace("?", " ")
    return ("FAIL", "?") if "Failed" in txt else (None, txt.strip())


def main():
    os.makedirs(OUT, exist_ok=True)
    roms = sorted(glob.glob(os.path.join(ROOT, "**", "*.gba"), recursive=True))
    results = {}
    for rom in roms:
        name = os.path.relpath(rom, ROOT)
        pre = os.path.join(OUT, "cur")
        env = dict(os.environ, BIOS=BIOS)
        try:
            subprocess.run([HH, rom, str(FRAMES), "dump", pre],
                           capture_output=True, timeout=180, env=env)
        except subprocess.TimeoutExpired:
            results[name] = ("TIMEOUT", "")
            continue
        v, d = verdict(pre)
        results[name] = (v or "UNKNOWN", d)
    npass = sum(1 for v, _ in results.values() if v == "PASS")
    nfail = sum(1 for v, _ in results.values() if v == "FAIL")
    nunk = len(results) - npass - nfail
    for k in sorted(results):
        v, d = results[k]
        if v != "PASS":
            print(f"{v:8} {k}" + (f"  [{d}]" if d else ""))
    print(f"\nPASSED {npass} / FAILED {nfail} / UNKNOWN {nunk} / TOTAL {len(results)}")


if __name__ == "__main__":
    main()
