#!/bin/sh
# android_jni_test.sh - verify the Android JNI layer on a desktop JVM.
#
# The Android app talks to the core through jni_bridge.c -> acore.c. This
# script compiles that exact bridge for Linux, loads it from a real JVM and
# checks that the frame it produces is bit-identical to the desktop build's
# output for the same ROM. That proves the port drives the same emulation,
# without needing a device or an emulator image.
#
# Usage: sh tools/android_jni_test.sh [rom] [bios]
# Needs a JDK (javac/java). The ROM/BIOS default to the files in the repo root.
set -e

ROOT=$(cd "$(dirname "$0")/.." && pwd)
ROM=${1:-"$ROOT/Pokemon Esmeralda.gba"}
BIOS=${2:-"$ROOT/gba_bios.bin"}
WORK=${TMPDIR:-/tmp}/bars_android_jni

JAVA_HOME=${JAVA_HOME:-$(dirname "$(dirname "$(command -v javac)")")}
JNI_INC="$JAVA_HOME/include"

if [ ! -f "$JNI_INC/jni.h" ]; then
    echo "javac/JDK not found (JAVA_HOME=$JAVA_HOME)"; exit 1
fi
if [ ! -f "$ROM" ]; then
    echo "ROM not found: $ROM"; exit 1
fi

rm -rf "$WORK"; mkdir -p "$WORK/src/com/boycottadvance/reborn"

echo "== compiling jni_bridge + core for Linux =="
gcc -std=c99 -O2 -shared -fPIC -Wall \
    -I"$JNI_INC" -I"$JNI_INC/linux" \
    -I"$ROOT/src" -I"$ROOT/android/app/src/main/cpp" \
    "$ROOT/android/app/src/main/cpp/jni_bridge.c" \
    "$ROOT/android/app/src/main/cpp/acore.c" \
    "$ROOT"/src/gba.c "$ROOT"/src/mem.c "$ROOT"/src/arm.c "$ROOT"/src/ppu.c \
    "$ROOT"/src/apu.c "$ROOT"/src/bios.c "$ROOT"/src/eeprom.c "$ROOT"/src/flash.c \
    -lm -o "$WORK/libboycottadvance.so"

# The Java side mirrors the app's NativeEmulator (same class, same natives).
# Keep the package so the JNI symbol names (Java_com_boycottadvance_reborn_*)
# match; the file must live under the matching directory.
cp "$ROOT/android/app/src/main/java/com/boycottadvance/reborn/NativeEmulator.java" \
   "$WORK/src/com/boycottadvance/reborn/NativeEmulator.java"

cat > "$WORK/src/JniTest.java" <<'EOF'
import com.boycottadvance.reborn.NativeEmulator;
public class JniTest {
    public static void main(String[] a) {
        NativeEmulator e = new NativeEmulator();
        if (!e.isValid()) { System.out.println("FAIL create"); System.exit(1); }
        System.out.println("create ok");
        if (!"-".equals(a[1])) System.out.println("loadBios=" + e.loadBios(a[1]));
        System.out.println("loadRom=" + e.loadRom(a[0]));
        System.out.println("romLoaded=" + e.romLoaded());
        System.out.println("romInfo=" + e.romInfo().replace("\n", " | "));
        e.setInt(NativeEmulator.SET_SCANLINE, 1);
        System.out.println("scanline get=" + e.getInt(NativeEmulator.SET_SCANLINE));
        e.setInt(NativeEmulator.SET_SCANLINE, 0);
        int[] frame = new int[240 * 160];
        short[] ab = new short[735 * 2];
        long pcm = 0;
        for (int f = 0; f < 120; f++) {
            e.setKeys(0); e.runFrame(frame);
            pcm += e.audioMix(ab, 735);
        }
        long h = 2166136261L;
        for (int v : frame) { h ^= (v & 0xFFFFFFFFL); h = (h * 16777619L) & 0xFFFFFFFFL; }
        System.out.printf("frame120 argb crc=%08x px00=%08x%n", h, frame[0]);
        System.out.println("pcm frames=" + pcm);
        System.out.println("saveState=" + e.saveState(0));
        System.out.println("loadState=" + e.loadState(0));
        e.saveBattery();
        e.close();
        System.out.println("DONE");
    }
}
EOF

echo "== running the JNI test on the JVM =="
"$JAVA_HOME/bin/javac" -d "$WORK/classes" \
    "$WORK/src/com/boycottadvance/reborn/NativeEmulator.java" "$WORK/src/JniTest.java"
"$JAVA_HOME/bin/java" -Djava.library.path="$WORK" -cp "$WORK/classes" \
    JniTest "$ROM" "$BIOS" | tee "$WORK/jni.out"

JNI_CRC=$(sed -n 's/.*argb crc=\([0-9a-f]*\).*/\1/p' "$WORK/jni.out")

echo "== building the same frame with the desktop core (acore, no JVM) =="
gcc -std=c99 -O2 -Wall \
    -I"$ROOT/src" -I"$ROOT/android/app/src/main/cpp" \
    "$ROOT/tools/acore_test.c" "$ROOT/android/app/src/main/cpp/acore.c" \
    "$ROOT"/src/gba.c "$ROOT"/src/mem.c "$ROOT"/src/arm.c "$ROOT"/src/ppu.c \
    "$ROOT"/src/apu.c "$ROOT"/src/bios.c "$ROOT"/src/eeprom.c "$ROOT"/src/flash.c \
    -lm -o "$WORK/acore_test"
REF_CRC=$("$WORK/acore_test" "$ROM" "$BIOS" "$WORK" 120 120 0 \
          | sed -n 's/.*argb crc=\([0-9a-f]*\).*/\1/p')

echo
echo "JNI   frame120 crc = $JNI_CRC"
echo "acore frame120 crc = $REF_CRC"
if [ "$JNI_CRC" = "$REF_CRC" ] && [ -n "$JNI_CRC" ]; then
    echo "PASS: the JNI layer drives the identical emulation (bit-identical frame)"
else
    echo "FAIL: frame mismatch"; exit 1
fi

