package com.opende.deusex;

import android.content.Context;
import android.net.Uri;
import android.os.Environment;
import android.os.ParcelFileDescriptor;
import android.provider.DocumentsContract;

import java.io.File;

public final class GogInstaller {

    public interface ProgressListener {
        boolean onProgress(long bytesDone, long bytesTotal, int filesDone, int filesTotal, String currentFile);
    }

    static {
        try {
            System.loadLibrary("SDL2");
        } catch (Throwable ignored) {
        }
        System.loadLibrary("main");
    }

    private GogInstaller() {
    }

    public static native String nativeExtract(
        String installerPath,
        String outputDir,
        ProgressListener listener
    );

    public static File getDefaultExtractDir(Context context) {
        File ext = Environment.getExternalStorageDirectory();
        if (ext != null && ext.canWrite()) {
            File openDeDir = new File(ext, "OpenDE");
            if (openDeDir.exists() || openDeDir.mkdirs()) {
                return openDeDir;
            }
        }
        File appExt = context.getExternalFilesDir(null);
        return new File(appExt != null ? appExt : context.getFilesDir(), "DeusEx");
    }

    public static String tryResolvePathFromUri(Context context, Uri uri) {
        if (uri == null) {
            return null;
        }
        if ("file".equalsIgnoreCase(uri.getScheme())) {
            return uri.getPath();
        }
        if (DocumentsContract.isDocumentUri(context, uri)) {
            String authority = uri.getAuthority();
            String docId = DocumentsContract.getDocumentId(uri);
            if ("com.android.externalstorage.documents".equals(authority) && docId != null) {
                int colon = docId.indexOf(':');
                String type = colon >= 0 ? docId.substring(0, colon) : docId;
                String rel = colon >= 0 ? docId.substring(colon + 1) : "";
                if ("primary".equalsIgnoreCase(type)) {
                    return Environment.getExternalStorageDirectory().getAbsolutePath() + "/" + rel;
                }
                File storageRoot = new File("/storage/" + type + "/" + rel);
                if (storageRoot.exists()) {
                    return storageRoot.getAbsolutePath();
                }
            }
        }
        return null;
    }

    public static String extractFromUriOrPath(
        Context context,
        String filePath,
        Uri uri,
        File outputDir,
        ProgressListener listener
    ) {
        if (filePath != null && new File(filePath).canRead()) {
            return nativeExtract(filePath, outputDir.getAbsolutePath(), listener);
        }
        if (uri != null) {
            String resolved = tryResolvePathFromUri(context, uri);
            if (resolved != null && new File(resolved).canRead()) {
                return nativeExtract(resolved, outputDir.getAbsolutePath(), listener);
            }
            try (ParcelFileDescriptor pfd = context.getContentResolver().openFileDescriptor(uri, "r")) {
                if (pfd == null) {
                    return "Nao foi possivel abrir o arquivo selecionado.";
                }
                String fdPath = "/proc/self/fd/" + pfd.getFd();
                return nativeExtract(fdPath, outputDir.getAbsolutePath(), listener);
            } catch (Exception e) {
                return "Erro ao acessar o instalador: " + e.getMessage();
            }
        }
        return "Caminho do instalador invalido.";
    }
}
