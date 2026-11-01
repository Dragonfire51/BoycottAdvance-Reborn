#!/usr/bin/env python3
"""Compare a run of frames from our emulator against VBA-M / mGBA.

Usage:
  fb_compare.py <dirA> <dirB> [--extA png] [--extB ppm] [--step N] [--perm rgb|bgr]
Both dirs contain frame_%06d.<ext>. Reports per-frame mean abs diff and the
best matching offset within a window (phase alignment), because the two
emulators reach the same screen at slightly different frame indices.
"""
import sys, os, itertools
import numpy as np
from PIL import Image


def load(path):
    im = Image.open(path).convert('RGB')
    return np.asarray(im, dtype=np.int16)


def main():
    a, b = sys.argv[1], sys.argv[2]
    extA, extB, step, perm = 'png', 'ppm', 10, 'rgb'
    args = sys.argv[3:]
    for i, x in enumerate(args):
        if x == '--extA':
            extA = args[i + 1]
        elif x == '--extB':
            extB = args[i + 1]
        elif x == '--step':
            step = int(args[i + 1])
        elif x == '--perm':
            perm = args[i + 1]

    def find(d, f, ext):
        for w in (5, 6, 3):
            p = os.path.join(d, ('frame_%0' + str(w) + 'd.%s') % (f, ext))
            if os.path.exists(p):
                return p
        return None

    def A(f):
        return find(a, f, extA)

    def B(f):
        return find(b, f, extB)

    def nframes(d, ext):
        n = 0
        while find(d, n, ext) is not None:
            n += 1
        return n

    na, nb = nframes(a, extA), nframes(b, extB)
    print('frames: A=%d B=%d' % (na, nb))

    def permute(img):
        if perm == 'bgr':
            return img[:, :, ::-1]
        return img

    ref_cache = {}
    print('%8s %10s %10s %8s' % ('frame', 'bestk', 'mindiff', 'selfdiff'))
    for f in range(0, min(na, nb), step):
        pa = A(f)
        if pa is None:
            continue
        ia = permute(load(pa))
        best = None
        for k in range(-6, 7):
            g = f + k
            if g < 0 or g >= nb:
                continue
            pg = B(g)
            if pg is None:
                continue
            if g not in ref_cache:
                ref_cache[g] = permute(load(pg))
            d = float(np.abs(ia - ref_cache[g]).mean())
            if best is None or d < best[1]:
                best = (k, d)
        # diff against same index
        pf = B(f)
        if pf is None:
            continue
        if f not in ref_cache:
            ref_cache[f] = permute(load(pf))
        self_d = float(np.abs(ia - ref_cache[f]).mean())
        print('%8d %10d %10.3f %8.3f' % (f, best[0], best[1], self_d))


if __name__ == '__main__':
    main()
