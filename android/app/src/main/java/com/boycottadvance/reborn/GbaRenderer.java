package com.boycottadvance.reborn;

import android.opengl.GLES20;
import android.opengl.GLSurfaceView;
import android.opengl.GLUtils;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.FloatBuffer;

import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

/**
 * Draws the emulator frame (a 240x160 ARGB texture) with OpenGL ES 2.0.
 *
 * The picture keeps the GBA's 3:2 aspect ratio and is centred with black
 * bars, so it is never distorted. Filtering is GL_NEAREST by default (crisp
 * pixels); GL_LINEAR when smoothing is on. The texture is uploaded once per
 * frame and the whole view is cleared to black first.
 */
public class GbaRenderer implements GLSurfaceView.Renderer {

    public interface FrameSource { void copyFrame(int[] out); }

    private static final String VERT =
        "attribute vec2 aPos;\n" +
        "attribute vec2 aTex;\n" +
        "varying vec2 vTex;\n" +
        "void main() {\n" +
        "  gl_Position = vec4(aPos, 0.0, 1.0);\n" +
        "  vTex = aTex;\n" +
        "}\n";

    private static final String FRAG =
        "precision mediump float;\n" +
        "varying vec2 vTex;\n" +
        "uniform sampler2D uTex;\n" +
        "void main() { gl_FragColor = texture2D(uTex, vTex); }\n";

    private final FrameSource source;
    private final int[] argb = new int[EmulatorThread.W * EmulatorThread.H];
    private final int[] texBytes = new int[EmulatorThread.W * EmulatorThread.H];

    private int program, texId, aPos, aTex, uTex;
    private QuadBuffer quad;
    /* Direct buffer for the texture upload, reused every frame: allocating a
     * fresh 150 KB direct buffer per frame (9 MB/s) can exhaust direct memory
     * and throw OutOfMemoryError on low-end devices. */
    private final ByteBuffer texBuf = ByteBuffer
            .allocateDirect(EmulatorThread.W * EmulatorThread.H * 4)
            .order(ByteOrder.nativeOrder());
    private int viewW = 1, viewH = 1;
    private volatile boolean smooth = false;

    public GbaRenderer(FrameSource src) { this.source = src; }

    public void setSmooth(boolean s) { smooth = s; }

    @Override
    public void onSurfaceCreated(GL10 gl, EGLConfig config) {
        program = buildProgram(VERT, FRAG);
        aPos = GLES20.glGetAttribLocation(program, "aPos");
        aTex = GLES20.glGetAttribLocation(program, "aTex");
        uTex = GLES20.glGetUniformLocation(program, "uTex");

        int[] t = new int[1];
        GLES20.glGenTextures(1, t, 0);
        texId = t[0];
        GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, texId);
        GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_WRAP_S, GLES20.GL_CLAMP_TO_EDGE);
        GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_WRAP_T, GLES20.GL_CLAMP_TO_EDGE);
        applyFilter();
        GLES20.glClearColor(0f, 0f, 0f, 1f);

        quad = new QuadBuffer();
    }

    private void applyFilter() {
        int f = smooth ? GLES20.GL_LINEAR : GLES20.GL_NEAREST;
        GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_MIN_FILTER, f);
        GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_MAG_FILTER, f);
    }

    @Override
    public void onSurfaceChanged(GL10 gl, int w, int h) {
        viewW = Math.max(1, w);
        viewH = Math.max(1, h);
        GLES20.glViewport(0, 0, viewW, viewH);
    }

    @Override
    public void onDrawFrame(GL10 gl) {
        GLES20.glClear(GLES20.GL_COLOR_BUFFER_BIT);

        source.copyFrame(argb);
        /* ARGB (0xAARRGGBB) -> the texture expects RGBA bytes. */
        for (int i = 0; i < argb.length; i++) {
            int c = argb[i];
            texBytes[i] = ((c & 0x00FF0000) >> 16) | (c & 0x0000FF00) |
                          ((c & 0x000000FF) << 16) | 0xFF000000;
        }
        ByteBuffer buf = texBuf;
        buf.clear();
        buf.asIntBuffer().put(texBytes);
        buf.position(0);

        GLES20.glUseProgram(program);
        GLES20.glActiveTexture(GLES20.GL_TEXTURE0);
        GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, texId);
        applyFilter();
        GLES20.glTexImage2D(GLES20.GL_TEXTURE_2D, 0, GLES20.GL_RGBA,
                EmulatorThread.W, EmulatorThread.H, 0,
                GLES20.GL_RGBA, GLES20.GL_UNSIGNED_BYTE, buf);
        GLES20.glUniform1i(uTex, 0);

        /* Keep 3:2 aspect: fit the largest centred rectangle. */
        float targetAspect = (float) EmulatorThread.W / EmulatorThread.H;
        float viewAspect = (float) viewW / viewH;
        float sx = 1f, sy = 1f;
        if (viewAspect > targetAspect) sx = targetAspect / viewAspect;
        else sy = viewAspect / targetAspect;

        FloatBuffer qb = quad.update(sx, sy);

        int stride = 4 * 4;
        qb.position(0);
        GLES20.glVertexAttribPointer(aPos, 2, GLES20.GL_FLOAT, false, stride, qb);
        GLES20.glEnableVertexAttribArray(aPos);
        qb.position(2);
        GLES20.glVertexAttribPointer(aTex, 2, GLES20.GL_FLOAT, false, stride, qb);
        GLES20.glEnableVertexAttribArray(aTex);

        GLES20.glDrawArrays(GLES20.GL_TRIANGLE_STRIP, 0, 4);
    }

    private static int buildProgram(String vs, String fs) {
        int v = compile(GLES20.GL_VERTEX_SHADER, vs);
        int f = compile(GLES20.GL_FRAGMENT_SHADER, fs);
        int p = GLES20.glCreateProgram();
        GLES20.glAttachShader(p, v);
        GLES20.glAttachShader(p, f);
        GLES20.glLinkProgram(p);
        return p;
    }

    private static int compile(int type, String src) {
        int s = GLES20.glCreateShader(type);
        GLES20.glShaderSource(s, src);
        GLES20.glCompileShader(s);
        return s;
    }
}
