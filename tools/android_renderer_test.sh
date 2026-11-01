#!/bin/sh
# android_renderer_test.sh - regression check for the GL quad buffer.
#
# The renderer rewrites all 16 floats of the quad (4 vertices * (x, y, u, v))
# on every frame. An earlier version sized the direct buffer for a single
# vertex (4 floats), so the per-frame put() overflowed with
# BufferOverflowException. That exception is thrown on the GLSurfaceView GL
# thread, which does not catch it, so the whole process died - the "crashes
# right after the ROM starts" report.
#
# This runs the real QuadBuffer class on a desktop JVM (it has no Android
# dependency on purpose) and checks the buffer holds all 16 floats, and that
# the old sizing would indeed overflow. No device needed.
#
# Usage: sh tools/android_renderer_test.sh
set -e

ROOT=$(cd "$(dirname "$0")/.." && pwd)
WORK=${TMPDIR:-/tmp}/bars_android_render
JAVA_HOME=${JAVA_HOME:-$(dirname "$(dirname "$(command -v javac)")")}
if [ ! -x "$JAVA_HOME/bin/javac" ]; then echo "JDK not found"; exit 1; fi

rm -rf "$WORK"; mkdir -p "$WORK/src/com/boycottadvance/reborn" "$WORK/classes"

cp "$ROOT/android/app/src/main/java/com/boycottadvance/reborn/QuadBuffer.java" \
   "$WORK/src/com/boycottadvance/reborn/QuadBuffer.java"

cat > "$WORK/src/RendererTest.java" <<'EOF'
import com.boycottadvance.reborn.QuadBuffer;
import java.lang.reflect.Field;
import java.nio.FloatBuffer;

/** Exercises the real quad buffer the GL renderer draws with. */
public class RendererTest {
    public static void main(String[] a) throws Exception {
        QuadBuffer q = new QuadBuffer();
        FloatBuffer b = q.buffer();
        if (b.capacity() != QuadBuffer.FLOATS || QuadBuffer.FLOATS != 16) {
            System.out.println("FAIL: buffer holds " + b.capacity()
                    + " floats, expected 16 (4 vertices * (x,y,u,v))");
            System.exit(1);
        }

        /* The exact call the renderer makes each frame, twice (like two
         * frames), to be sure it can be rewritten. */
        q.update(1f, 1f);
        FloatBuffer out = q.update(0.75f, 1f);
        if (out.position() != 0) {
            System.out.println("FAIL: buffer not rewound for glVertexAttribPointer");
            System.exit(1);
        }
        float[] got = new float[16];
        out.get(got);
        /* Bottom-left vertex (x, y, u, v) = (-0.75, -1, 0, 1). */
        if (got[0] != -0.75f || got[1] != -1f || got[2] != 0f || got[3] != 1f) {
            System.out.println("FAIL: first vertex is wrong: "
                    + got[0] + "," + got[1] + "," + got[2] + "," + got[3]);
            System.exit(1);
        }

        /* Prove the regression this test guards against: a buffer sized for
         * one vertex overflows when the frame is written. */
        java.nio.FloatBuffer small = java.nio.ByteBuffer.allocateDirect(4 * 4)
                .order(java.nio.ByteOrder.nativeOrder()).asFloatBuffer();
        boolean overflowed = false;
        try {
            small.clear();
            small.put(new float[] { -1, -1, 0, 1, 1, -1, 1, 1, -1, 1, 0, 0, 1, 1, 1, 0 });
        } catch (java.nio.BufferOverflowException e) {
            overflowed = true;
        }
        if (!overflowed) {
            System.out.println("FAIL: the old single-vertex sizing did not overflow; "
                    + "the test no longer guards the bug");
            System.exit(1);
        }

        System.out.println("RENDERER_OK");
    }
}
EOF

"$JAVA_HOME/bin/javac" -d "$WORK/classes" \
    "$WORK/src/com/boycottadvance/reborn/QuadBuffer.java" "$WORK/src/RendererTest.java"

set +e
"$JAVA_HOME/bin/java" -cp "$WORK/classes" RendererTest 2>&1 | tee "$WORK/render.out"
RC=$?
set -e

if grep -q "RENDERER_OK" "$WORK/render.out" && [ "$RC" = 0 ]; then
    echo "PASS: the GL quad buffer holds all 16 floats and can be rewritten per frame"
else
    echo "FAIL: renderer quad buffer check failed (rc=$RC)"
    exit 1
fi
