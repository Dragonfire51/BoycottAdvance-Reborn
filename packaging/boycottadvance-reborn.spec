Name:           boycottadvance-reborn
Version:        @VERSION@
Release:        1
Summary:        Clean-room Game Boy Advance emulator
License:        Cardware
URL:            https://github.com/Dragonfire51/BoycottAdvance-Reborn
BuildArch:      x86_64

%description
A modern reconstruction of the BoycottAdvance 0.2.8 Game Boy Advance
emulator, with a complete ARM7TDMI core and BIOS SWI emulation.

%install
mkdir -p %{buildroot}/usr/lib/boycottadvance-reborn
mkdir -p %{buildroot}/usr/bin
mkdir -p %{buildroot}/usr/share/applications
mkdir -p %{buildroot}/usr/share/icons/hicolor/32x32/apps
mkdir -p %{buildroot}/usr/share/doc/boycottadvance-reborn
install -m755 %{_sourcedir}/boycottadvance-reborn \
    %{buildroot}/usr/lib/boycottadvance-reborn/boycottadvance-reborn
install -m644 %{_sourcedir}/gba_bios.bin \
    %{buildroot}/usr/lib/boycottadvance-reborn/gba_bios.bin
install -m644 %{_sourcedir}/README.md \
    %{buildroot}/usr/share/doc/boycottadvance-reborn/README.md
install -m644 %{_sourcedir}/boycottadvance-reborn.desktop \
    %{buildroot}/usr/share/applications/boycottadvance-reborn.desktop
install -m644 %{_sourcedir}/icon.png \
    %{buildroot}/usr/share/icons/hicolor/32x32/apps/boycottadvance-reborn.png
ln -sf /usr/lib/boycottadvance-reborn/boycottadvance-reborn \
    %{buildroot}/usr/bin/boycottadvance-reborn

%files
/usr/lib/boycottadvance-reborn/boycottadvance-reborn
/usr/lib/boycottadvance-reborn/gba_bios.bin
/usr/bin/boycottadvance-reborn
/usr/share/applications/boycottadvance-reborn.desktop
/usr/share/icons/hicolor/32x32/apps/boycottadvance-reborn.png
/usr/share/doc/boycottadvance-reborn/README.md
