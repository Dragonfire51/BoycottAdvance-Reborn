package com.boycottadvance.reborn;

import android.media.AudioFormat;
import android.media.AudioManager;
import android.media.AudioTrack;
import android.os.Process;

/**
 * Runs the emulation loop on a dedicated thread and plays the audio the core
 * produces. The audio device is the clock: each frame writes the samples it
 * generated, and AudioTrack.write() blocks until there is room, which paces
 * the emulation to real time (the same idea as the desktop frontends, which
 * pace by the audio queue). In fast-forward the audio is dropped and the
 * loop runs as fast as the CPU allows.
 */
public final class EmulatorThread extends Thread {

    public static final int W = 240;
    public static final int H = 160;
    private static final int SAMPLE_RATE = 44100;
    /* The core produces 44100*280896/16777216 = 738.35 samples per frame, not
       735 (44100/60). Draining a fixed count per frame leaves the surplus in
       the ring, which fills it and then drops the overflow on every frame
       (audible crackle), so each frame drains whatever the ring actually
       holds. This is the buffer capacity in samples, not a per-frame amount. */
    private static final int AUDIO_BUF_FRAMES = 1024;

    private final NativeEmulator emu;
    private final Object frameLock = new Object();
    private final int[] frame = new int[W * H];
    private final int[] renderBuf = new int[W * H];

    private volatile boolean running = true;
    private volatile boolean paused = false;
    private volatile boolean fastForward = false;
    private volatile int keys = 0;

    private AudioTrack audio;
    private short[] audioBuf = new short[AUDIO_BUF_FRAMES * 2];
    private final Object audioLock = new Object();

    /* FPS reporting for the on-screen counter. */
    private volatile float fps = 0f;
    private volatile int framesRun = 0;

    public EmulatorThread(NativeEmulator emu) {
        super("GbaEmu");
        this.emu = emu;
    }

    public void setKeys(int mask) { keys = mask; }
    public void setPaused(boolean p) {
        paused = p;
        synchronized (audioLock) {
            if (audio != null) {
                if (p) { audio.pause(); audio.flush(); }
                else audio.play();
            }
        }
    }
    public boolean isPaused() { return paused; }
    public void setFastForward(boolean ff) { fastForward = ff; }
    public boolean isFastForward() { return fastForward; }
    public float fps() { return fps; }

    /**
     * Stops the loop and waits for the thread to leave runFrame() before
     * returning. The caller (onDestroy) frees the native session right after,
     * so joining here is what prevents freeing the core while this thread is
     * still inside a native call.
     */
    public void shutdown() {
        running = false;
        setPaused(true);
        interrupt();
        try { join(2000); } catch (InterruptedException e) { Thread.currentThread().interrupt(); }
    }

    /** Copy the latest rendered frame into out (must be W*H ints). */
    public void copyFrame(int[] out) {
        synchronized (frameLock) {
            System.arraycopy(frame, 0, out, 0, frame.length);
        }
    }

    private void openAudio() {
        int minBuf = AudioTrack.getMinBufferSize(SAMPLE_RATE,
                AudioFormat.CHANNEL_OUT_STEREO, AudioFormat.ENCODING_PCM_16BIT);
        if (minBuf <= 0) minBuf = AUDIO_BUF_FRAMES * 4;
        int bufBytes = Math.max(minBuf * 2, AUDIO_BUF_FRAMES * 4 * 2);
        try {
            audio = new AudioTrack(AudioManager.STREAM_MUSIC, SAMPLE_RATE,
                    AudioFormat.CHANNEL_OUT_STEREO, AudioFormat.ENCODING_PCM_16BIT,
                    bufBytes, AudioTrack.MODE_STREAM);
            if (audio.getState() == AudioTrack.STATE_INITIALIZED) {
                audio.play();
            } else {
                audio.release();
                audio = null;
            }
        } catch (Throwable t) {
            audio = null;
        }
    }

    @Override
    public void run() {
        Process.setThreadPriority(Process.THREAD_PRIORITY_URGENT_AUDIO);
        openAudio();

        long lastFpsT = System.nanoTime();
        while (running) {
            if (paused || !emu.romLoaded()) {
                try { Thread.sleep(20); } catch (InterruptedException e) { }
                lastFpsT = System.nanoTime();
                framesRun = 0;
                continue;
            }

            emu.setKeys(keys);
            emu.runFrame(renderBuf);
            synchronized (frameLock) {
                System.arraycopy(renderBuf, 0, frame, 0, frame.length);
            }

            boolean soundOn = emu.getInt(NativeEmulator.SET_SOUND) != 0;
            if (soundOn && audio != null && !fastForward) {
                /* Drain everything the ring holds (chunked to audioBuf), not a
                   fixed 735: the core yields ~738.35 samples/frame, so a fixed
                   count overflows the ring and drops samples every frame. */
                int avail = emu.audioAvailable();
                while (avail > 0) {
                    int want = Math.min(avail, AUDIO_BUF_FRAMES);
                    int got = emu.audioMix(audioBuf, want);
                    if (got <= 0) break;
                    try {
                        audio.write(audioBuf, 0, got * 2);
                    } catch (Throwable t) {
                        // Device gone; keep emulating silently.
                        break;
                    }
                    avail -= got;
                }
            } else {
                // Drain so the ring does not overflow while muted/fast-forward.
                int avail = emu.audioAvailable();
                while (avail > 0) {
                    int want = Math.min(avail, AUDIO_BUF_FRAMES);
                    int got = emu.audioMix(audioBuf, want);
                    if (got <= 0) break;
                    avail -= got;
                }
            }

            if (fastForward) {
                try { Thread.sleep(1); } catch (InterruptedException e) { }
            }

            framesRun++;
            long now = System.nanoTime();
            long dt = now - lastFpsT;
            if (dt >= 1_000_000_000L) {
                fps = framesRun * 1e9f / dt;
                framesRun = 0;
                lastFpsT = now;
            }
        }

        synchronized (audioLock) {
            if (audio != null) { audio.stop(); audio.release(); audio = null; }
        }
    }
}
