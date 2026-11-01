package com.boycottadvance.reborn;

import java.io.BufferedInputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/**
 * Pulls a GBA image out of a .zip archive.
 *
 * ROMs are usually distributed zipped, but the emulator core opens a plain
 * file by path with fopen (it also writes the .sav and the savestates next to
 * the ROM), so the first .gba/.agb/.bin entry is extracted to a real file.
 *
 * Only java.io and java.util.zip, no Android types, so it is unit-tested on
 * the desktop JVM by tools/android_zip_test.sh.
 */
public final class ZipRom {

    private ZipRom() { }

    /** True if the name looks like an image the core can load. */
    public static boolean isRomName(String name) {
        if (name == null) return false;
        String n = name.toLowerCase();
        return n.endsWith(".gba") || n.endsWith(".agb") || n.endsWith(".bin");
    }

    public static boolean isZipName(String name) {
        return name != null && name.toLowerCase().endsWith(".zip");
    }

    /** True if the file starts with a zip signature (PK\003\004 / PK\005\006). */
    public static boolean isZipFile(File f) {
        try (InputStream in = new FileInputStream(f)) {
            byte[] sig = new byte[4];
            int n = in.read(sig);
            if (n < 4) return false;
            return sig[0] == 'P' && sig[1] == 'K'
                && ((sig[2] == 3 && sig[3] == 4) || (sig[2] == 5 && sig[3] == 6)
                 || (sig[2] == 7 && sig[3] == 8));
        } catch (IOException e) {
            return false;
        }
    }

    /** The first ROM-looking entry of the archive, or null. */
    public static String findRomEntry(File zip) throws IOException {
        try (ZipInputStream in = new ZipInputStream(
                new BufferedInputStream(new FileInputStream(zip)))) {
            ZipEntry e;
            while ((e = in.getNextEntry()) != null) {
                if (!e.isDirectory() && isRomName(e.getName())) return e.getName();
            }
        }
        return null;
    }

    /**
     * Extract the first ROM entry of zip into outDir and return the written
     * file, or null if the archive holds no ROM. The file keeps the entry's
     * base name so the .sav stays associated with it.
     */
    public static File extract(File zip, File outDir) throws IOException {
        try (ZipInputStream in = new ZipInputStream(
                new BufferedInputStream(new FileInputStream(zip)))) {
            ZipEntry e;
            while ((e = in.getNextEntry()) != null) {
                if (e.isDirectory() || !isRomName(e.getName())) continue;
                String base = new File(e.getName()).getName();
                if (!outDir.exists() && !outDir.mkdirs()) {
                    throw new IOException("cannot create " + outDir);
                }
                File out = new File(outDir, base);
                try (OutputStream os = new FileOutputStream(out)) {
                    copy(in, os);
                }
                return out;
            }
        }
        return null;
    }

    /** Copy until EOF; returns the number of bytes written. */
    public static long copy(InputStream in, OutputStream out) throws IOException {
        byte[] buf = new byte[64 * 1024];
        long total = 0;
        int n;
        while ((n = in.read(buf)) > 0) {
            out.write(buf, 0, n);
            total += n;
        }
        return total;
    }
}
