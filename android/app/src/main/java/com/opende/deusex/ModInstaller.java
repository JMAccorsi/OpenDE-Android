package com.opende.deusex;

import android.content.Context;
import android.database.Cursor;
import android.net.Uri;
import android.provider.DocumentsContract;
import android.provider.OpenableColumns;

import org.apache.commons.compress.archivers.sevenz.SevenZArchiveEntry;
import org.apache.commons.compress.archivers.sevenz.SevenZFile;
import org.apache.commons.compress.archivers.zip.ZipArchiveEntry;
import org.apache.commons.compress.archivers.zip.ZipArchiveInputStream;

import java.io.BufferedInputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.Arrays;
import java.util.HashSet;
import java.util.Locale;
import java.util.Set;
import java.util.concurrent.atomic.AtomicBoolean;

final class ModInstaller {
    interface ProgressCallback {
        void onProgress(String message, int permille);
    }

    private static final Set<String> MOD_SUBDIRS = new HashSet<>(Arrays.asList(
        "system", "maps", "textures", "music", "sounds"
    ));

    private static final Set<String> KNOWN_MOD_ROOT_NAMES = new HashSet<>(Arrays.asList(
        "gmdxv9", "gmdx", "hdtp", "new vision", "newvision", "tnm2", "tnm", "revision", "deusex", "shifter", "biomod"
    ));

    private ModInstaller() {}

    static ModManager.ModInfo installArchive(
        Context context,
        File gameRoot,
        Uri uri,
        AtomicBoolean cancelFlag,
        ProgressCallback callback
    ) throws IOException {
        String displayName = queryDisplayName(context, uri);
        String lower = displayName.toLowerCase(Locale.ROOT);
        File modsRoot = ModManager.getModsRoot(gameRoot);
        if (modsRoot == null) {
            throw new IOException("Mods directory unavailable.");
        }

        File staging = new File(modsRoot, ".staging_" + System.currentTimeMillis());
        if (!staging.mkdirs()) {
            throw new IOException("Failed to create staging directory.");
        }

        File tempArchive = null;
        try {
            callback.onProgress("Preparando arquivo: " + displayName, 50);
            if (lower.endsWith(".exe")) {
                String err = GogInstaller.extractFromUriOrPath(
                    context,
                    GogInstaller.tryResolvePathFromUri(context, uri),
                    uri,
                    staging,
                    (bytesDone, bytesTotal, filesDone, filesTotal, currentFile) -> {
                        int permille = bytesTotal > 0 ? (int) Math.min(800, 100 + (bytesDone * 700L / bytesTotal)) : 300;
                        callback.onProgress("Extraindo: " + (currentFile != null ? currentFile : displayName), permille);
                        return !cancelFlag.get();
                    }
                );
                if (err != null && !err.isEmpty()) {
                    throw new IOException("Falha ao extrair .exe: " + err);
                }
            } else if (lower.endsWith(".7z")) {
                String directPath = GogInstaller.tryResolvePathFromUri(context, uri);
                File archiveFile;
                if (directPath != null && new File(directPath).canRead()) {
                    archiveFile = new File(directPath);
                } else {
                    tempArchive = new File(staging, "_archive.7z");
                    copyUriToFile(context, uri, tempArchive, cancelFlag, callback);
                    archiveFile = tempArchive;
                }
                File extractTarget = new File(staging, "extracted");
                extractTarget.mkdirs();
                extractSevenZ(archiveFile, extractTarget, cancelFlag, callback, 150, 750);
                if (tempArchive != null && tempArchive.exists()) {
                    tempArchive.delete();
                }
            } else {
                // Default to ZIP
                File extractTarget = new File(staging, "extracted");
                extractTarget.mkdirs();
                extractZipUri(context, uri, extractTarget, cancelFlag, callback);
            }

            if (cancelFlag.get()) {
                throw new IOException("Instalação cancelada.");
            }

            callback.onProgress("Organizando estrutura do mod...", 820);
            return finalizeModFromStaging(modsRoot, staging, displayName, cancelFlag, callback);
        } finally {
            ModManager.deleteRecursive(staging);
        }
    }

