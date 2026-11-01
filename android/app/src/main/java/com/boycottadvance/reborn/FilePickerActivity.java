package com.boycottadvance.reborn;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;
import android.os.Environment;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.AdapterView;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ListView;
import android.widget.TextView;

import java.io.File;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Comparator;
import java.util.List;

/**
 * The app's own file browser, used to pick a ROM or a BIOS image. It lists
 * directories and matching files, lets the user walk the tree and returns the
 * chosen path in the intent result.
 *
 * Two modes: MODE_ROM accepts .gba/.zip/.7z archives, MODE_BIOS accepts
 * .bin/.rom/.gba images. It starts in a sensible place (the app's folder, the
 * shared storage root or Downloads) so the user rarely has to navigate far.
 */
public class FilePickerActivity extends Activity {

    public static final String EXTRA_MODE = "mode";
    public static final String EXTRA_PATH = "path";
    public static final String RESULT_PATH = "result_path";
    public static final int MODE_ROM = 0;
    public static final int MODE_BIOS = 1;

    private int mode = MODE_ROM;
    private File current;

    private TextView pathView;
    private ListView listView;
    private final List<File> entries = new ArrayList<>();
    private ArrayAdapter<String> adapter;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        mode = getIntent().getIntExtra(EXTRA_MODE, MODE_ROM);
        String start = getIntent().getStringExtra(EXTRA_PATH);
        current = pickStartDir(start);

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        int pad = (int) (getResources().getDisplayMetrics().density * 8);
        root.setPadding(pad, pad, pad, pad);

        LinearLayout bar = new LinearLayout(this);
        bar.setOrientation(LinearLayout.HORIZONTAL);
        bar.setGravity(Gravity.CENTER_VERTICAL);
        Button up = new Button(this);
        up.setText("\u2191");     // up arrow
        up.setOnClickListener(v -> goUp());
        Button home = new Button(this);
        home.setText("Home");
        home.setOnClickListener(v -> { current = pickStartDir(null); refresh(); });
        pathView = new TextView(this);
        pathView.setTextSize(11f);
        pathView.setPadding(pad, 0, 0, 0);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f);
        bar.addView(up, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        bar.addView(home, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        bar.addView(pathView, lp);
        root.addView(bar);

        listView = new ListView(this);
        adapter = new ArrayAdapter<>(this, android.R.layout.simple_list_item_1);
        listView.setAdapter(adapter);
        listView.setOnItemClickListener(new AdapterView.OnItemClickListener() {
            @Override public void onItemClick(AdapterView<?> p, View v, int pos, long id) {
                File f = entries.get(pos);
                if (f.isDirectory()) { current = f; refresh(); }
                else {
                    Intent r = new Intent();
                    r.putExtra(RESULT_PATH, f.getAbsolutePath());
                    setResult(RESULT_OK, r);
                    finish();
                }
            }
        });
        root.addView(listView, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f));

        setContentView(root);
        refresh();
    }

    private File pickStartDir(String hint) {
        if (hint != null) {
            File f = new File(hint);
            if (f.isDirectory()) return f;
            if (f.getParentFile() != null && f.getParentFile().isDirectory())
                return f.getParentFile();
        }
        File ext = getExternalFilesDir(null);
        if (ext != null && ext.isDirectory()) return ext;
        File shared = Environment.getExternalStorageDirectory();
        if (shared != null && shared.isDirectory()) {
            File dl = new File(shared, "Download");
            if (dl.isDirectory()) return dl;
            return shared;
        }
        return new File("/");
    }

    private boolean matches(File f) {
        if (f.isDirectory()) return true;
        String n = f.getName().toLowerCase();
        if (mode == MODE_ROM) {
            return n.endsWith(".gba") || n.endsWith(".zip") || n.endsWith(".7z");
        }
        return n.endsWith(".bin") || n.endsWith(".rom") || n.endsWith(".gba");
    }

    private void goUp() {
        File parent = current.getParentFile();
        if (parent != null) { current = parent; refresh(); }
    }

    private void refresh() {
        pathView.setText(current.getAbsolutePath());
        entries.clear();
        File[] list = current.listFiles();
        if (list != null) {
            Arrays.sort(list, new Comparator<File>() {
                @Override public int compare(File a, File b) {
                    if (a.isDirectory() != b.isDirectory())
                        return a.isDirectory() ? -1 : 1;
                    return a.getName().compareToIgnoreCase(b.getName());
                }
            });
            for (File f : list) {
                if (f.getName().startsWith(".")) continue;
                if (matches(f)) entries.add(f);
            }
        }
        adapter.clear();
        for (File f : entries) {
            adapter.add((f.isDirectory() ? "[DIR] " : "      ") + f.getName());
        }
        adapter.notifyDataSetChanged();
    }

    @Override
    public void onBackPressed() {
        File parent = current.getParentFile();
        if (parent != null && current.canRead()) { goUp(); }
        else { setResult(RESULT_CANCELED); finish(); }
    }
}
