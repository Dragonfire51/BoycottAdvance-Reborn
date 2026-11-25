#!/usr/bin/env python3
"""Run every automated check of the emulator core.

Three groups, all optional so the script is useful in any checkout:

  1. gba-tests verdicts - each ROM of jsmolka/gba-tests prints "All tests
     passed" (or "Failed test N") in a mode-4 BG2 text that is decoded from the
     VRAM dump.  Needs a checkout: --fetch clones it.
  2. Framebuffer goldens - SHA-256 of our own frames at fixed indices, so any
     change in behaviour is caught without a reference emulator.  The ROMs are
     not in the repo, so this group is skipped when they are missing.
  3. mGBA comparison - frame-by-frame pixel diff against mGBA (accuracy report,
     not pass/fail) when the mGBA harness and library are available.

Env:
  HH          headless harness          (default /workspace/work/harness/ourrec)
  BIOS        GBA BIOS image            (default <repo>/gba_bios.bin)
  SUITE_ROOT  gba-tests checkout        (default /workspace/work/gba-tests)
  ROMS        directory with game ROMs  (default /workspace/work/roms)
  GOLDEN      directory with *.sha256   (default <repo>/tools/golden)
  MGBA_HH     mGBA harness              (default /workspace/work/harness/mgbarec2)
  MGBA_LIB    mGBA shared library dir   (default /workspace/work/mgba/build2)

Usage:
  tools/run_gba_tests.py [--fetch] [--update] [--no-suite] [--no-golden]
                         [--mgba] [--frames N] [-v]
"""
import hashlib
import os
import shutil
import subprocess
import sys
import tempfile

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
HH = os.environ.get("HH", "/workspace/work/harness/ourrec")
BIOS = os.environ.get("BIOS", os.path.join(REPO, "gba_bios.bin"))
SUITE_ROOT = os.environ.get("SUITE_ROOT", "/workspace/work/gba-tests")
ROMS = os.environ.get("ROMS", "/workspace/work/roms")
GOLDEN = os.environ.get("GOLDEN", os.path.join(REPO, "tools", "golden"))
MGBA_HH = os.environ.get("MGBA_HH", "/workspace/work/harness/mgbarec2")
MGBA_LIB = os.environ.get("MGBA_LIB", "/workspace/work/mgba/build2")
SUITE_URL = "https://github.com/jsmolka/gba-tests"

# gba-tests ROMs that render a picture instead of a verdict.
SUITE_VISUAL = {"ppu/hello.gba", "ppu/shades.gba", "ppu/stripes.gba"}
# ROMs known to fail for a documented reason (kept visible, never silently green).
SUITE_KNOWN_FAIL = {
    "nes/nes.gba": "two-stage pipeline: needs an instruction prefetch queue",
}

# (rom file, frames, frame indices to hash) - frames chosen on a static screen.
GOLDEN_CASES = [
    ("Eragon (USA).gba", 600, [100, 300, 500, 550]),
    ("Digimon Racing (USA) (En,Fr,De,Es,It).gba", 2400, [200, 600, 1200, 2350]),
    ("0491 - Medabots AX - Rokusho Version (U)(Venom).gba", 900, [200, 500, 850]),
]
# Keys script used by the Digimon golden: it only reaches the race with input.
GOLDEN_KEYS = {
    "Digimon Racing (USA) (En,Fr,De,Es,It).gba":
        ",".join("A@%d:12" % f for f in range(2100, 2401, 100)),
}


def run(cmd, env=None, timeout=600, **kw):
    return subprocess.run(cmd, env=env, timeout=timeout, capture_output=True, **kw)


def need(path, what):
    if not os.path.exists(path):
        print("  skipped: %s not found (%s)" % (path, what))
        return False
    return True


def suite_verdicts(frames, verbose):
    """Run every gba-tests ROM and decode its verdict from VRAM."""
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    os.environ["SUITE_ROOT"] = SUITE_ROOT
    os.environ["HH"] = HH
    os.environ["BIOS"] = BIOS
    import gba_test_suite as suite          # glyph table + verdict decoder

    roms = sorted(
        os.path.join(d, f)
        for d, _, fs in os.walk(SUITE_ROOT)
        for f in fs if f.endswith(".gba")
    )
    if not roms:
        print("  skipped: no .gba in %s (use --fetch)" % SUITE_ROOT)
        return 0, 0, 0
    out = tempfile.mkdtemp(prefix="gbatests-")
    npass = nfail = nskip = 0
    for rom in roms:
        name = os.path.relpath(rom, SUITE_ROOT)
        pre = os.path.join(out, "cur")
        env = dict(os.environ, BIOS=BIOS)
        try:
            run([HH, rom, str(frames), "dump", pre], env=env, timeout=180)
        except subprocess.TimeoutExpired:
            print("  TIMEOUT  %s" % name)
            nfail += 1
            continue
        verdict, detail = suite.verdict(pre)
        if name in SUITE_VISUAL:
            nskip += 1
            if verbose:
                print("  VISUAL   %s  (renders a picture, no verdict)" % name)
            continue
        if verdict == "PASS":
            npass += 1
            if verbose:
                print("  PASS     %s" % name)
        elif name in SUITE_KNOWN_FAIL:
            nskip += 1
            print("  KNOWN    %s  [%s]" % (name, SUITE_KNOWN_FAIL[name]))
        else:
            nfail += 1
            print("  FAIL     %s  [%s]" % (name, detail or "?"))
    shutil.rmtree(out, ignore_errors=True)
    return npass, nfail, nskip


