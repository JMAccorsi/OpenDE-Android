package com.opende.deusex;

import android.content.Context;
import android.content.SharedPreferences;

import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Comparator;
import java.util.List;
import java.util.Locale;

final class ModManager {
    private static final String PREFS_NAME = "opende_prefs";
    private static final String KEY_SELECTED_MOD = "selected_mod_id";
    private static final String MOD_MANIFEST = "mod.json";

    static final class ModInfo {
        final String id;
        final String name;
        final String version;
        final boolean hasCustomScripts;
        final File directory;

        ModInfo(String id, String name, String version, boolean hasCustomScripts, File directory) {
            this.id = id;
            this.name = name;
            this.version = version;
            this.hasCustomScripts = hasCustomScripts;
            this.directory = directory;
        }
    }

    private ModManager() {}

    static File getModsRoot(File gameRoot) {
        if (gameRoot == null) {
            return null;
        }
        File parent = gameRoot.getParentFile();
        File root = new File(parent != null ? parent : gameRoot, "mods");
        if (!root.exists()) {
            root.mkdirs();
        }
        return root;
    }

    static List<ModInfo> listMods(File gameRoot) {
        List<ModInfo> mods = new ArrayList<>();
        File modsRoot = getModsRoot(gameRoot);
        if (modsRoot == null || !modsRoot.isDirectory()) {
            return mods;
        }
        File[] children = modsRoot.listFiles();
        if (children == null) {
            return mods;
        }
        Arrays.sort(children, Comparator.comparing(f -> f.getName().toLowerCase(Locale.ROOT)));
        for (File dir : children) {
            if (!dir.isDirectory() || dir.getName().startsWith(".")) {
                continue;
            }
            if (!looksLikeModDir(dir)) {
                continue;
            }
            mods.add(readModInfo(dir));
        }
        return mods;
    }

    static boolean looksLikeModDir(File dir) {
        if (new File(dir, MOD_MANIFEST).isFile()) {
            return true;
        }
        String[] knownDirs = { "System", "Maps", "Textures", "Music", "Sounds" };
        for (String sub : knownDirs) {
            if (GameLocator.findSubdir(dir, sub) != null) {
                return true;
            }
        }
        return false;
    }

    static ModInfo readModInfo(File dir) {
        String id = dir.getName();
        String name = id;
        String version = "";
        boolean customScripts = hasModCustomScripts(dir);
        File manifest = new File(dir, MOD_MANIFEST);
        if (manifest.isFile()) {
            try {
                String jsonStr = readTextFile(manifest);
                JSONObject obj = new JSONObject(jsonStr);
                String parsedName = obj.optString("name", "").trim();
                if (!parsedName.isEmpty()) {
                    name = parsedName;
                }
                version = obj.optString("version", "").trim();
                if (obj.has("hasCustomScripts")) {
                    customScripts = obj.optBoolean("hasCustomScripts", customScripts);
                }
            } catch (Exception ignored) {
            }
        }
        return new ModInfo(id, name, version, customScripts, dir);
    }

    static void writeModManifest(File modDir, String id, String displayName, String version) throws IOException {
        JSONObject obj = new JSONObject();
        String jsonText;
        try {
            obj.put("id", id);
            obj.put("name", displayName);
            obj.put("version", version != null ? version : "");
            obj.put("hasCustomScripts", hasModCustomScripts(modDir));
            obj.put("installedAt", System.currentTimeMillis());
            jsonText = obj.toString(2);
        } catch (Exception e) {
            throw new IOException("Failed to build mod.json", e);
        }
        try (FileOutputStream out = new FileOutputStream(new File(modDir, MOD_MANIFEST))) {
            out.write(jsonText.getBytes(StandardCharsets.UTF_8));
        }
    }

    static boolean hasModCustomScripts(File modDir) {
        File sys = GameLocator.findSubdir(modDir, "System");
        if (sys == null) {
            return false;
        }
        File[] files = sys.listFiles();
        if (files == null) {
            return false;
        }
        for (File f : files) {
            if (f.isFile() && f.getName().equalsIgnoreCase("DeusEx.u")) {
                return true;
            }
        }
        return false;
    }

    static String getSelectedModId(Context context) {
        SharedPreferences prefs = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE);
        String val = prefs.getString(KEY_SELECTED_MOD, "");
        return val != null ? val : "";
    }

    static void setSelectedModId(Context context, String modId) {
        SharedPreferences prefs = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE);
        prefs.edit().putString(KEY_SELECTED_MOD, modId != null ? modId : "").apply();
    }

    static ModInfo findSelectedMod(Context context, File gameRoot) {
        String id = getSelectedModId(context);
        if (id.isEmpty()) {
            return null;
        }
        for (ModInfo mod : listMods(gameRoot)) {
            if (mod.id.equals(id)) {
                return mod;
            }
        }
        setSelectedModId(context, "");
        return null;
    }

    static boolean deleteMod(Context context, File gameRoot, ModInfo mod) {
        if (mod == null || mod.directory == null) {
            return false;
        }
        if (mod.id.equals(getSelectedModId(context))) {
            setSelectedModId(context, "");
        }
        boolean ok = deleteRecursive(mod.directory);
        File ext = context.getExternalFilesDir(null);
        if (ext != null) {
            File userModDir = new File(new File(ext, "mods"), mod.id);
            if (userModDir.exists()) {
                deleteRecursive(userModDir);
            }
        }
        return ok;
    }

    static String sanitizeModId(String rawName) {
        String base = rawName == null ? "" : rawName.trim();
        int dot = base.lastIndexOf('.');
        if (dot > 0) {
            String ext = base.substring(dot).toLowerCase(Locale.ROOT);
            if (ext.equals(".zip") || ext.equals(".7z") || ext.equals(".exe")) {
                base = base.substring(0, dot);
            }
        }
        StringBuilder sb = new StringBuilder();
        boolean prevUnderscore = false;
        for (int i = 0; i < base.length(); i++) {
            char c = base.charAt(i);
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
                sb.append(Character.toLowerCase(c));
                prevUnderscore = false;
            } else if ((c == '-' || c == '_' || c == ' ') && !prevUnderscore && sb.length() > 0) {
                sb.append('_');
                prevUnderscore = true;
            }
        }
        String result = sb.toString().replaceAll("_+$", "");
        return result.isEmpty() ? "mod_" + (System.currentTimeMillis() % 100000) : result;
    }

    static boolean deleteRecursive(File target) {
        if (target == null || !target.exists()) {
            return true;
        }
        if (target.isDirectory()) {
            File[] children = target.listFiles();
            if (children != null) {
                for (File child : children) {
                    deleteRecursive(child);
                }
            }
        }
        return target.delete();
    }

    private static String readTextFile(File file) throws IOException {
        try (FileInputStream in = new FileInputStream(file);
             ByteArrayOutputStream out = new ByteArrayOutputStream()) {
            byte[] buf = new byte[4096];
            int n;
            while ((n = in.read(buf)) > 0) {
                out.write(buf, 0, n);
            }
            return new String(out.toByteArray(), StandardCharsets.UTF_8);
        }
    }
}
