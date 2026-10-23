#!/usr/bin/env python3
"""Compare our core framebuffer (RGB555 screen.bin) against mGBA PPM.

Usage: fb_diff.py <our_screen.bin> <ref.ppm> [--save map.ppm]

Both render the same ROM after the same number of frames. The reference PPM is
written by mgbaharness with mColor 0x00BBGGRR, our screen.bin is RGB555 with R
in bits 0-4. We expand both to 8-bit RGB and compare per-pixel.
"""
import sys

def load_ppm(path):
    with open(path, "rb") as f:
        data = f.read()
    # P6\nW H\n255\n
    parts = data.split(b"\n", 3)
    assert parts[0] == b"P6", "not P6"
    w, h = map(int, parts[1].split())
    assert w == 240 and h == 160, f"unexpected dims {w}x{h}"
    px = parts[3]
    return px, w, h

def load_rgb555(path):
    with open(path, "rb") as f:
        data = f.read()
    assert len(data) == 240 * 160 * 2, f"screen.bin size {len(data)}"
    out = bytearray(240 * 160 * 3)
    for i in range(240 * 160):
        c = data[2 * i] | (data[2 * i + 1] << 8)
        r = c & 0x1F
        g = (c >> 5) & 0x1F
        b = (c >> 10) & 0x1F
        out[3 * i] = (r << 3) | (r >> 2)
        out[3 * i + 1] = (g << 3) | (g >> 2)
        out[3 * i + 2] = (b << 3) | (b >> 2)
    return bytes(out), 240, 160

def main():
    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument("ours")
    ap.add_argument("ref")
    ap.add_argument("--save")
    args = ap.parse_args()

    ours, w, h = load_rgb555(args.ours)
    ref, rw, rh = load_ppm(args.ref)

    n = w * h
    diffs = []
    for i in range(n):
        o = ours[3 * i:3 * i + 3]
        r = ref[3 * i:3 * i + 3]
        if o != r:
            diffs.append((i % w, i // w, o, r))
    pct = 100.0 * len(diffs) / n
    print(f"total pixels={n} different={len(diffs)} ({pct:.2f}%)")

    if diffs and args.save:
        out = bytearray(3 * n)
        for i in range(n):
            o = ours[3 * i:3 * i + 3]
            r = ref[3 * i:3 * i + 3]
            if o != r:
                out[3 * i:3 * i + 3] = b"\xff\x00\xff"
            else:
                out[3 * i:3 * i + 3] = o
        with open(args.save, "wb") as f:
            f.write(b"P6\n240 160\n255\n")
            f.write(out)
        print(f"map saved to {args.save} (magenta = different)")
    print("sample diffs (x,y): ours=RGB  ref=RGB")
    for d in diffs[:10]:
        x, y, o, r = d
        print(f"  ({x:3d},{y:3d}) ours={o[0]:02X}{o[1]:02X}{o[2]:02X} ref={r[0]:02X}{r[1]:02X}{r[2]:02X}")

if __name__ == "__main__":
    main()