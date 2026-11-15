package com.opende.deusex;

import android.content.Context;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.content.res.AssetFileDescriptor;
import android.content.res.AssetManager;
import android.util.Log;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;

final class OverlayInstaller {
    private static final String TAG = "OpenDE";
    private static final String STAMP_FILE = ".overlay_stamp";

    private OverlayInstaller() {}

    static void ensureInstalled(Context context) {
        File ext = context.getExternalFilesDir(null);
        if (ext == null) {
            return;
        }
        File systemDir = new File(ext, "System");
        if (!systemDir.isDirectory() && !systemDir.mkdirs()) {
            Log.w(TAG, "Failed to create app System dir: " + systemDir.getAbsolutePath());
            return;
        }

        String currentStamp = computeBuildStamp(context);
        File stampFile = new File(systemDir, STAMP_FILE);
        if (isUpToDate(systemDir, stampFile, currentStamp)) {
            return;
        }

        AssetManager assets = context.getAssets();
        boolean ok = true;
        ok &= installAsset(assets, "dxcontroller/DXController.u", new File(systemDir, "DXController.u"));
        ok &= installAsset(assets, "dxcontroller/DeusEx.u", new File(systemDir, "DeusEx.u"));
        ok &= installAssetDir(assets, "ptb", new File(systemDir, "ptb"));

        if (ok) {
            writeStamp(stampFile, currentStamp);
        }
    }

    private static String computeBuildStamp(Context context) {
        try {
            PackageInfo info = context.getPackageManager().getPackageInfo(context.getPackageName(), 0);
            return info.versionCode + ":" + info.lastUpdateTime;
        } catch (PackageManager.NameNotFoundException e) {
            return "1:0";
        }
    }

    private static boolean isUpToDate(File systemDir, File stampFile, String expectedStamp) {
        if (!stampFile.isFile()
            || !new File(systemDir, "DXController.u").isFile()
            || !new File(systemDir, "DeusEx.u").isFile()
            || !new File(systemDir, "ptb/DeusEx.int").isFile()) {
            return false;
        }
        try (BufferedReader reader = new BufferedReader(new InputStreamReader(new FileInputStream(stampFile), StandardCharsets.UTF_8))) {
            String line = reader.readLine();
            return expectedStamp.equals(line != null ? line.trim() : "");
        } catch (IOException e) {
            return false;
        }
    }

    private static void writeStamp(File stampFile, String stamp) {
        try (FileOutputStream out = new FileOutputStream(stampFile)) {
            out.write((stamp + "\n").getBytes(StandardCharsets.UTF_8));
            out.flush();
        } catch (IOException e) {
            Log.w(TAG, "Failed to write overlay stamp: " + e.getMessage());
        }
    }

    private static boolean installAssetDir(AssetManager assets, String assetDir, File targetDir) {
        String[] entries;
        try {
            entries = assets.list(assetDir);
        } catch (IOException e) {
            Log.w(TAG, "Failed to list asset dir " + assetDir + ": " + e.getMessage());
            return false;
        }
        if (entries == null || entries.length == 0) {
            return true;
        }
        if (!targetDir.isDirectory() && !targetDir.mkdirs()) {
            Log.w(TAG, "Failed to create dir " + targetDir.getAbsolutePath());
            return false;
        }
        boolean ok = true;
        for (String name : entries) {
            ok &= installAsset(assets, assetDir + "/" + name, new File(targetDir, name));
        }
        return ok;
    }

    private static boolean installAsset(AssetManager assets, String assetPath, File target) {
        long assetLength = -1;
        try (AssetFileDescriptor afd = assets.openFd(assetPath)) {
            assetLength = afd.getLength();
        } catch (IOException ignored) {
        }

        if (assetLength > 0 && target.isFile() && target.length() == assetLength) {
            return true;
        }

        File tmp = new File(target.getParentFile(), target.getName() + ".tmp");
        try (InputStream in = assets.open(assetPath); FileOutputStream out = new FileOutputStream(tmp)) {
            byte[] buf = new byte[65536];
            int n;
            while ((n = in.read(buf)) > 0) {
                out.write(buf, 0, n);
            }
            out.flush();
        } catch (IOException | SecurityException e) {
            Log.w(TAG, "Failed to extract " + assetPath + ": " + e.getMessage());
            tmp.delete();
            return false;
        }

        if (target.exists() && !target.delete()) {
            Log.w(TAG, "Failed to replace " + target.getAbsolutePath());
        }
        if (!tmp.renameTo(target)) {
            Log.w(TAG, "Failed to finalize " + target.getAbsolutePath());
            tmp.delete();
            return false;
        }
        return true;
    }
}
