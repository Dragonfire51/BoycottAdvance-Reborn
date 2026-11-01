#!/bin/sh
# android_zip_test.sh - verify .zip ROM loading end to end on a desktop JVM.
#
# Android ROMs are usually distributed as .zip, and the app unzips them with
# ZipRom before handing a real file to the core. This script proves that
# path: it zips a ROM, extracts it with the very same ZipRom.java the app
# uses, and checks that the extracted file loads and runs in the native core
# exactly like the original (same frame CRC).
#
# Usage: sh tools/android_zip_test.sh [rom]
set -e

ROOT=$(cd "$(dirname "$0")/.." && pwd)
ROM=${1:-"$ROOT/Pokemon Esmeralda.gba"}
WORK=${TMPDIR:-/tmp}/bars_android_zip

JAVA_HOME=${JAVA_HOME:-$(dirname "$(dirname "$(command -v javac)")")}
if [ ! -f "$JAVA_HOME/include/jni.h" ]; then
    echo "javac/JDK not found (JAVA_HOME=$JAVA_HOME)"; exit 1
fi
if [ ! -f "$ROM" ]; then echo "ROM not found: $ROM"; exit 1; fi

rm -rf "$WORK"; mkdir -p "$WORK/src/com/boycottadvance/reborn" "$WORK/out"
cp "$ROM" "$WORK/out/$(basename "$ROM")"

echo "== zipping the ROM =="
( cd "$WORK/out" && zip -q "$WORK/rom.zip" "$(basename "$ROM")" )
ls -l "$WORK/rom.zip"

cp "$ROOT/android/app/src/main/java/com/boycottadvance/reborn/ZipRom.java" \
   "$WORK/src/com/boycottadvance/reborn/ZipRom.java"

cat > "$WORK/src/ZipTest.java" <<'EOF'
import com.boycottadvance.reborn.ZipRom;
import java.io.File;

public class ZipTest {
    public static void main(String[] a) throws Exception {
        File zip = new File(a[0]);
        File out = new File(a[1]);
        String entry = ZipRom.findRomEntry(zip);
        System.out.println("entry=" + entry);
        File rom = ZipRom.extract(zip, out);
        if (rom == null) { System.out.println("FAIL: no ROM in zip"); System.exit(1); }
        System.out.println("extracted=" + rom.getAbsolutePath()
                + " size=" + rom.length());
        if (entry == null || rom.length() == 0) { System.out.println("FAIL"); System.exit(1); }
        System.out.println("ZIP_OK");
    }
}
EOF

echo "== extracting with the app's ZipRom =="
"$JAVA_HOME/bin/javac" -d "$WORK/classes" \
    "$WORK/src/com/boycottadvance/reborn/ZipRom.java" "$WORK/src/ZipTest.java"
"$JAVA_HOME/bin/java" -cp "$WORK/classes" ZipTest "$WORK/rom.zip" "$WORK/out" \
    | tee "$WORK/zip.out"

EXTRACTED="$WORK/out/$(basename "$ROM")"

echo "== running the extracted ROM in the native core =="
gcc -std=c99 -O2 -Wall \
    -I"$ROOT/src" -I"$ROOT/android/app/src/main/cpp" \
    "$ROOT/tools/acore_test.c" "$ROOT/android/app/src/main/cpp/acore.c" \
    "$ROOT"/src/gba.c "$ROOT"/src/mem.c "$ROOT"/src/arm.c "$ROOT"/src/ppu.c \
    "$ROOT"/src/apu.c "$ROOT"/src/bios.c "$ROOT"/src/eeprom.c "$ROOT"/src/flash.c \
    -lm -o "$WORK/acore_test"

Z_CRC=$("$WORK/acore_test" "$EXTRACTED" - "$WORK/out" 120 120 0 \
        | sed -n 's/.*argb crc=\([0-9a-f]*\).*/\1/p')
P_CRC=$("$WORK/acore_test" "$ROM" - "$WORK/out" 120 120 0 \
        | sed -n 's/.*argb crc=\([0-9a-f]*\).*/\1/p')

echo
echo "zip-extracted frame120 crc = $Z_CRC"
echo "original      frame120 crc = $P_CRC"
if [ "$Z_CRC" = "$P_CRC" ] && [ -n "$Z_CRC" ]; then
    echo "PASS: a .zip ROM loads and runs identically to the raw .gba"
else
    echo "FAIL: frame mismatch"; exit 1
fi
