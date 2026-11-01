package com.boycottadvance.reborn;

/**
 * JNI surface for the native emulator core (libboycottadvance.so).
 *
 * Each instance wraps one emulator session. The native side owns the core
 * and the settings; this class only forwards calls and provides the button
 * and setting identifiers.
 */
public final class NativeEmulator {

    static {
        System.loadLibrary("boycottadvance");
    }

    /* GBA keypad bits (see ACORE_KEY_* in acore.h). */
    public static final int KEY_A      = 1 << 0;
    public static final int KEY_B      = 1 << 1;
    public static final int KEY_SELECT = 1 << 2;
    public static final int KEY_START  = 1 << 3;
    public static final int KEY_RIGHT  = 1 << 4;
    public static final int KEY_LEFT   = 1 << 5;
    public static final int KEY_UP     = 1 << 6;
    public static final int KEY_DOWN   = 1 << 7;
    public static final int KEY_R      = 1 << 8;
    public static final int KEY_L      = 1 << 9;

    /* Setting ids (must match the SET_* enum in jni_bridge.c). */
    public static final int SET_SOUND          = 0;
    public static final int SET_DSOUND_A       = 1;
    public static final int SET_DSOUND_B       = 2;
    public static final int SET_GBC_SOUND      = 3;
    public static final int SET_REVERSE_STEREO = 4;
    public static final int SET_BIOS_ENABLE    = 5;
    public static final int SET_BIOS_FASTBOOT  = 6;
    public static final int SET_SKIP_INTROS    = 7;
    public static final int SET_FRAMESKIP      = 8;
    public static final int SET_AUTOFRAMESKIP  = 9;
    public static final int SET_SCANLINE       = 10;
    public static final int SET_SMOOTH         = 11;
    public static final int SET_STATE_SLOT     = 12;
    public static final int SET_SHOWFPS        = 13;

    private long handle;

    public NativeEmulator() {
        handle = create();
    }

    public boolean isValid() { return handle != 0; }

    public void close() {
        if (handle != 0) { destroy(handle); handle = 0; }
    }

    /* ---- native methods ---- */
    private static native long    create();
    private static native void    destroy(long h);
    public  static native boolean loadBios(long h, String path);
    public  static native boolean loadRom(long h, String path);
    public  static native boolean romLoaded(long h);
    public  static native String  romInfo(long h);
    public  static native void    reset(long h);
    public  static native void    saveBattery(long h);
    public  static native void    setKeys(long h, int mask);
    public  static native void    runFrame(long h, int[] out);
    public  static native int     audioMix(long h, short[] out, int nframes);
    public  static native int     audioAvailable(long h);
    public  static native int     saveState(long h, int slot);
    public  static native int     loadState(long h, int slot);
    public  static native void    setInt(long h, int key, int value);
    public  static native int     getInt(long h, int key);

    /* Convenience wrappers bound to this instance. */
    public boolean loadBios(String path)   { return handle != 0 && loadBios(handle, path); }
    public boolean loadRom(String path)    { return handle != 0 && loadRom(handle, path); }
    public boolean romLoaded()             { return handle != 0 && romLoaded(handle); }
    public String  romInfo()               { return handle != 0 ? romInfo(handle) : ""; }
    public void    reset()                 { if (handle != 0) reset(handle); }
    public void    saveBattery()           { if (handle != 0) saveBattery(handle); }
    public void    setKeys(int mask)       { if (handle != 0) setKeys(handle, mask); }
    public void    runFrame(int[] out)     { if (handle != 0) runFrame(handle, out); }
    public int     audioMix(short[] out, int n) { return handle != 0 ? audioMix(handle, out, n) : 0; }
    public int     audioAvailable()        { return handle != 0 ? audioAvailable(handle) : 0; }
    public int     saveState(int slot)     { return handle != 0 ? saveState(handle, slot) : -1; }
    public int     loadState(int slot)     { return handle != 0 ? loadState(handle, slot) : -1; }
    public void    setInt(int key, int v)  { if (handle != 0) setInt(handle, key, v); }
    public int     getInt(int key)         { return handle != 0 ? getInt(handle, key) : 0; }
}