    static ModManager.ModInfo installFolder(
        Context context,
        File gameRoot,
        Uri treeUri,
        AtomicBoolean cancelFlag,
        ProgressCallback callback
    ) throws IOException {
        File resolved = GameLocator.resolveTreeUri(context, treeUri);
        File modsRoot = ModManager.getModsRoot(gameRoot);
        if (modsRoot == null) {
            throw new IOException("Mods directory unavailable.");
        }

        File staging = new File(modsRoot, ".staging_" + System.currentTimeMillis());
        if (!staging.mkdirs()) {
            throw new IOException("Failed to create staging directory.");
        }
        String displayName = resolved != null ? resolved.getName() : "CustomMod";
        try {
            File extractTarget = new File(staging, "extracted");
            extractTarget.mkdirs();
            if (resolved != null && resolved.isDirectory()) {
                callback.onProgress("Copiando pasta do mod...", 200);
                copyDirectory(resolved, extractTarget, cancelFlag);
            } else {
                callback.onProgress("Importando arquivos via SAF...", 200);
                String docId = DocumentsContract.getTreeDocumentId(treeUri);
                Uri rootDocUri = DocumentsContract.buildDocumentUriUsingTree(treeUri, docId);
                String rootName = queryDisplayName(context, rootDocUri);
                if (rootName != null && !rootName.isEmpty()) {
                    displayName = rootName;
                }
                copyDocumentTree(context, treeUri, docId, extractTarget, cancelFlag);
            }
            if (cancelFlag.get()) {
                throw new IOException("Instalação cancelada.");
            }
            callback.onProgress("Organizando estrutura do mod...", 820);
            return finalizeModFromStaging(modsRoot, staging, displayName, cancelFlag, callback);
        } finally {
            ModManager.deleteRecursive(staging);
        }
    }

    private static ModManager.ModInfo finalizeModFromStaging(
        File modsRoot,
        File staging,
        String rawDisplayName,
        AtomicBoolean cancelFlag,
        ProgressCallback callback
    ) throws IOException {
        cleanupInstallerJunk(staging);

        File payloadRoot = findModPayloadRoot(staging);
        if (payloadRoot == null) {
            throw new IOException("Pasta System/, Maps/, Textures/, Music/ ou Sounds/ não encontrada no mod.");
        }

        // Check for nested NVTextures.7z (New Vision)
        File nvTex = findFileCaseInsensitive(payloadRoot, "NVTextures.7z");
        if (nvTex != null) {
            callback.onProgress("Extraindo NVTextures.7z...", 850);
            File texDir = GameLocator.findSubdir(payloadRoot, "Textures");
            if (texDir == null) {
                texDir = new File(payloadRoot, "Textures");
                texDir.mkdirs();
            }
            extractSevenZ(nvTex, texDir, cancelFlag, callback, 850, 950);
            nvTex.delete();
        }

        // Clean raw display name
        String cleanName = stripExtension(rawDisplayName);
        String modId = ModManager.sanitizeModId(cleanName);
        File targetDir = new File(modsRoot, modId);
        if (targetDir.exists()) {
            ModManager.deleteRecursive(targetDir);
        }

        if (!payloadRoot.renameTo(targetDir)) {
            targetDir.mkdirs();
            copyDirectory(payloadRoot, targetDir, cancelFlag);
        }

        if (!ModManager.looksLikeModDir(targetDir)) {
            ModManager.deleteRecursive(targetDir);
            throw new IOException("Conteúdo extraído não parece um mod válido de Deus Ex.");
        }

        callback.onProgress("Gerando mod.json...", 980);
        ModManager.writeModManifest(targetDir, modId, cleanName, "");
        return ModManager.readModInfo(targetDir);
    }

    private static File findModPayloadRoot(File root) {
        if (hasAnyModSubdir(root)) {
            return root;
        }
        // BFS up to depth 4 to find directory containing System/Maps/Textures/Music/Sounds
        java.util.ArrayDeque<File> queue = new java.util.ArrayDeque<>();
        java.util.ArrayDeque<Integer> depths = new java.util.ArrayDeque<>();
        queue.add(root);
        depths.add(0);
        while (!queue.isEmpty()) {
            File cur = queue.poll();
            int depth = depths.poll();
            if (hasAnyModSubdir(cur)) {
                return cur;
            }
            if (depth >= 4) {
                continue;
            }
            File[] children = cur.listFiles();
            if (children == null) {
                continue;
            }
            for (File child : children) {
                if (child.isDirectory() && !child.getName().startsWith(".")) {
                    queue.add(child);
                    depths.add(depth + 1);
                }
            }
        }
        // Fallback: check known mod root folder names even if they only contain loose files
        File[] top = root.listFiles();
        if (top != null) {
            for (File child : top) {
                if (child.isDirectory() && KNOWN_MOD_ROOT_NAMES.contains(child.getName().toLowerCase(Locale.ROOT))) {
                    return child;
                }
            }
        }
        return null;
    }

