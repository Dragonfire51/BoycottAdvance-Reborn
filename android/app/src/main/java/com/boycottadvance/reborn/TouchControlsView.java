package com.boycottadvance.reborn;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.RectF;
import android.util.AttributeSet;
import android.view.MotionEvent;
import android.view.View;

import java.util.ArrayList;
import java.util.List;

/**
 * On-screen GBA controls, laid out the way Android GBA emulators do: a fixed
 * D-pad bottom-left, A/B bottom-right, L/R above them and SELECT/START in the
 * middle. Every control is an independent touch zone, so multi-touch (hold a
 * direction and press A) works.
 *
 * The D-pad is one circular zone split into four sectors with a dead centre;
 * sliding the thumb from one sector to another changes direction without
 * lifting the finger, which is how these controls are normally used.
 */
public class TouchControlsView extends View {

    public interface Listener { void onKeysChanged(int mask); }

    private Listener listener;

    private final Paint fill   = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint stroke = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint label  = new Paint(Paint.ANTI_ALIAS_FLAG);

    private float dpadCx, dpadCy, dpadR;
    private int dpadPointerId = -1;
    private int dpadMask = 0;

    private static final class Btn {
        final int key; final String text;
        float cx, cy, r;
        int pointerId = -1;
        boolean down;
        Btn(int key, String text) { this.key = key; this.text = text; }
    }
    private final List<Btn> buttons = new ArrayList<>();
    private Btn bA, bB, bL, bR, bSelect, bStart;

    private int mask = 0;

    public TouchControlsView(Context c) { super(c); init(); }
    public TouchControlsView(Context c, AttributeSet a) { super(c, a); init(); }

    public void setListener(Listener l) { listener = l; }

    private void init() {
        fill.setColor(Color.argb(90, 255, 255, 255));
        stroke.setColor(Color.argb(160, 255, 255, 255));
        stroke.setStyle(Paint.Style.STROKE);
        stroke.setStrokeWidth(3f);
        label.setColor(Color.argb(220, 255, 255, 255));
        label.setTextAlign(Paint.Align.CENTER);
        label.setFakeBoldText(true);

        bA      = new Btn(NativeEmulator.KEY_A, "A");
        bB      = new Btn(NativeEmulator.KEY_B, "B");
        bL      = new Btn(NativeEmulator.KEY_L, "L");
        bR      = new Btn(NativeEmulator.KEY_R, "R");
        bSelect = new Btn(NativeEmulator.KEY_SELECT, "SEL");
        bStart  = new Btn(NativeEmulator.KEY_START, "STA");
        buttons.add(bA); buttons.add(bB); buttons.add(bL); buttons.add(bR);
        buttons.add(bSelect); buttons.add(bStart);
    }

    @Override
    protected void onSizeChanged(int w, int h, int ow, int oh) {
        float unit = Math.min(w, h);
        dpadR = unit * 0.17f;
        dpadCx = w * 0.17f;
        dpadCy = h * 0.68f;

        float br = unit * 0.085f;
        bA.cx = w - unit * 0.14f; bA.cy = h * 0.70f; bA.r = br;
        bB.cx = w - unit * 0.29f; bB.cy = h * 0.78f; bB.r = br;
        bL.cx = w - unit * 0.32f; bL.cy = h * 0.32f; bL.r = br * 0.85f;
        bR.cx = w - unit * 0.11f; bR.cy = h * 0.32f; bR.r = br * 0.85f;
        float sr = unit * 0.055f;
        bSelect.cx = w * 0.42f; bSelect.cy = h * 0.93f; bSelect.r = sr;
        bStart.cx  = w * 0.58f; bStart.cy  = h * 0.93f; bStart.r = sr;

        label.setTextSize(unit * 0.05f);
    }

    /* Direction from a point inside the D-pad zone; 0 in the dead centre. */
    private int dpadDirAt(float x, float y) {
        float dx = x - dpadCx, dy = y - dpadCy;
        float dist = (float) Math.hypot(dx, dy);
        if (dist < dpadR * 0.28f) return 0;            /* dead zone */
        if (Math.abs(dx) > Math.abs(dy)) {
            return dx > 0 ? NativeEmulator.KEY_RIGHT : NativeEmulator.KEY_LEFT;
        }
        return dy > 0 ? NativeEmulator.KEY_DOWN : NativeEmulator.KEY_UP;
    }

