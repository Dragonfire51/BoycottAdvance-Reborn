package com.boycottadvance.reborn;

import android.app.Activity;
import android.content.Intent;
import android.content.SharedPreferences;
import android.opengl.GLSurfaceView;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.view.Gravity;
import android.view.Menu;
import android.view.MenuItem;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.PopupMenu;
import android.widget.TextView;
import android.widget.Toast;

import java.io.File;

/**
 * Main screen: the emulator view with the on-screen GBA controls overlaid, a
 * top bar with the menu and the ROM picker. Mirrors the features of the
 * desktop builds (load ROM, load BIOS, reset, savestates in 10 slots, sound
 * toggles, scanline/smoothing, fast-forward, history) using the same native
 * core through acore.
 */
public class MainActivity extends Activity implements TouchControlsView.Listener {

    private static final int REQ_ROM = 1;
    private static final int REQ_BIOS = 2;

    private NativeEmulator emu;
    private EmulatorThread thread;
    private GLSurfaceView glView;
    private GbaRenderer renderer;
    private TouchControlsView controls;
    private TextView status;
    private Button fastFwdBtn;

    private SharedPreferences prefs;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        prefs = getSharedPreferences("bars", MODE_PRIVATE);

        emu = new NativeEmulator();
        if (!emu.isValid()) {
            Toast.makeText(this, "No se pudo crear el nucleo", Toast.LENGTH_LONG).show();
            finish();
            return;
        }
        loadSettings();