    private static boolean hasAnyModSubdir(File dir) {
        if (dir == null || !dir.isDirectory()) {
            return false;
        }
        File[] children = dir.listFiles();
        if (children == null) {
            return false;
        }
        for (File child : children) {
            if (child.isDirectory() && MOD_SUBDIRS.contains(child.getName().toLowerCase(Locale.ROOT))) {
                return true;
            }
        }
        return false;
    }

    private static void cleanupInstallerJunk(File dir) {
        File[] children = dir.listFiles();
        if (children == null) {
            return;
        }
        for (File child : children) {
            String lower = child.getName().toLowerCase(Locale.ROOT);
            if (child.isDirectory()) {
                if (lower.equals("$pluginsdir") || lower.equals("$temp")) {
                    ModManager.deleteRecursive(child);
                } else {
                    cleanupInstallerJunk(child);
                }
            } else if (lower.startsWith("uninstall") || lower.endsWith(".nsi") || lower.endsWith(".url") || lower.endsWith(".lnk")) {
                child.delete();
            }
        }
    }

    private static void extractZipUri(
        Context context,
        Uri uri,
        File destDir,
        AtomicBoolean cancelFlag,
        ProgressCallback callback
    ) throws IOException {
        String destCanonical = destDir.getCanonicalPath() + File.separator;
        byte[] buf = new byte[32768];
        int count = 0;
        try (InputStream raw = context.getContentResolver().openInputStream(uri);
             BufferedInputStream bis = new BufferedInputStream(raw);
             ZipArchiveInputStream zin = new ZipArchiveInputStream(bis)) {
            ZipArchiveEntry entry;
            while ((entry = zin.getNextEntry()) != null) {
                if (cancelFlag.get()) {
                    throw new IOException("Cancelado.");
                }
                File target = new File(destDir, entry.getName());
                if (!target.getCanonicalPath().startsWith(destCanonical) && !target.getCanonicalPath().equals(destDir.getCanonicalPath())) {
                    throw new IOException("Entrada ZIP inválida: " + entry.getName());
                }
                if (entry.isDirectory()) {
                    target.mkdirs();
                    continue;
                }
                File parent = target.getParentFile();
                if (parent != null && !parent.exists()) {
                    parent.mkdirs();
                }
                try (FileOutputStream out = new FileOutputStream(target)) {
                    int n;
                    while ((n = zin.read(buf)) > 0) {
                        out.write(buf, 0, n);
                    }
                }
                count++;
                if ((count & 15) == 0) {
                    callback.onProgress("Extraindo (" + count + "): " + target.getName(), 350);
                }
            }
        }
    }

    private static void extractSevenZ(
        File archiveFile,
        File destDir,
        AtomicBoolean cancelFlag,
        ProgressCallback callback,
        int startPermille,
        int endPermille
    ) throws IOException {
        String destCanonical = destDir.getCanonicalPath() + File.separator;
        byte[] buf = new byte[32768];
        int count = 0;
        try (SevenZFile sevenZ = SevenZFile.builder().setFile(archiveFile).get()) {
            SevenZArchiveEntry entry;
            while ((entry = sevenZ.getNextEntry()) != null) {
                if (cancelFlag.get()) {
                    throw new IOException("Cancelado.");
                }
                File target = new File(destDir, entry.getName());
                if (!target.getCanonicalPath().startsWith(destCanonical) && !target.getCanonicalPath().equals(destDir.getCanonicalPath())) {
                    throw new IOException("Entrada 7z inválida: " + entry.getName());
                }
                if (entry.isDirectory()) {
                    target.mkdirs();
                    continue;
                }
                File parent = target.getParentFile();
                if (parent != null && !parent.exists()) {
                    parent.mkdirs();
                }
                try (FileOutputStream out = new FileOutputStream(target)) {
                    int n;
                    while ((n = sevenZ.read(buf)) > 0) {
                        out.write(buf, 0, n);
                    }
                }
                count++;
                if ((count & 15) == 0) {
                    int p = Math.min(endPermille, startPermille + Math.min(endPermille - startPermille, count / 2));
                    callback.onProgress("Extraindo 7z (" + count + "): " + target.getName(), p);
                }
            }
        }
    }

