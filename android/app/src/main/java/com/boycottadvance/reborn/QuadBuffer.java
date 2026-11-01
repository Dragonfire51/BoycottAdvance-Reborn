package com.boycottadvance.reborn;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.FloatBuffer;

/**
 * The 4-vertex textured quad the GL renderer draws, as interleaved
 * (x, y, u, v) floats: 4 vertices * 4 components = 16 floats.
 *
 * It has no Android dependency, so the desktop JVM can exercise exactly the
 * code the APK runs (see tools/android_renderer_test.sh). The buffer is sized
 * for all 16 floats up front; sizing it for a single vertex made the per-frame
 * update overflow with BufferOverflowException and killed the GL thread.
 */
public final class QuadBuffer {

    public static final int FLOATS = 16;   /* 4 vertices * (x, y, u, v) */

    private final FloatBuffer buf = ByteBuffer.allocateDirect(FLOATS * 4)
            .order(ByteOrder.nativeOrder()).asFloatBuffer();

    /** The interleaved vertex buffer, ready to bind. */
    public FloatBuffer buffer() { return buf; }

    /**
     * Rewrite the quad for the given half-extents and return the buffer with
     * its position at 0, ready for glVertexAttribPointer.
     */
    public FloatBuffer update(float sx, float sy) {
        float[] v = {
            -sx, -sy,  0f, 1f,   /* bottom-left  */
             sx, -sy,  1f, 1f,   /* bottom-right */
            -sx,  sy,  0f, 0f,   /* top-left     */
             sx,  sy,  1f, 0f    /* top-right    */
        };
        buf.clear();
        buf.put(v).position(0);
        return buf;
    }
}