        FrameLayout root = new FrameLayout(this);
        glView = new GLSurfaceView(this);
        glView.setEGLContextClientVersion(2);
        renderer = new GbaRenderer(new GbaRenderer.FrameSource() {
            @Override public void copyFrame(int[] out) { thread.copyFrame(out); }
        });
        glView.setRenderer(renderer);
        glView.setRenderMode(GLSurfaceView.RENDERMODE_CONTINUOUSLY);
        root.addView(glView, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));

        controls = new TouchControlsView(this);
        controls.setListener(this);
        root.addView(controls, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));

        /* Top bar: ROM / menu / status. */
        LinearLayout bar = new LinearLayout(this);
        bar.setOrientation(LinearLayout.HORIZONTAL);
        bar.setGravity(Gravity.CENTER_VERTICAL);
        bar.setBackgroundColor(0x99000000);
        int pad = (int) (getResources().getDisplayMetrics().density * 6);
        bar.setPadding(pad, pad, pad, pad);
        Button romBtn = new Button(this);
        romBtn.setText("ROM");
        romBtn.setOnClickListener(v -> pickRom());
        Button menuBtn = new Button(this);
        menuBtn.setText("\u2630");   // hamburger
        menuBtn.setOnClickListener(v -> showMenu(menuBtn));
        fastFwdBtn = new Button(this);
        fastFwdBtn.setText(">>");
        fastFwdBtn.setOnClickListener(v -> toggleFastForward());
        status = new TextView(this);
        status.setTextColor(0xFFFFFFFF);
        status.setTextSize(11f);
        status.setPadding(pad, 0, 0, 0);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f);
        bar.addView(romBtn, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        bar.addView(menuBtn, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        bar.addView(fastFwdBtn, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        bar.addView(status, lp);
        FrameLayout.LayoutParams barLp = new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        barLp.gravity = Gravity.TOP;
        root.addView(bar, barLp);

        setContentView(root);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);

        thread = new EmulatorThread(emu);
        thread.start();

        /* Auto-load BIOS + last ROM if they exist. */
        String bios = prefs.getString("bios", null);
        if (bios != null && new File(bios).exists()) emu.loadBios(bios);
        String lastRom = prefs.getString("last_rom", null);
        if (lastRom != null && new File(lastRom).exists()) loadRom(lastRom);
        else pickRom();
    }

    /* ---- settings ---- */
    private void loadSettings() {
        emu.setInt(NativeEmulator.SET_SOUND, prefs.getInt("sound", 1));
        emu.setInt(NativeEmulator.SET_DSOUND_A, prefs.getInt("dsound_a", 1));
        emu.setInt(NativeEmulator.SET_DSOUND_B, prefs.getInt("dsound_b", 1));
        emu.setInt(NativeEmulator.SET_GBC_SOUND, prefs.getInt("gbc_sound", 1));
        emu.setInt(NativeEmulator.SET_REVERSE_STEREO, prefs.getInt("rev_stereo", 0));
        emu.setInt(NativeEmulator.SET_BIOS_ENABLE, prefs.getInt("bios_enable", 1));
        emu.setInt(NativeEmulator.SET_BIOS_FASTBOOT, prefs.getInt("bios_fastboot", 0));
        emu.setInt(NativeEmulator.SET_SKIP_INTROS, prefs.getInt("skip_intros", 0));
        emu.setInt(NativeEmulator.SET_FRAMESKIP, prefs.getInt("frameskip", 0));
        emu.setInt(NativeEmulator.SET_AUTOFRAMESKIP, prefs.getInt("autoframeskip", 0));
        emu.setInt(NativeEmulator.SET_SCANLINE, prefs.getInt("scanline", 0));
        emu.setInt(NativeEmulator.SET_SMOOTH, prefs.getInt("smooth", 0));
        emu.setInt(NativeEmulator.SET_STATE_SLOT, prefs.getInt("state_slot", 0));
        emu.setInt(NativeEmulator.SET_SHOWFPS, prefs.getInt("showfps", 1));
        if (renderer != null) renderer.setSmooth(prefs.getInt("smooth", 0) != 0);
    }

    private void putBool(String k, boolean v) { prefs.edit().putBoolean(k, v).apply(); }

    private boolean getFlag(String k, boolean d) { return prefs.getBoolean(k, d); }

    /* ---- ROM / BIOS ---- */
    private void pickRom() {
        Intent i = new Intent(this, FilePickerActivity.class);
        i.putExtra(FilePickerActivity.EXTRA_MODE, FilePickerActivity.MODE_ROM);
        String last = prefs.getString("last_rom", null);
        if (last != null) i.putExtra(FilePickerActivity.EXTRA_PATH, last);
        startActivityForResult(i, REQ_ROM);
    }

    private void pickBios() {
        Intent i = new Intent(this, FilePickerActivity.class);
        i.putExtra(FilePickerActivity.EXTRA_MODE, FilePickerActivity.MODE_BIOS);
        String last = prefs.getString("bios", null);
        if (last != null) i.putExtra(FilePickerActivity.EXTRA_PATH, last);
        startActivityForResult(i, REQ_BIOS);
    }

    private void loadRom(String path) {
        if (emu.loadRom(path)) {
            prefs.edit().putString("last_rom", path).apply();
            updateStatus();
        } else {
            Toast.makeText(this, "No se pudo cargar la ROM", Toast.LENGTH_LONG).show();
        }
    }

    private void updateStatus() {
        String info = emu.romInfo();
        String[] parts = info.split("\n");
        String t = parts.length > 0 ? parts[0] : "";
        String c = parts.length > 1 ? parts[1] : "";
        String s = parts.length > 2 ? parts[2] : "";
        status.setText(t + "  [" + c + "]  " + s);
    }

    @Override
    protected void onActivityResult(int req, int res, Intent data) {
        super.onActivityResult(req, res, data);
        if (res != RESULT_OK || data == null) return;
        String path = data.getStringExtra(FilePickerActivity.RESULT_PATH);
        if (path == null) return;
        if (req == REQ_ROM) loadRom(path);
        else if (req == REQ_BIOS) {
            if (emu.loadBios(path)) {
                prefs.edit().putString("bios", path).apply();
                Toast.makeText(this, "BIOS: " + new File(path).getName(), Toast.LENGTH_SHORT).show();
                if (emu.romLoaded()) emu.reset();
            } else {
                Toast.makeText(this, "BIOS invalido (necesita 16 KB)", Toast.LENGTH_LONG).show();
            }
        }
    }

    /* ---- menu ---- */
    private void showMenu(View anchor) {
        PopupMenu m = new PopupMenu(this, anchor);
        final int ID_ROM = 1, ID_BIOS = 2, ID_RESET = 3, ID_RUN = 4, ID_LOAD = 5,
                  ID_SAVE = 6, ID_SOUND = 7, ID_SCANLINE = 8, ID_SMOOTH = 9,
                  ID_BOOT = 10, ID_SKIP = 11, ID_FPS = 12, ID_SLOT = 13, ID_HIST = 14;
        m.getMenu().add(Menu.NONE, ID_ROM, 0, "Cargar ROM...");
        m.getMenu().add(Menu.NONE, ID_BIOS, 1, "Cargar BIOS...");
        m.getMenu().add(Menu.NONE, ID_RESET, 2, "Reiniciar");
        m.getMenu().add(Menu.NONE, ID_RUN, 3, thread.isPaused() ? "Reanudar" : "Pausa");
        m.getMenu().add(Menu.NONE, ID_SLOT, 4, "Ranura de estado: " + (emu.getInt(NativeEmulator.SET_STATE_SLOT) + 1));
        m.getMenu().add(Menu.NONE, ID_LOAD, 5, "Cargar estado");
        m.getMenu().add(Menu.NONE, ID_SAVE, 6, "Guardar estado");
        m.getMenu().add(Menu.NONE, ID_SOUND, 7, "Sonido: " + (emu.getInt(NativeEmulator.SET_SOUND) != 0 ? "on" : "off"));
        m.getMenu().add(Menu.NONE, ID_SCANLINE, 8, "Scanlines: " + (emu.getInt(NativeEmulator.SET_SCANLINE) != 0 ? "on" : "off"));
        m.getMenu().add(Menu.NONE, ID_SMOOTH, 9, "Suavizado: " + (emu.getInt(NativeEmulator.SET_SMOOTH) != 0 ? "on" : "off"));
        m.getMenu().add(Menu.NONE, ID_BOOT, 10, "Skip BIOS Boot: " + (emu.getInt(NativeEmulator.SET_BIOS_FASTBOOT) != 0 ? "on" : "off"));
        m.getMenu().add(Menu.NONE, ID_SKIP, 11, "Skip Intro Hacks: " + (emu.getInt(NativeEmulator.SET_SKIP_INTROS) != 0 ? "on" : "off"));
        m.getMenu().add(Menu.NONE, ID_FPS, 12, "Mostrar FPS: " + (emu.getInt(NativeEmulator.SET_SHOWFPS) != 0 ? "on" : "off"));
        m.getMenu().add(Menu.NONE, ID_HIST, 13, "Historial / Acerca de");
        m.setOnMenuItemClickListener(new PopupMenu.OnMenuItemClickListener() {
            @Override public boolean onMenuItemClick(MenuItem item) {
                switch (item.getItemId()) {
                    case ID_ROM: pickRom(); break;
                    case ID_BIOS: pickBios(); break;
                    case ID_RESET: emu.reset(); break;
                    case ID_RUN: thread.setPaused(!thread.isPaused()); break;
                    case ID_SLOT: {
                        int s = (emu.getInt(NativeEmulator.SET_STATE_SLOT) + 1) % 10;
                        emu.setInt(NativeEmulator.SET_STATE_SLOT, s);
                        prefs.edit().putInt("state_slot", s).apply();
                        break;
                    }
                    case ID_LOAD: {
                        int r = emu.loadState(emu.getInt(NativeEmulator.SET_STATE_SLOT));
                        Toast.makeText(MainActivity.this, r == 0 ? "Estado cargado" : "Sin estado", Toast.LENGTH_SHORT).show();
                        break;
                    }
                    case ID_SAVE: {
                        int r = emu.saveState(emu.getInt(NativeEmulator.SET_STATE_SLOT));
                        Toast.makeText(MainActivity.this, r == 0 ? "Estado guardado" : "Error", Toast.LENGTH_SHORT).show();
                        break;
                    }
                    case ID_SOUND: {
                        boolean v = emu.getInt(NativeEmulator.SET_SOUND) == 0;
                        emu.setInt(NativeEmulator.SET_SOUND, v ? 1 : 0);
                        prefs.edit().putInt("sound", v ? 1 : 0).apply();
                        break;
                    }
                    case ID_SCANLINE: {
                        boolean v = emu.getInt(NativeEmulator.SET_SCANLINE) == 0;
                        emu.setInt(NativeEmulator.SET_SCANLINE, v ? 1 : 0);
                        prefs.edit().putInt("scanline", v ? 1 : 0).apply();
                        break;
                    }
                    case ID_SMOOTH: {
                        boolean v = emu.getInt(NativeEmulator.SET_SMOOTH) == 0;
                        emu.setInt(NativeEmulator.SET_SMOOTH, v ? 1 : 0);
                        prefs.edit().putInt("smooth", v ? 1 : 0).apply();
                        renderer.setSmooth(v);
                        break;
                    }
                    case ID_BOOT: {
                        boolean v = emu.getInt(NativeEmulator.SET_BIOS_FASTBOOT) == 0;
                        emu.setInt(NativeEmulator.SET_BIOS_FASTBOOT, v ? 1 : 0);
                        prefs.edit().putInt("bios_fastboot", v ? 1 : 0).apply();
                        emu.reset();
                        break;
                    }
                    case ID_SKIP: {
                        boolean v = emu.getInt(NativeEmulator.SET_SKIP_INTROS) == 0;
                        emu.setInt(NativeEmulator.SET_SKIP_INTROS, v ? 1 : 0);
                        prefs.edit().putInt("skip_intros", v ? 1 : 0).apply();
                        emu.reset();
                        break;
                    }
                    case ID_FPS: {
                        boolean v = emu.getInt(NativeEmulator.SET_SHOWFPS) == 0;
                        emu.setInt(NativeEmulator.SET_SHOWFPS, v ? 1 : 0);
                        prefs.edit().putInt("showfps", v ? 1 : 0).apply();
                        break;
                    }
                    case ID_HIST: showHistory(); break;
                }
                return true;
            }
        });
        m.show();
    }

    private void showHistory() {
        new android.app.AlertDialog.Builder(this)
            .setTitle("BoycottAdvance Reborn")
            .setMessage("Emulador GBA (C).\n\n" +
                        "Version 1.4.27\n" +
                        "Nucleo C99 compartido con los builds de escritorio.\n\n" +
                        "Controles: D-pad, A/B, L/R, SELECT/START en pantalla.")
            .setPositiveButton("OK", null)
            .show();
    }

    private void toggleFastForward() {
        boolean ff = !thread.isFastForward();
        thread.setFastForward(ff);
        fastFwdBtn.setText(ff ? "|>" : ">>");
    }

    /* ---- touch controls ---- */
    @Override
    public void onKeysChanged(int mask) {
        if (thread != null) thread.setKeys(mask);
    }

    /* ---- lifecycle ---- */
    @Override
    protected void onPause() {
        super.onPause();
        if (glView != null) glView.onPause();
        if (thread != null) thread.setPaused(true);
        if (emu != null) emu.saveBattery();
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (glView != null) glView.onResume();
        if (thread != null) thread.setPaused(false);
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        if (thread != null) thread.shutdown();
        if (emu != null) { emu.saveBattery(); emu.close(); }
    }

    @Override
    public void onBackPressed() {
        new android.app.AlertDialog.Builder(this)
            .setTitle("Salir")
            .setMessage("Cerrar el emulador?")
            .setPositiveButton("Si", (d, w) -> { finish(); })
            .setNegativeButton("No", null)
            .show();
    }
}