    private static void copyUriToFile(
        Context context,
        Uri uri,
        File dest,
        AtomicBoolean cancelFlag,
        ProgressCallback callback
    ) throws IOException {
        byte[] buf = new byte[65536];
        long total = 0;
        try (InputStream in = context.getContentResolver().openInputStream(uri);
             FileOutputStream out = new FileOutputStream(dest)) {
            if (in == null) {
                throw new IOException("Não foi possível abrir o arquivo.");
            }
            int n;
            while ((n = in.read(buf)) > 0) {
                if (cancelFlag.get()) {
                    throw new IOException("Cancelado.");
                }
                out.write(buf, 0, n);
                total += n;
                if ((total & 0xFFFFF) == 0) {
                    callback.onProgress("Copiando arquivo (" + (total / (1024 * 1024)) + " MB)...", 120);
                }
            }
        }
    }

    private static void copyDirectory(File src, File dst, AtomicBoolean cancelFlag) throws IOException {
        if (cancelFlag.get()) {
            throw new IOException("Cancelado.");
        }
        if (src.isDirectory()) {
            if (!dst.exists()) {
                dst.mkdirs();
            }
            File[] children = src.listFiles();
            if (children != null) {
                for (File child : children) {
                    copyDirectory(child, new File(dst, child.getName()), cancelFlag);
                }
            }
        } else {
            try (FileInputStream in = new FileInputStream(src);
                 FileOutputStream out = new FileOutputStream(dst)) {
                byte[] buf = new byte[32768];
                int n;
                while ((n = in.read(buf)) > 0) {
                    out.write(buf, 0, n);
                }
            }
        }
    }

    private static void copyDocumentTree(
        Context context,
        Uri treeUri,
        String parentDocId,
        File destDir,
        AtomicBoolean cancelFlag
    ) throws IOException {
        if (cancelFlag.get()) {
            throw new IOException("Cancelado.");
        }
        Uri childrenUri = DocumentsContract.buildChildDocumentsUriUsingTree(treeUri, parentDocId);
        String[] projection = new String[] {
            DocumentsContract.Document.COLUMN_DOCUMENT_ID,
            DocumentsContract.Document.COLUMN_DISPLAY_NAME,
            DocumentsContract.Document.COLUMN_MIME_TYPE
        };
        try (Cursor cursor = context.getContentResolver().query(childrenUri, projection, null, null, null)) {
            if (cursor == null) {
                return;
            }
            while (cursor.moveToNext()) {
                String docId = cursor.getString(0);
                String name = cursor.getString(1);
                String mime = cursor.getString(2);
                if (DocumentsContract.Document.MIME_TYPE_DIR.equals(mime)) {
                    File sub = new File(destDir, name);
                    sub.mkdirs();
                    copyDocumentTree(context, treeUri, docId, sub, cancelFlag);
                } else {
                    Uri fileUri = DocumentsContract.buildDocumentUriUsingTree(treeUri, docId);
                    File target = new File(destDir, name);
                    try (InputStream in = context.getContentResolver().openInputStream(fileUri);
                         FileOutputStream out = new FileOutputStream(target)) {
                        if (in != null) {
                            byte[] buf = new byte[32768];
                            int n;
                            while ((n = in.read(buf)) > 0) {
                                out.write(buf, 0, n);
                            }
                        }
                    }
                }
            }
        }
    }

    private static File findFileCaseInsensitive(File dir, String name) {
        if (dir == null || !dir.isDirectory()) {
            return null;
        }
        File[] files = dir.listFiles();
        if (files == null) {
            return null;
        }
        for (File f : files) {
            if (f.isFile() && f.getName().equalsIgnoreCase(name)) {
                return f;
            }
        }
        return null;
    }

    private static String stripExtension(String name) {
        if (name == null) {
            return "Mod";
        }
        int dot = name.lastIndexOf('.');
        if (dot > 0) {
            String ext = name.substring(dot).toLowerCase(Locale.ROOT);
            if (ext.equals(".zip") || ext.equals(".7z") || ext.equals(".exe")) {
                return name.substring(0, dot);
            }
        }
        return name;
    }

    private static String queryDisplayName(Context context, Uri uri) {
        if (uri == null) {
            return "mod.zip";
        }
        try (Cursor cursor = context.getContentResolver().query(uri, new String[] { OpenableColumns.DISPLAY_NAME }, null, null, null)) {
            if (cursor != null && cursor.moveToFirst()) {
                String name = cursor.getString(0);
                if (name != null && !name.trim().isEmpty()) {
                    return name.trim();
                }
            }
        } catch (Exception ignored) {
        }
        String last = uri.getLastPathSegment();
        return last != null && !last.trim().isEmpty() ? last.trim() : "mod.zip";
    }
}
