package com.opende.deusex;

import android.app.Activity;
import android.content.Intent;
import android.content.pm.ActivityInfo;
import android.os.Build;
import android.os.Bundle;
import android.view.Display;
import android.view.Surface;

import org.libsdl.app.SDLActivity;

import java.io.File;
import java.util.ArrayList;
import java.util.List;

public class OpenDEActivity extends SDLActivity {
    public static final String EXTRA_MOD_ID = "mod_id";
    public static final String EXTRA_GAME_ROOT = "game_root";
    public static final String EXTRA_LAUNCHER_FONTS = "launcher_fonts";

    private static volatile AndroidBridge sBridge;

    @Override
    protected String[] getLibraries() {
        return new String[] {
            "SDL3",
            "main"
        };
    }

    @Override
    protected String[] getArguments() {
        List<String> args = new ArrayList<>();
        Intent intent = getIntent();
        String gameRoot = intent != null ? intent.getStringExtra(EXTRA_GAME_ROOT) : null;
        if ((gameRoot == null || gameRoot.trim().isEmpty()) && sBridge != null) {
            gameRoot = sBridge.getGameRootPath();
        }
        if (gameRoot == null || gameRoot.trim().isEmpty()) {
            File priv = GameLocator.findPrivateGameRoot(this);
            if (priv != null) {
                gameRoot = priv.getAbsolutePath();
            } else if (GameLocator.hasStoragePermission(this)) {
                File cached = GameLocator.getCachedGameRoot(this);
                if (cached != null) {
                    gameRoot = cached.getAbsolutePath();
                }
            }
        }
        if (gameRoot != null && !gameRoot.trim().isEmpty()) {
            args.add("--game");
            args.add(gameRoot.trim());
        }

        String fontsDir = intent != null ? intent.getStringExtra(EXTRA_LAUNCHER_FONTS) : null;
        if (fontsDir == null || fontsDir.trim().isEmpty()) {
            File ext = getExternalFilesDir(null);
            if (ext != null) {
                fontsDir = new File(ext, "dxlauncher/fonts").getAbsolutePath();
            }
        }
        if (fontsDir != null && !fontsDir.trim().isEmpty()) {
            args.add("--launcher-fonts");
            args.add(fontsDir.trim());
        }

        String modId = intent != null ? intent.getStringExtra(EXTRA_MOD_ID) : null;
        if (modId == null || modId.trim().isEmpty()) {
            modId = ModManager.getSelectedModId(this);
        }
        if (modId != null && !modId.trim().isEmpty()) {
            args.add("--mod");
            args.add(modId.trim());
        }
        return args.toArray(new String[0]);
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        OverlayInstaller.ensureInstalled(this);
        sBridge = new AndroidBridge(this);
        super.onCreate(savedInstanceState);
    }

    @Override
    protected void onResume() {
        super.onResume();
        OverlayInstaller.ensureInstalled(this);
        if (sBridge != null) {
            sBridge.onResume();
        }
    }

    @Override
    protected void onDestroy() {
        if (sBridge != null) {
            sBridge.onDestroy();
            sBridge = null;
        }
        super.onDestroy();
        if (isFinishing()) {
            System.exit(0);
        }
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        if (sBridge != null) {
            sBridge.onRequestPermissionsResult(requestCode, grantResults);
        }
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        if (sBridge != null && sBridge.handleActivityResult(requestCode, resultCode, data)) {
            return;
        }
        super.onActivityResult(requestCode, resultCode, data);
    }

