package com.boycottadvance.reborn;

import android.app.Activity;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.provider.DocumentsContract;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.AdapterView;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ListView;
import android.widget.TextView;
import android.widget.Toast;

import java.io.File;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Comparator;
import java.util.List;

/**
 * The app's own file browser, used to pick a ROM or a BIOS image.
 *
 * Android 11+ ignores requestLegacyExternalStorage, so listing a shared
 * folder with File.listFiles() returns nothing on a modern phone. The browser
 * therefore works through the Storage Access Framework: the user grants a
 * folder once with "Carpeta", the permission is persisted and the tree is
 * listed with DocumentsContract. "Archivo" picks a single document. The app's
 * own folder is always browsable without any permission.
 *
 * The chosen item is returned as a content:// URI (SAF) or a plain path (app
 * folder); MainActivity's RomStore turns either into a real file.
 */
public class FilePickerActivity extends Activity {

    public static final String EXTRA_MODE = "mode";
    public static final String RESULT_REF  = "result_ref";
    public static final String RESULT_NAME = "result_name";
    public static final int MODE_ROM = 0;
    public static final int MODE_BIOS = 1;

    private static final int REQ_TREE = 1;
    private static final int REQ_FILE = 2;
    private static final String PREF_TREE = "saf_tree";

    private int mode = MODE_ROM;

    private TextView pathView;
    private ListView listView;
    private final List<Item> entries = new ArrayList<>();
    private ArrayAdapter<String> adapter;
    private android.content.SharedPreferences prefs;

    /* Current location: either a real directory or a SAF document tree. */
    private File currentDir;
    private Uri treeUri;                     /* root of the granted folder */
    private final List<Uri> dirStack = new ArrayList<>();
    private final List<String> nameStack = new ArrayList<>();

    private static final class Item {
        final String name;
        final File file;      /* app-folder entries */
        final Uri uri;        /* SAF entries */
        final boolean dir;
        Item(String n, File f, Uri u, boolean d) { name = n; file = f; uri = u; dir = d; }
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        mode = getIntent().getIntExtra(EXTRA_MODE, MODE_ROM);

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        int pad = (int) (getResources().getDisplayMetrics().density * 8);
        root.setPadding(pad, pad, pad, pad);

        LinearLayout bar = new LinearLayout(this);
        bar.setOrientation(LinearLayout.HORIZONTAL);
        bar.setGravity(Gravity.CENTER_VERTICAL);
        Button up = new Button(this);
        up.setText("\u2191");
        up.setOnClickListener(v -> goUp());
        Button app = new Button(this);
        app.setText("App");
        app.setOnClickListener(v -> { currentDir = RomStore.romDir(this); refresh(); });
        Button folder = new Button(this);
        folder.setText("Carpeta");
        folder.setOnClickListener(v -> showFolder());
        Button file = new Button(this);
        file.setText("Archivo");
        file.setOnClickListener(v -> openFilePicker());
        pathView = new TextView(this);
        pathView.setTextSize(11f);
        pathView.setPadding(pad, 0, 0, 0);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f);
        bar.addView(up, wr());
        bar.addView(app, wr());
        bar.addView(folder, wr());
        bar.addView(file, wr());
        bar.addView(pathView, lp);
        root.addView(bar);

