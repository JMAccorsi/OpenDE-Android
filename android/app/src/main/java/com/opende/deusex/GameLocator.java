package com.opende.deusex;

import android.Manifest;
import android.content.Context;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Build;
import android.os.Environment;
import android.provider.DocumentsContract;
import android.util.Log;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Set;

final class GameLocator {
    private static final String TAG = "OpenDE";
    private static final String PREFS_NAME = "opende_prefs";
    private static final String KEY_GAME_ROOT = "game_root";
    private static final String CACHE_FILE = "opende_game_root.txt";
    private static final int MAX_SCAN_DEPTH = 5;

    private static final Set<String> SKIP_DIR_NAMES = new HashSet<>();
    static {
        SKIP_DIR_NAMES.add("dcim");
        SKIP_DIR_NAMES.add("pictures");
        SKIP_DIR_NAMES.add("movies");
        SKIP_DIR_NAMES.add("music");
        SKIP_DIR_NAMES.add("ringtones");
        SKIP_DIR_NAMES.add("notifications");
        SKIP_DIR_NAMES.add("alarms");
        SKIP_DIR_NAMES.add("podcasts");
        SKIP_DIR_NAMES.add("audiobooks");
        SKIP_DIR_NAMES.add("obb");
        SKIP_DIR_NAMES.add("lost+found");
        SKIP_DIR_NAMES.add("system volume information");
        SKIP_DIR_NAMES.add("$recycle.bin");
    }

    private GameLocator() {}

    static boolean hasStoragePermission(Context context) {
        if (Build.VERSION.SDK_INT >= 30) {
            return Environment.isExternalStorageManager();
        }
        if (Build.VERSION.SDK_INT >= 23) {
            return context.checkSelfPermission(Manifest.permission.WRITE_EXTERNAL_STORAGE) == PackageManager.PERMISSION_GRANTED
                || context.checkSelfPermission(Manifest.permission.READ_EXTERNAL_STORAGE) == PackageManager.PERMISSION_GRANTED;
        }
        return true;
    }

    static File findSubdir(File root, String name) {
        if (root == null || !root.isDirectory()) {
            return null;
        }
        File exact = new File(root, name);
        if (exact.isDirectory()) {
            return exact;
        }
        File[] children = root.listFiles();
        if (children == null) {
            return null;
        }
        for (File child : children) {
            if (child.isDirectory() && child.getName().equalsIgnoreCase(name)) {
                return child;
            }
        }
        return null;
    }

    private static boolean hasFile(File dir, String name) {
        if (dir == null || !dir.isDirectory()) {
            return false;
        }
        File exact = new File(dir, name);
        if (exact.isFile()) {
            return true;
        }
        File[] children = dir.listFiles();
        if (children == null) {
            return false;
        }
        for (File child : children) {
            if (child.isFile() && child.getName().equalsIgnoreCase(name)) {
                return true;
            }
        }
        return false;
    }

    static boolean isValidGameRoot(File dir) {
        if (dir == null || !dir.isDirectory()) {
            return false;
        }
        File sys = findSubdir(dir, "System");
        return sys != null && hasFile(sys, "DeusEx.u") && hasFile(sys, "Engine.u");
    }

    private static int scoreGameRoot(File dir) {
        int score = 1;
        if (findSubdir(dir, "Maps") != null) score += 10;
        if (findSubdir(dir, "Textures") != null) score += 5;
        if (findSubdir(dir, "Sounds") != null) score += 5;
        if (findSubdir(dir, "Music") != null) score += 5;
        return score;
    }