_CACHE = {}


def frame_pixels(rom, frames, keys):
    """Pixels of every dumped frame, keyed by frame index (cached per ROM)."""
    key = (rom, frames, keys)
    if key in _CACHE:
        return _CACHE[key]
    tmp = tempfile.mkdtemp(prefix="frames-")
    env = dict(os.environ, BIOS=BIOS, NOFASTBOOT="1", SEQDIR="%s:50" % tmp)
    if keys:
        env["KEYS"] = keys
    out = {}
    try:
        run([HH, rom, str(frames), "dump", os.path.join(tmp, "cur")],
            env=env, timeout=1800)
        for name in os.listdir(tmp):
            if name.endswith(".ppm"):
                data = open(os.path.join(tmp, name), "rb").read()
                out[int(name[6:11])] = data.split(b"255\n", 1)[1]
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    _CACHE[key] = out
    return out


def golden(update, verbose):
    """Compare our frames with the stored hashes (or refresh them)."""
    npass = nfail = 0
    for rom_name, frames, frames_used in GOLDEN_CASES:
        rom = os.path.join(ROMS, rom_name)
        if not os.path.exists(rom):
            print("  skipped: %s (no ROM)" % rom_name)
            continue
        pix = frame_pixels(rom, frames, GOLDEN_KEYS.get(rom_name))
        got = {n: hashlib.sha256(pix[n]).hexdigest() for n in frames_used if n in pix}
        path = os.path.join(GOLDEN, rom_name + ".sha256")
        if update:
            os.makedirs(GOLDEN, exist_ok=True)
            with open(path, "w") as fh:
                for n in frames_used:
                    fh.write("%d %s\n" % (n, got.get(n, "-")))
            print("  updated  %s" % os.path.basename(path))
            continue
        if not os.path.exists(path):
            print("  skipped: %s (no golden; run --update)" % os.path.basename(path))
            continue
        want = {}
        for line in open(path):
            n, h = line.split()
            if h != "-":
                want[int(n)] = h
        bad = [n for n in want if n in got and got[n] != want[n]]
        missing = [n for n in want if n not in got]
        if bad or missing:
            nfail += 1
            print("  FAIL     %s  [differ %s, missing %s]" % (rom_name, bad, missing))
        else:
            npass += 1
            if verbose:
                print("  PASS     %s" % rom_name)
    return npass, nfail


def mgba_report(verbose):
    """Pixel diff of our frames against mGBA at the golden indices."""
    if not (os.path.exists(MGBA_HH) and os.path.isdir(MGBA_LIB)):
        print("  skipped: mGBA harness/library not available")
        return
    for rom_name, frames, frames_used in GOLDEN_CASES:
        rom = os.path.join(ROMS, rom_name)
        if not os.path.exists(rom):
            continue
        ref = tempfile.mkdtemp(prefix="mgba-")
        try:
            env = dict(os.environ, LD_LIBRARY_PATH=MGBA_LIB,
                       SEQDIR="%s:50" % ref)
            if GOLDEN_KEYS.get(rom_name):
                env["KEYS"] = GOLDEN_KEYS[rom_name]
            run([MGBA_HH, rom, str(frames), ref, BIOS], env=env, timeout=1800)
            line = []
            for n in frames_used:
                a = os.path.join(ref, "frame_%05d.ppm" % n)
                if not os.path.exists(a):
                    continue
                A = open(a, "rb").read().split(b"255\n", 1)[1]
                B = frame_pixels(rom, frames, GOLDEN_KEYS.get(rom_name)).get(n)
                if B is None:
                    continue
                d = sum(1 for i in range(0, len(A), 3) if A[i:i + 3] != B[i:i + 3])
                line.append("f%d=%d" % (n, d))
            print("  %-20s %s" % (rom_name[:20], "  ".join(line) or "no frames"))
        finally:
            shutil.rmtree(ref, ignore_errors=True)


def main():
    args = sys.argv[1:]
    frames = 400
    if "--frames" in args:
        frames = int(args[args.index("--frames") + 1])
    verbose = "-v" in args

    if "--fetch" in args and not os.path.isdir(SUITE_ROOT):
        print("fetching %s -> %s" % (SUITE_URL, SUITE_ROOT))
        run(["git", "clone", "-q", "--depth", "1", SUITE_URL, SUITE_ROOT], timeout=300)

    if not need(HH, "build it: gcc -O2 -Isrc tools/headharness.c src/*.o -lm -o <HH>"):
        return 2

    ok = True
    if "--no-suite" not in args:
        print("gba-tests (%d frames/ROM)" % frames)
        p, f, k = suite_verdicts(frames, verbose)
        print("  -> %d passed, %d failed, %d known-fail\n" % (p, f, k))
        ok = ok and f == 0

    if "--no-golden" not in args:
        print("framebuffer goldens")
        p, f = golden("--update" in args, verbose)
        print("  -> %d matched, %d differ\n" % (p, f))
        ok = ok and f == 0

    if "--mgba" in args:
        print("mGBA pixel diff (report only)")
        mgba_report(verbose)
        print("")

    print("RESULT: %s" % ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