        listView = new ListView(this);
        adapter = new ArrayAdapter<>(this, android.R.layout.simple_list_item_1);
        listView.setAdapter(adapter);
        listView.setOnItemClickListener(new AdapterView.OnItemClickListener() {
            @Override public void onItemClick(AdapterView<?> p, View v, int pos, long id) {
                Item it = entries.get(pos);
                if (it.dir) {
                    if (it.file != null) { currentDir = it.file; refresh(); }
                    else { pushDir(it.uri, it.name); }
                } else {
                    returnResult(it.uri != null ? it.uri.toString()
                                                : it.file.getAbsolutePath(), it.name);
                }
            }
        });
        root.addView(listView, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f));

        setContentView(root);

        prefs = getSharedPreferences("bars", MODE_PRIVATE);
        restoreTree();

        /* Prefer the folder granted earlier; otherwise the app's own folder,
         * which is always readable without any permission. */
        if (treeUri == null) currentDir = RomStore.romDir(this);
        refresh();
    }

    /** Re-open the folder granted in an earlier run, if the grant survives. */
    private void restoreTree() {
        String saved = prefs.getString(PREF_TREE, null);
        if (saved == null) return;
        Uri uri = Uri.parse(saved);
        try {
            getContentResolver().takePersistableUriPermission(
                    uri, Intent.FLAG_GRANT_READ_URI_PERMISSION);
        } catch (Exception ignored) { }
        treeUri = uri;
    }

    private LinearLayout.LayoutParams wr() {
        return new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT);
    }

    private void returnResult(String ref, String name) {
        Intent r = new Intent();
        r.putExtra(RESULT_REF, ref);
        r.putExtra(RESULT_NAME, name);
        setResult(RESULT_OK, r);
        finish();
    }

    /* ---- SAF pickers ---- */
    /** If a folder was already granted, browse it; otherwise ask for one. */
    private void showFolder() {
        if (treeUri == null) { openTreePicker(); return; }
        currentDir = null;
        refresh();
    }

    private void openTreePicker() {
        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
        i.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION |
                   Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
        try {
            startActivityForResult(i, REQ_TREE);
        } catch (Exception e) {
            Toast.makeText(this, "Sin selector de carpetas", Toast.LENGTH_SHORT).show();
        }
    }

    private void openFilePicker() {
        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        i.addCategory(Intent.CATEGORY_OPENABLE);
        i.setType("*/*");
        i.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
        try {
            startActivityForResult(i, REQ_FILE);
        } catch (Exception e) {
            Toast.makeText(this, "Sin selector de archivos", Toast.LENGTH_SHORT).show();
        }
    }

    @Override
    protected void onActivityResult(int req, int res, Intent data) {
        super.onActivityResult(req, res, data);
        if (res != RESULT_OK || data == null || data.getData() == null) return;
        Uri uri = data.getData();
        if (req == REQ_FILE) {
            String name = displayName(uri);
            if (!matches(name)) {
                Toast.makeText(this, "Tipo de archivo no valido", Toast.LENGTH_SHORT).show();
                return;
            }
            returnResult(uri.toString(), name);
            return;
        }
        if (req == REQ_TREE) {
            try {
                getContentResolver().takePersistableUriPermission(
                        uri, Intent.FLAG_GRANT_READ_URI_PERMISSION);
            } catch (Exception ignored) { }
            treeUri = uri;
            prefs.edit().putString(PREF_TREE, uri.toString()).apply();
            dirStack.clear();
            nameStack.clear();
            currentDir = null;
            refresh();
        }
    }

    /* ---- listing ---- */
    private void refresh() {
        entries.clear();
        adapter.clear();

        if (currentDir != null) {
            pathView.setText(currentDir.getAbsolutePath());
            File[] list = currentDir.listFiles();
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
                    if (matches(f.getName())) entries.add(new Item(f.getName(), f, null, f.isDirectory()));
                }
            }
        } else if (treeUri != null) {
            Uri dir = dirStack.isEmpty() ? treeUri : dirStack.get(dirStack.size() - 1);
            String label = nameStack.isEmpty() ? "Carpeta elegida" : nameStack.get(nameStack.size() - 1);
            pathView.setText(label);
            listTree(dir);
        }

        for (Item it : entries)
            adapter.add((it.dir ? "[DIR] " : "      ") + it.name);
        adapter.notifyDataSetChanged();
    }

    private void listTree(Uri dir) {
        Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(
                dir, DocumentsContract.getTreeDocumentId(dir));
        String[] cols = {
                DocumentsContract.Document.COLUMN_DOCUMENT_ID,
                DocumentsContract.Document.COLUMN_DISPLAY_NAME,
                DocumentsContract.Document.COLUMN_MIME_TYPE };
        try (android.database.Cursor c = getContentResolver().query(children, cols, null, null, null)) {
            if (c == null) return;
            while (c.moveToNext()) {
                String id = c.getString(0);
                String name = c.getString(1);
                String mime = c.getString(2);
                if (name == null || name.startsWith(".")) continue;
                boolean isDir = DocumentsContract.Document.MIME_TYPE_DIR.equals(mime);
                if (!matches(name)) continue;
                Uri child = DocumentsContract.buildDocumentUriUsingTree(dir, id);
                entries.add(new Item(name, null, child, isDir));
            }
        } catch (Exception ignored) { }
        entries.sort(new Comparator<Item>() {
            @Override public int compare(Item a, Item b) {
                if (a.dir != b.dir) return a.dir ? -1 : 1;
                return a.name.compareToIgnoreCase(b.name);
            }
        });
    }

    private void pushDir(Uri dir, String name) {
        dirStack.add(dir);
        nameStack.add(name);
        refresh();
    }

    private void goUp() {
        if (currentDir != null) {
            File base = RomStore.romDir(this);
            File parent = currentDir.getParentFile();
            if (parent != null && isInside(parent, base)) { currentDir = parent; refresh(); }
            else { currentDir = base; refresh(); }
            return;
        }
        if (!dirStack.isEmpty()) {
            dirStack.remove(dirStack.size() - 1);
            nameStack.remove(nameStack.size() - 1);
            refresh();
        }
    }

    private static boolean isInside(File f, File base) {
        try {
            return f.getCanonicalPath().startsWith(base.getCanonicalPath());
        } catch (Exception e) {
            return false;
        }
    }

    private boolean matches(String name) {
        if (name == null) return false;
        String n = name.toLowerCase();
        if (mode == MODE_ROM)
            return n.endsWith(".gba") || n.endsWith(".agb") || n.endsWith(".bin")
                || n.endsWith(".zip") || n.endsWith(".7z");
        return n.endsWith(".bin") || n.endsWith(".rom") || n.endsWith(".gba");
    }

    private String displayName(Uri uri) {
        /* The provider's real name is the reliable source of the extension. */
        String name = null;
        String[] cols = { android.provider.OpenableColumns.DISPLAY_NAME };
        try (android.database.Cursor c = getContentResolver().query(uri, cols, null, null, null)) {
            if (c != null && c.moveToFirst()) name = c.getString(0);
        } catch (Exception ignored) { }
        if (name != null && !name.isEmpty()) return name;

        try {
            name = DocumentsContract.getDocumentId(uri);
            int colon = name.lastIndexOf(':');
            if (colon >= 0) name = name.substring(colon + 1);
            int slash = name.lastIndexOf('/');
            if (slash >= 0) name = name.substring(slash + 1);
        } catch (Exception ignored) { }
        if (name == null || name.isEmpty()) name = uri.getLastPathSegment();
        if (name == null || name.isEmpty()) name = "rom.gba";
        return name;
    }

    @Override
    public void onBackPressed() {
        if (currentDir != null || !dirStack.isEmpty()) { goUp(); }
        else { setResult(RESULT_CANCELED); finish(); }
    }
}
