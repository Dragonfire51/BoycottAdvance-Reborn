#!/bin/sh
# Build .deb and .rpm packages for the Linux build.
#
# Layout (both formats share it):
#   /usr/lib/boycottadvance-reborn/boycottadvance-reborn   the real ELF binary
#   /usr/lib/boycottadvance-reborn/gba_bios.bin            BIOS dump, next to it
#   /usr/bin/boycottadvance-reborn                         symlink to the above
#   /usr/share/applications/boycottadvance-reborn.desktop
#   /usr/share/icons/hicolor/32x32/apps/boycottadvance-reborn.png
#   /usr/share/doc/boycottadvance-reborn/README.md
#
# The binary looks for gba_bios.bin next to itself, and /proc/self/exe resolves
# the /usr/bin symlink back to /usr/lib, so the BIOS is found with no code
# change. Run from the repository root after `make`.
set -eu

VERSION=$(sed -n 's/.*#define GBA_VERSION  *"v\([0-9.]*\)".*/\1/p' src/gba.h)
[ -n "$VERSION" ] || { echo "cannot read GBA_VERSION from src/gba.h" >&2; exit 1; }

BIN=boycottadvance-reborn
[ -x "$BIN" ] || { echo "build first: make" >&2; exit 1; }

ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
OUT="$ROOT/dist"
mkdir -p "$OUT"

# --- .deb -------------------------------------------------------------------
DEBROOT=$(mktemp -d)
install -Dm755 "$BIN" "$DEBROOT/usr/lib/boycottadvance-reborn/$BIN"
install -Dm644 gba_bios.bin "$DEBROOT/usr/lib/boycottadvance-reborn/gba_bios.bin"
install -Dm644 README.md "$DEBROOT/usr/share/doc/boycottadvance-reborn/README.md"
install -Dm644 packaging/boycottadvance-reborn.desktop \
    "$DEBROOT/usr/share/applications/boycottadvance-reborn.desktop"
install -Dm644 packaging/icon.png \
    "$DEBROOT/usr/share/icons/hicolor/32x32/apps/boycottadvance-reborn.png"
mkdir -p "$DEBROOT/usr/bin"
ln -sf "/usr/lib/boycottadvance-reborn/$BIN" "$DEBROOT/usr/bin/$BIN"
SIZE=$(du -sk "$DEBROOT/usr" | cut -f1)
mkdir -p "$DEBROOT/DEBIAN"
cat > "$DEBROOT/DEBIAN/control" <<EOF
Package: boycottadvance-reborn
Version: $VERSION
Section: games
Priority: optional
Architecture: amd64
Depends: libc6, libsdl2-2.0-0
Maintainer: BoycottAdvance Reborn <noreply@example.com>
Installed-Size: $SIZE
Homepage: https://github.com/Dragonfire51/BoycottAdvance-Reborn
Description: Clean-room Game Boy Advance emulator
 A modern reconstruction of the BoycottAdvance 0.2.8 Game Boy Advance
 emulator, with a complete ARM7TDMI core and BIOS SWI emulation.
EOF
dpkg-deb --root-owner-group --build "$DEBROOT" \
    "$OUT/boycottadvance-reborn_${VERSION}_amd64.deb" >/dev/null
rm -rf "$DEBROOT"
echo "built $OUT/boycottadvance-reborn_${VERSION}_amd64.deb"

# --- .rpm -------------------------------------------------------------------
if command -v rpmbuild >/dev/null 2>&1; then
    TOP=$(mktemp -d)
    mkdir -p "$TOP"/BUILD "$TOP"/RPMS "$TOP"/SOURCES "$TOP"/SPECS "$TOP"/SRPMS
    sed "s/@VERSION@/$VERSION/" packaging/boycottadvance-reborn.spec > "$TOP/SPECS/boycottadvance-reborn.spec"
    cp "$BIN" gba_bios.bin README.md "$TOP/SOURCES/"
    cp packaging/boycottadvance-reborn.desktop packaging/icon.png "$TOP/SOURCES/"
    rpmbuild --define "_topdir $TOP" -bb "$TOP/SPECS/boycottadvance-reborn.spec" \
        >/dev/null 2>&1
    find "$TOP/RPMS" -name '*.rpm' -exec cp {} "$OUT/" \;
    rm -rf "$TOP"
    echo "built $OUT/boycottadvance-reborn-${VERSION}-1.x86_64.rpm"
else
    echo "rpmbuild not installed: skipped .rpm" >&2
fi