    private boolean inDpad(float x, float y) {
        float dx = x - dpadCx, dy = y - dpadCy;
        return dx * dx + dy * dy <= dpadR * dpadR * 1.8f;
    }

    private void recomputeMask() {
        int m = dpadMask;
        for (Btn b : buttons) if (b.down) m |= b.key;
        if (m != mask) {
            mask = m;
            if (listener != null) listener.onKeysChanged(mask);
        }
    }

    private Btn hitButton(float x, float y) {
        for (Btn b : buttons) {
            float dx = x - b.cx, dy = y - b.cy;
            if (dx * dx + dy * dy <= b.r * b.r * 1.6f) return b;
        }
        return null;
    }

    @Override
    public boolean onTouchEvent(MotionEvent e) {
        int action = e.getActionMasked();
        switch (action) {
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_POINTER_DOWN: {
                int idx = e.getActionIndex();
                int id = e.getPointerId(idx);
                float x = e.getX(idx), y = e.getY(idx);
                Btn b = hitButton(x, y);
                if (b != null) {
                    b.pointerId = id; b.down = true;
                } else if (inDpad(x, y) && dpadPointerId < 0) {
                    dpadPointerId = id;
                    dpadMask = dpadDirAt(x, y);
                }
                recomputeMask();
                invalidate();
                return true;
            }
            case MotionEvent.ACTION_MOVE: {
                for (int i = 0; i < e.getPointerCount(); i++) {
                    if (e.getPointerId(i) == dpadPointerId) {
                        dpadMask = dpadDirAt(e.getX(i), e.getY(i));
                    }
                }
                recomputeMask();
                invalidate();
                return true;
            }
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_POINTER_UP: {
                int idx = e.getActionIndex();
                int id = e.getPointerId(idx);
                if (id == dpadPointerId) { dpadPointerId = -1; dpadMask = 0; }
                for (Btn b : buttons) if (b.pointerId == id) { b.pointerId = -1; b.down = false; }
                recomputeMask();
                invalidate();
                return true;
            }
            case MotionEvent.ACTION_CANCEL: {
                dpadPointerId = -1; dpadMask = 0;
                for (Btn b : buttons) { b.pointerId = -1; b.down = false; }
                recomputeMask();
                invalidate();
                return true;
            }
        }
        return super.onTouchEvent(e);
    }

    @Override
    protected void onDraw(Canvas c) {
        super.onDraw(c);

        float t = dpadR * 0.40f;
        boolean u = (dpadMask & NativeEmulator.KEY_UP) != 0;
        boolean d = (dpadMask & NativeEmulator.KEY_DOWN) != 0;
        boolean l = (dpadMask & NativeEmulator.KEY_LEFT) != 0;
        boolean r = (dpadMask & NativeEmulator.KEY_RIGHT) != 0;
        RectF up    = new RectF(dpadCx - t, dpadCy - dpadR, dpadCx + t, dpadCy);
        RectF down  = new RectF(dpadCx - t, dpadCy, dpadCx + t, dpadCy + dpadR);
        RectF left  = new RectF(dpadCx - dpadR, dpadCy - t, dpadCx, dpadCy + t);
        RectF right = new RectF(dpadCx, dpadCy - t, dpadCx + dpadR, dpadCy + t);
        c.drawRect(up, u ? fill : stroke);
        c.drawRect(down, d ? fill : stroke);
        c.drawRect(left, l ? fill : stroke);
        c.drawRect(right, r ? fill : stroke);
        c.drawCircle(dpadCx, dpadCy, dpadR, stroke);

        for (Btn b : buttons) {
            c.drawCircle(b.cx, b.cy, b.r, b.down ? fill : stroke);
            c.drawText(b.text, b.cx, b.cy + label.getTextSize() * 0.35f, label);
        }
    }
}