    static File getCachedGameRoot(Context context) {
        SharedPreferences prefs = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE);
        String saved = prefs.getString(KEY_GAME_ROOT, null);
        if (saved != null && !saved.isEmpty()) {
            File dir = new File(saved);
            if (isValidGameRoot(dir)) {
                return dir;
            }
        }
        return null;
    }

    static File findPrivateGameRoot(Context context) {
        File ext = context.getExternalFilesDir(null);
        if (ext == null) {
            return null;
        }
        File[] candidates = new File[] {
            new File(ext, "DeusEx"),
            new File(ext, "Deus Ex"),
            ext
        };
        for (File candidate : candidates) {
            if (isValidGameRoot(candidate)) {
                return candidate;
            }
        }
        return null;
    }

    static void saveGameRoot(Context context, File root) {
        if (root == null) {
            return;
        }
        String path = root.getAbsolutePath();
        context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
            .edit()
            .putString(KEY_GAME_ROOT, path)
            .apply();
        writeCacheFile(context.getExternalFilesDir(null), path);
        writeCacheFile(context.getFilesDir(), path);
        Log.i(TAG, "Saved game root: " + path);
    }

    private static void writeCacheFile(File dir, String path) {
        if (dir == null) {
            return;
        }
        if (!dir.isDirectory() && !dir.mkdirs()) {
            return;
        }
        File target = new File(dir, CACHE_FILE);
        try (FileOutputStream out = new FileOutputStream(target)) {
            out.write((path + "\n").getBytes(StandardCharsets.UTF_8));
            out.flush();
        } catch (IOException e) {
            Log.w(TAG, "Failed to write " + target.getAbsolutePath() + ": " + e.getMessage());
        }
    }

    static File discoverGameRoot(Context context) {
        File cached = getCachedGameRoot(context);
        if (cached != null) {
            return cached;
        }

        File priv = findPrivateGameRoot(context);
        if (priv != null) {
            return priv;
        }

        List<File> storageRoots = collectStorageRoots(context);
        for (File root : storageRoots) {
            File[] quickCandidates = new File[] {
                new File(root, "OpenDE"),
                new File(root, "DeusEx"),
                new File(root, "Deus Ex"),
                new File(root, "Deus Ex GOTY"),
                new File(root, "Games/DeusEx"),
                new File(root, "Games/Deus Ex"),
                new File(root, "Games/Deus Ex GOTY"),
                new File(root, "Download/DeusEx"),
                new File(root, "Download/Deus Ex"),
                new File(root, "Download/Deus Ex GOTY"),
                new File(root, "Downloads/DeusEx"),
                new File(root, "Downloads/Deus Ex")
            };
            for (File candidate : quickCandidates) {
                if (isValidGameRoot(candidate)) {
                    return candidate;
                }
            }
        }

        File best = null;
        int bestScore = -1;
        Set<String> visited = new HashSet<>();

        class Node {
            final File dir;
            final int depth;
            Node(File dir, int depth) {
                this.dir = dir;
                this.depth = depth;
            }
        }

        ArrayDeque<Node> queue = new ArrayDeque<>();
        for (File root : storageRoots) {
            if (root != null && root.isDirectory() && root.canRead()) {
                queue.add(new Node(root, 0));
            }
        }

        while (!queue.isEmpty()) {
            Node current = queue.poll();
            String canonical;
            try {
                canonical = current.dir.getCanonicalPath();
            } catch (IOException e) {
                canonical = current.dir.getAbsolutePath();
            }
            if (!visited.add(canonical)) {
                continue;
            }

            if (isValidGameRoot(current.dir)) {
                int score = scoreGameRoot(current.dir);
                if (score > bestScore) {
                    bestScore = score;
                    best = current.dir;
                    if (score >= 26) {
                        return best;
                    }
                }
                continue;
            }

            if (current.depth >= MAX_SCAN_DEPTH) {
                continue;
            }

            File[] children = current.dir.listFiles();
            if (children == null) {
                continue;
            }
            for (File child : children) {
                if (!child.isDirectory() || shouldSkipDir(context, child)) {
                    continue;
                }
                queue.add(new Node(child, current.depth + 1));
            }
        }

        return best;
    }

    private static List<File> collectStorageRoots(Context context) {
        List<File> roots = new ArrayList<>();
        Set<String> seen = new HashSet<>();

        addRoot(roots, seen, Environment.getExternalStorageDirectory());
        addRoot(roots, seen, new File("/storage/emulated/0"));

        File storageDir = new File("/storage");
        File[] volumes = storageDir.listFiles();
        if (volumes != null) {
            for (File vol : volumes) {
                String name = vol.getName();
                if (!name.equals("emulated") && !name.equals("self") && vol.isDirectory()) {
                    addRoot(roots, seen, vol);
                }
            }
        }

        File[] extAppDirs = context.getExternalFilesDirs(null);
        if (extAppDirs != null) {
            for (File appDir : extAppDirs) {
                if (appDir != null) {
                    addRoot(roots, seen, appDir);
                }
            }
        }
        return roots;
    }

    private static void addRoot(List<File> roots, Set<String> seen, File dir) {
        if (dir == null || !dir.isDirectory()) {
            return;
        }
        String key = dir.getAbsolutePath();
        if (seen.add(key)) {
            roots.add(dir);
        }
    }

    private static boolean shouldSkipDir(Context context, File dir) {
        String name = dir.getName().toLowerCase(Locale.ROOT);
        if (name.startsWith(".")) {
            return true;
        }
        if (SKIP_DIR_NAMES.contains(name)) {
            return true;
        }
        String path = dir.getAbsolutePath();
        if (path.endsWith("/Android/obb") || path.endsWith("/Android/data")) {
            // Allow scanning only our own app data directory if needed, skip all other apps' data
            File ownExt = context.getExternalFilesDir(null);
            return ownExt == null || !ownExt.getAbsolutePath().startsWith(path);
        }
        if (path.contains("/Android/data/") && !path.contains("/Android/data/" + context.getPackageName())) {
            return true;
        }
        return name.equals("maps") || name.equals("textures") || name.equals("sounds")
            || name.equals("music") || name.equals("system") || name.equals("save");
    }

    static File resolveTreeUri(Context context, Uri treeUri) {
        if (treeUri == null) {
            return null;
        }
        String docId;
        try {
            docId = DocumentsContract.getTreeDocumentId(treeUri);
        } catch (IllegalArgumentException e) {
            return null;
        }
        if (docId == null || docId.isEmpty()) {
            return null;
        }
        int colon = docId.indexOf(':');
        String volume = colon >= 0 ? docId.substring(0, colon) : docId;
        String rel = colon >= 0 ? docId.substring(colon + 1) : "";

        File base;
        if ("primary".equalsIgnoreCase(volume)) {
            base = Environment.getExternalStorageDirectory();
        } else {
            base = new File("/storage/" + volume);
        }
        File picked = rel.isEmpty() ? base : new File(base, rel);
        if (isValidGameRoot(picked)) {
            return picked;
        }
        // Handle user picking the parent folder containing DeusEx/
        File[] children = picked.listFiles();
        if (children != null) {
            for (File child : children) {
                if (child.isDirectory() && isValidGameRoot(child)) {
                    return child;
                }
            }
        }
        // Handle user picking the System/ folder directly
        if ("system".equalsIgnoreCase(picked.getName()) && isValidGameRoot(picked.getParentFile())) {
            return picked.getParentFile();
        }
        return null;
    }

    static File discoverGogInstaller(Context context) {
        List<File> roots = collectStorageRoots(context);
        List<File> searchDirs = new ArrayList<>();
        for (File root : roots) {
            if (root == null) continue;
            searchDirs.add(new File(root, "Download"));
            searchDirs.add(new File(root, "Downloads"));
            searchDirs.add(new File(root, "OpenDE"));
            searchDirs.add(new File(root, "Games"));
            searchDirs.add(root);
        }
        File best = null;
        long bestSize = 0;
        for (File dir : searchDirs) {
            if (!dir.isDirectory() || !dir.canRead()) continue;
            File[] files = dir.listFiles();
            if (files == null) continue;
            for (File f : files) {
                if (!f.isFile()) continue;
                String lower = f.getName().toLowerCase(Locale.US);
                if (!lower.endsWith(".exe")) continue;
                if (!(lower.contains("deus") || lower.contains("setup_"))) continue;
                long len = f.length();
                if (len < 50L * 1024L * 1024L) continue;
                if (len > bestSize) {
                    bestSize = len;
                    best = f;
                }
            }
        }
        return best;
    }
}
