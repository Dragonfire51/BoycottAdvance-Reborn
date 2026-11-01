package com.boycottadvance.reborn;

import android.content.Context;
import android.net.Uri;

import java.io.BufferedInputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;

/**
 * Turns whatever the file browser returned (a SAF content:// document or a
 * plain path) into a real file the native core can fopen.
 *
 * The core opens the ROM by path and writes the .sav and the savestates next
 * to it, so files coming from a content:// URI are copied into the app's
 * private directory (no storage permission needed there). Files that are
 * already real paths are used as-is.
 */
public final class RomStore {

    private RomStore() { }

    /** Directory the app owns: readable and writable without permissions. */
    public static File romDir(Context ctx) {
        File d = new File(ctx.getFilesDir(), "roms");
        if (!d.exists()) d.mkdirs();
        return d;
    }

    /** True for a path we can hand to fopen unchanged. */
    public static boolean isPlainPath(String ref) {
        return ref != null && !ref.startsWith("content:");
    }

    /**
     * Materialise ref into a loadable file. A plain path is returned as-is; a
     * content:// URI is copied into the app folder (or, for a .zip, its inner
     * ROM is extracted). Returns null when the reference cannot be resolved.
     */
    public static File resolve(Context ctx, String ref, String displayName)
            throws IOException {
        if (isPlainPath(ref)) {
            File f = new File(ref);
            if (!f.exists()) return null;
            if (ZipRom.isZipName(f.getName())) {
                File rom = ZipRom.extract(f, f.getParentFile());
                if (rom != null) f.delete();
                return rom;
            }
            return f;
        }
        Uri uri = Uri.parse(ref);
        File dir = romDir(ctx);
        String name = safeName(displayName);
        if (name == null) name = "rom";

        /* Copy the stream once, then decide by content: some providers hand
         * out a name without the real extension, so the archive magic is the
         * reliable test. */
        File staged = new File(dir, name.endsWith(".zip") ? name : name + ".tmp");
        try (InputStream in = ctx.getContentResolver().openInputStream(uri)) {
            if (in == null) return null;
            try (OutputStream os = new FileOutputStream(staged)) {
                ZipRom.copy(new BufferedInputStream(in), os);
            }
        }
        if (ZipRom.isZipName(name) || ZipRom.isZipFile(staged)) {
            File rom = ZipRom.extract(staged, dir);
            staged.delete();
            return rom;
        }
        File out = new File(dir, name);
        if (!staged.renameTo(out)) {
            /* rename can fail across mounts; fall back to a copy. */
            try (InputStream in = new FileInputStream(staged);
                 OutputStream os = new FileOutputStream(out)) {
                ZipRom.copy(in, os);
            }
            staged.delete();
        }
        return out;
    }

    /** Strip any directory part and characters the filesystem dislikes. */
    private static String safeName(String name) {
        if (name == null || name.isEmpty()) return null;
        String base = new File(name).getName();
        StringBuilder sb = new StringBuilder(base.length());
        for (int i = 0; i < base.length(); i++) {
            char c = base.charAt(i);
            sb.append(c == '/' || c == '\\' || c == ':' || c == '\0' ? '_' : c);
        }
        return sb.length() == 0 ? null : sb.toString();
    }
}
