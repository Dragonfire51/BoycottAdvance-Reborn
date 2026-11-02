#!/bin/sh
# android_lifecycle_test.sh - reproduce/verify the Android session lifecycle
# race at the JNI level, on a desktop JVM, under AddressSanitizer.
#
# On Android the frame loop runs on its own thread while the UI thread can
# close() the session (leaving the Activity) or load another ROM. If those
# native calls are not serialised, close() frees the core while the frame
# thread is still inside runFrame() -> heap-use-after-free, which is exactly
# the "crashes when I open the emulator again" report.
#
# This drives the *real* NativeEmulator class with that concurrency, so it
# fails (ASan) before the fix and passes after it. No device needed.
#
# Usage: sh tools/android_lifecycle_test.sh [rom]
set -e

ROOT=$(cd "$(dirname "$0")/.." && pwd)
ROM=${1:-"$ROOT/Pokemon Esmeralda.gba"}
WORK=${TMPDIR:-/tmp}/bars_android_life

JAVA_HOME=${JAVA_HOME:-$(dirname "$(dirname "$(command -v javac)")")}
JNI_INC="$JAVA_HOME/include"
if [ ! -f "$JNI_INC/jni.h" ]; then echo "JDK not found"; exit 1; fi
if [ ! -f "$ROM" ]; then echo "ROM not found: $ROM"; exit 1; fi

rm -rf "$WORK"; mkdir -p "$WORK/src/com/boycottadvance/reborn" "$WORK/classes"

echo "== compiling the JNI bridge + core with AddressSanitizer =="
gcc -std=c99 -g -O1 -fsanitize=address -fno-omit-frame-pointer -shared -fPIC \
    -I"$JNI_INC" -I"$JNI_INC/linux" \
    -I"$ROOT/src" -I"$ROOT/android/app/src/main/cpp" \
    "$ROOT/android/app/src/main/cpp/jni_bridge.c" \
    "$ROOT/android/app/src/main/cpp/acore.c" \
    "$ROOT"/src/gba.c "$ROOT"/src/mem.c "$ROOT"/src/arm.c "$ROOT"/src/ppu.c \
    "$ROOT"/src/apu.c "$ROOT"/src/bios.c "$ROOT"/src/eeprom.c "$ROOT"/src/flash.c \
    "$ROOT"/src/rtc.c \
    -lm -o "$WORK/libboycottadvance.so"

cp "$ROOT/android/app/src/main/java/com/boycottadvance/reborn/NativeEmulator.java" \
   "$WORK/src/com/boycottadvance/reborn/NativeEmulator.java"

cat > "$WORK/src/LifeTest.java" <<'EOF'
import com.boycottadvance.reborn.NativeEmulator;

/** Mimics the Android lifecycle: a frame thread running while the UI closes
 *  the session and opens a new one, plus reloading a ROM mid-run. */
public class LifeTest {
    public static void main(String[] a) throws Exception {
        final String rom = a[0];
        for (int round = 0; round < 5; round++) {
            final NativeEmulator e = new NativeEmulator();
            if (!e.loadRom(rom)) { System.out.println("loadRom failed"); System.exit(1); }

            final int[] buf = new int[240 * 160];
            final boolean[] run = { true };
            Thread t = new Thread(() -> {
                while (run[0]) { e.runFrame(buf); }
            });
            t.start();
            Thread.sleep(30);          /* let the frame thread get inside runFrame */

            e.close();                 /* frees the session, thread may be in native */
            run[0] = false;
            t.join(500);

            NativeEmulator e2 = new NativeEmulator();   /* reopen */
            if (!e2.loadRom(rom)) { System.out.println("reload failed"); System.exit(1); }
            int[] b2 = new int[240 * 160];
            for (int i = 0; i < 10; i++) e2.runFrame(b2);
            e2.close();
            System.out.println("round " + round + " ok");
        }

        /* Second race: reload the ROM while the frame thread keeps running
         * (the UI "load another ROM" path). The frame thread paces itself
         * (a few ms per frame, like the audio clock in the app) so the test
         * exercises the race without starving the reload on the monitor. */
        final NativeEmulator e = new NativeEmulator();
        if (!e.loadRom(rom)) { System.out.println("loadRom failed"); System.exit(1); }
        final int[] buf = new int[240 * 160];
        final boolean[] run = { true };
        Thread t = new Thread(() -> {
            while (run[0]) {
                e.runFrame(buf);
                try { Thread.sleep(4); } catch (InterruptedException ex) { }
            }
        });
        t.start();
        for (int i = 0; i < 6; i++) {
            Thread.sleep(15);
            e.loadRom(rom);            /* concurrent with runFrame */
        }
        run[0] = false;
        t.join(500);
        e.close();
        System.out.println("reload-race ok");
        System.out.println("LIFECYCLE_OK");
    }
}
EOF

echo "== running the lifecycle test under ASan =="
ASAN=$(gcc -print-file-name=libasan.so)
"$JAVA_HOME/bin/javac" -d "$WORK/classes" \
    "$WORK/src/com/boycottadvance/reborn/NativeEmulator.java" "$WORK/src/LifeTest.java"

set +e
LD_PRELOAD="$ASAN" ASAN_OPTIONS=detect_leaks=0:abort_on_error=0 \
    "$JAVA_HOME/bin/java" -Djava.library.path="$WORK" -cp "$WORK/classes" \
    LifeTest "$ROM" 2>&1 | tee "$WORK/life.out"
RC=$?
set -e

if grep -q "AddressSanitizer" "$WORK/life.out"; then
    echo "FAIL: AddressSanitizer caught a memory error in the session lifecycle"
    exit 1
fi
if grep -q "LIFECYCLE_OK" "$WORK/life.out" && [ "$RC" = 0 ]; then
    echo "PASS: create/run/close/reopen is memory-safe (no ASan report)"
else
    echo "FAIL: lifecycle test did not complete (rc=$RC)"
    exit 1
fi
