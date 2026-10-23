#!/usr/bin/env python3
"""Analyze per-row framebuffer differences between our core and mGBA PPMs.
Usage: fb_diff_rows.py <our_screen.bin> <ref.ppm>
"""
import argparse

def load_ppm(path):
    with open(path, "rb") as f:
        data = f.read()
    parts = data.split(b"\n", 3)
    assert parts[0] == b"P6", "not P6"
    w, h = map(int, parts[1].split())
    assert w == 240 and h == 160
    return parts[3], w, h

def load_rgb555(path):
    with open(path, "rb") as f:
        data = f.read()
    assert len(data) == 240 * 160 * 2
    return data

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("ours"); ap.add_argument("ref")
    args = ap.parse_args()
    own = load_rgb555(args.ours)
    ref, w, h = load_ppm(args.ref)
    diffs_by_row = {}
    for y in range(h):
        cnt = 0
        xs = []
        for x in range(w):
            i = y * w + x
            c0 = own[2 * i] | (own[2 * i + 1] << 8)
            r = c0 & 0x1F; g = (c0 >> 5) & 0x1F; b = (c0 >> 10) & 0x1F
            rr, gg, bb = ref[3 * i], ref[3 * i + 1], ref[3 * i + 2]
            orr = (r << 3) | (r >> 2); ogg = (g << 3) | (g >> 2); obb = (b << 3) | (b >> 2)
            if (orr, ogg, obb) != (rr, gg, bb):
                cnt += 1
                if len(xs) < 6: xs.append(x)
        if cnt:
            diffs_by_row[y] = (cnt, xs)
    total = sum(v[0] for v in diffs_by_row.values())
    print(f"rows with diffs={len(diffs_by_row)} total_px={total}")
    for y in sorted(diffs_by_row):
        cnt, xs = diffs_by_row[y]
        first = xs[0]; last = xs[-1] if cnt <= 6 else "…"
        print(f"  y={y:3d}: {cnt:4d} px  first_x={xs}")
        if cnt > 6:
            # find contiguous runs
            run_xs = sorted(x for x in range(w) if own_diff(args, own, ref, w, h, y, x))
            # compute runs
            runs = []
            start = run_xs[0]; prev = run_xs[0]
            for x in run_xs[1:]:
                if x != prev + 1:
                    runs.append((start, prev)); start = x
                prev = x
            runs.append((start, prev))
            print(f"           runs={runs[:8]}{'…' if len(runs)>8 else ''}")

def own_diff(args, own, ref, w, h, y, x):
    i = y * w + x
    c0 = own[2 * i] | (own[2 * i + 1] << 8)
    r = c0 & 0x1F; g = (c0 >> 5) & 0x1F; b = (c0 >> 10) & 0x1F
    rr, gg, bb = ref[3 * i], ref[3 * i + 1], ref[3 * i + 2]
    return ((r << 3) | (r >> 2), (g << 3) | (g >> 2), (b << 3) | (b >> 2)) != (rr, gg, bb)

if __name__ == "__main__":
    main()