    @Override
    public void setOrientationBis(int w, int h, boolean resizable, String hint) {
        setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE);
    }

    @SuppressWarnings("deprecation")
    public static int getDisplayRotation() {
        Activity activity = getContext() instanceof Activity ? (Activity) getContext() : null;
        if (activity == null) {
            return Surface.ROTATION_90;
        }
        Display display = null;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            display = activity.getDisplay();
        } else if (activity.getWindowManager() != null) {
            display = activity.getWindowManager().getDefaultDisplay();
        }
        return display != null ? display.getRotation() : Surface.ROTATION_90;
    }

    public static void setSelectedMod(String modId) {
        Activity activity = getContext() instanceof Activity ? (Activity) getContext() : null;
        if (activity != null) {
            ModManager.setSelectedModId(activity, modId == null ? "" : modId);
        }
    }

    public static void restartToLauncher() {
        Activity activity = getContext() instanceof Activity ? (Activity) getContext() : null;
        if (activity == null) {
            return;
        }
        Intent intent = new Intent(activity, BootActivity.class);
        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TOP | Intent.FLAG_ACTIVITY_NO_ANIMATION);
        activity.startActivity(intent);
        activity.runOnUiThread(activity::finish);
    }

    // ---- DXLauncher JNI Bridge ----

    public static int bridgeGetSeq() {
        AndroidBridge b = sBridge;
        return b != null ? b.getSeq() : 0;
    }

    public static String bridgeGetSnapshot() {
        AndroidBridge b = sBridge;
        return b != null ? b.getSnapshotString() : "";
    }

    public static byte[] bridgeGetSteamQrRgba(int[] outWh) {
        AndroidBridge b = sBridge;
        return b != null ? b.getSteamQrRgba(outWh) : null;
    }

    public static void bridgeRequestStoragePermission() {
        AndroidBridge b = sBridge;
        if (b != null) b.requestStoragePermission();
    }

    public static void bridgeStartAutoScan() {
        AndroidBridge b = sBridge;
        if (b != null) b.startAutoScan();
    }

    public static void bridgePickGameFolder() {
        AndroidBridge b = sBridge;
        if (b != null) b.pickGameFolder();
    }

    public static void bridgePickGogInstaller() {
        AndroidBridge b = sBridge;
        if (b != null) b.pickGogInstaller();
    }

    public static void bridgeStartGogExtractFound() {
        AndroidBridge b = sBridge;
        if (b != null) b.startGogExtractFound();
    }

    public static void bridgeStartSteamQr() {
        AndroidBridge b = sBridge;
        if (b != null) b.startSteamDownload(true, null, null);
    }

    public static void bridgeStartSteamCredentials(String user, String pass) {
        AndroidBridge b = sBridge;
        if (b != null) b.startSteamDownload(false, user, pass);
    }

    public static void bridgeSubmitSteamGuard(String code) {
        AndroidBridge b = sBridge;
        if (b != null) b.submitSteamGuardCode(code);
    }

    public static void bridgePickModArchive() {
        AndroidBridge b = sBridge;
        if (b != null) b.pickModArchive();
    }

    public static void bridgePickModFolder() {
        AndroidBridge b = sBridge;
        if (b != null) b.pickModFolder();
    }

    public static boolean bridgeDeleteMod(String modId) {
        AndroidBridge b = sBridge;
        return b != null && b.deleteMod(modId);
    }

    public static void bridgeCheckUpdates(boolean manual) {
        AndroidBridge b = sBridge;
        if (b != null) b.checkUpdates(manual);
    }

    public static void bridgeDownloadUpdate() {
        AndroidBridge b = sBridge;
        if (b != null) b.downloadAndInstallUpdate();
    }

    public static void bridgeOpenInstallUnknownPermission() {
        AndroidBridge b = sBridge;
        if (b != null) b.openInstallUnknownPermission();
    }

    public static void bridgeCancelTask() {
        AndroidBridge b = sBridge;
        if (b != null) b.cancelActiveTask();
    }

    public static void bridgeClearError() {
        AndroidBridge b = sBridge;
        if (b != null) b.clearError();
    }

    public static void bridgeConsumeInstalledModId() {
        AndroidBridge b = sBridge;
        if (b != null) b.consumeInstalledModId();
    }
}
