#!/usr/bin/env python3
"""Run an mGBA-cinema test against the headless harness and diff the frames.

config.ini (if present):  skip=<n> frames=<n>
The harness is run once per baseline frame (frame index = skip + i) and its
screen.bin is compared to baseline_000i.png.

Usage: cinema_check.py <testdir>
"""
import os
import struct
import subprocess
import sys

HH = os.environ.get("HH", "/tmp/hh")


def read_ini(d):
    cfg = {"skip": 0, "frames": 1}
    p = os.path.join(d, "config.ini")
    if os.path.exists(p):
        for line in open(p):
            line = line.strip()
            if "=" in line and not line.startswith("["):
                k, v = line.split("=", 1)
                try:
                    cfg[k.strip()] = int(v.strip())
                except ValueError:
                    pass  # non-numeric keys (e.g. irq baselines) are ignored
    return cfg


def png_rgb(path):
    from PIL import Image
    return list(Image.open(path).convert("RGB").getdata())


def ours(prefix):
    d = open(prefix + ".screen.bin", "rb").read()
    out = []
    for i in range(240 * 160):
        v = struct.unpack_from("<H", d, 2 * i)[0]
        r = v & 0x1F
        g = (v >> 5) & 0x1F
        b = (v >> 10) & 0x1F
        out.append(((r << 3) | (r >> 2), (g << 3) | (g >> 2), (b << 3) | (b >> 2)))
    return out


def main():
    d = sys.argv[1].rstrip("/")
    rom = os.path.join(d, "test.gba")
    if not os.path.exists(rom):
        for f in sorted(os.listdir(d)):
            if f.endswith(".gba"):
                rom = os.path.join(d, f)
                break
    cfg = read_ini(d)
    pref = "/tmp/cinema_%s" % os.path.basename(d)
    worst = 0
    last_c = 0
    for i in range(cfg["frames"]):
        f = cfg["skip"] + i
        p = "%s_f%d" % (pref, f)
        subprocess.run([HH, rom, str(f + 1), "dump", p],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        bp = os.path.join(d, "baseline_%04d.png" % i)
        if not os.path.exists(bp):
            print("  (no baseline_%04d.png)" % i)
            continue
        a = ours(p)
        b = png_rgb(bp)
        c = sum(1 for x, y in zip(a, b) if x != y)
        # mGBA blends in 8-bit and we blend in 5-bit per GBATEK
        # (I = MIN(31, I1st*EVA + I2nd*EVB)), so a +-1 rounding difference is
        # not a divergence.  Count those separately from real mismatches.
        r = sum(1 for x, y in zip(a, b)
                if any(abs(x[k] - y[k]) > 1 for k in range(3)))
        worst = max(worst, r)
        last_c = c
        print("  frame %d: diff %d / 38400 (%.2f%%), of which >1/channel: %d"
              % (f, c, 100.0 * c / 38400, r))
    print("%s: worst %.2f%% (real), %.2f%% (exact)"
          % (os.path.basename(d), 100.0 * worst / 38400, 100.0 * last_c / 38400))


main()
