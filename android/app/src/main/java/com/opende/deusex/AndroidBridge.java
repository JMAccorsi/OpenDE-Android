package com.opende.deusex;

import android.Manifest;
import android.app.Activity;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.pm.PackageInstaller;
import android.content.pm.PackageManager;
import android.graphics.Bitmap;
import android.net.Uri;
import android.os.Build;
import android.os.Environment;
import android.provider.Settings;
import android.util.Log;

import java.io.File;
import java.util.List;
import java.util.Locale;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;

final class AndroidBridge {
    private static final String TAG = "OpenDE-Bridge";

    static final int REQ_STORAGE = 2001;
    static final int REQ_PICK_FOLDER = 2002;
    static final int REQ_PICK_GOG_EXE = 2003;
    static final int REQ_PICK_MOD_ARCHIVE = 2004;
    static final int REQ_PICK_MOD_FOLDER = 2005;

    private final Activity activity;
    private final ExecutorService executor = Executors.newSingleThreadExecutor();
    private final AtomicBoolean cancelFlag = new AtomicBoolean(false);
    private final AtomicInteger stateSeq = new AtomicInteger(1);

    private boolean hasStoragePerm = false;
    private boolean gameValid = false;
    private String gameRoot = "";

    private boolean busy = false;
    private String taskKind = "";
    private int permille = -1;
    private String status = "";
    private String detail = "";
    private String foundGogPath = "";
    private String foundGogLabel = "";
    private String lastError = "";

    private int qrSeq = 0;
    private byte[] qrRgba = null;
    private int qrWidth = 0;
    private int qrHeight = 0;

    private boolean guardPending = false;
    private boolean guardIsEmail = false;
    private boolean guardPrevIncorrect = false;
    private String guardDomain = "";
    private CompletableFuture<String> guardFuture = null;

    private String installedModId = "";

    private String appVersion = "";
    private boolean updateAvailable = false;
    private String updateTag = "";
    private long updateSize = 0L;
    private String updateNotes = "";
    private UpdateChecker.Release latestRelease = null;
    private File pendingApk = null;
    private boolean needInstallPerm = false;
    private AtomicBoolean updateDownloadCancel = null;
    private BroadcastReceiver installReceiver = null;
    private boolean autoChecked = false;
    private boolean autoScannedOnStart = false;

    AndroidBridge(Activity activity) {
        this.activity = activity;
        this.appVersion = UpdateChecker.currentVersionName(activity);
        refreshEnvironmentLocked();
        if (!gameValid && hasStoragePerm && !autoScannedOnStart) {
            autoScannedOnStart = true;
            startAutoScan();
        }
        if (UpdateChecker.shouldAutoCheck(activity)) {
            checkUpdates(false);
        }
    }

    private boolean isPt() {
        String lang = Locale.getDefault().getLanguage();
        return lang != null && lang.toLowerCase(Locale.ROOT).startsWith("pt");
    }

    private static String escapeLine(String s) {
        if (s == null || s.isEmpty()) {
            return "";
        }
        return s.replace("\\", "\\\\")
            .replace("\r", "")
            .replace("\n", "\\n");
    }

    private synchronized void bump() {
        stateSeq.incrementAndGet();
    }

    private synchronized void refreshEnvironmentLocked() {
        hasStoragePerm = GameLocator.hasStoragePermission(activity);
        File root = GameLocator.findPrivateGameRoot(activity);
        if (root == null && hasStoragePerm) {
            root = GameLocator.getCachedGameRoot(activity);
        }
        if (root != null && GameLocator.isValidGameRoot(root)) {
            gameValid = true;
            gameRoot = root.getAbsolutePath();
        } else {
            gameValid = false;
            gameRoot = "";
        }
    }

    synchronized String getGameRootPath() {
        return gameValid ? gameRoot : "";
    }

    int getSeq() {
        return stateSeq.get();
    }

    synchronized String getSnapshotString() {
        refreshEnvironmentLocked();
        StringBuilder sb = new StringBuilder(512);
        sb.append("seq=").append(stateSeq.get()).append('\n');
        sb.append("perm=").append(hasStoragePerm ? 1 : 0).append('\n');
        sb.append("game_valid=").append(gameValid ? 1 : 0).append('\n');
        sb.append("game_root=").append(escapeLine(gameRoot)).append('\n');
        sb.append("busy=").append(busy ? 1 : 0).append('\n');
        sb.append("task=").append(escapeLine(taskKind)).append('\n');
        sb.append("permille=").append(permille).append('\n');
        sb.append("status=").append(escapeLine(status)).append('\n');
        sb.append("detail=").append(escapeLine(detail)).append('\n');
        sb.append("gog_path=").append(escapeLine(foundGogPath)).append('\n');
        sb.append("gog_label=").append(escapeLine(foundGogLabel)).append('\n');
        sb.append("error=").append(escapeLine(lastError)).append('\n');
        sb.append("qr_seq=").append(qrSeq).append('\n');
        sb.append("guard=").append(guardPending ? 1 : 0).append('\n');
        sb.append("guard_email=").append(guardIsEmail ? 1 : 0).append('\n');
        sb.append("guard_bad=").append(guardPrevIncorrect ? 1 : 0).append('\n');
        sb.append("guard_domain=").append(escapeLine(guardDomain)).append('\n');
        sb.append("installed_mod=").append(escapeLine(installedModId)).append('\n');
        sb.append("app_version=").append(escapeLine(appVersion)).append('\n');
        sb.append("upd_avail=").append(updateAvailable ? 1 : 0).append('\n');
        sb.append("upd_tag=").append(escapeLine(updateTag)).append('\n');
        sb.append("upd_size=").append(updateSize).append('\n');
        sb.append("upd_notes=").append(escapeLine(updateNotes)).append('\n');
        sb.append("need_install_perm=").append(needInstallPerm ? 1 : 0).append('\n');
        sb.append("pending_apk=").append(pendingApk != null ? 1 : 0).append('\n');
        return sb.toString();
    }

    synchronized byte[] getSteamQrRgba(int[] outWh) {
        if (outWh != null && outWh.length >= 2) {
            outWh[0] = qrWidth;
            outWh[1] = qrHeight;
        }
        return qrRgba;
    }

    synchronized void consumeInstalledModId() {
        installedModId = "";
        bump();
    }

    synchronized void clearError() {
        lastError = "";
        needInstallPerm = false;
        bump();
    }

    void onResume() {
        boolean prevPerm;
        boolean nowPerm;
        boolean nowValid;
        File pending;
        synchronized (this) {
            prevPerm = hasStoragePerm;
            refreshEnvironmentLocked();
            nowPerm = hasStoragePerm;
            nowValid = gameValid;
            pending = pendingApk;
        }
        if (pending != null && UpdateChecker.canInstallPackages(activity)) {
            synchronized (this) {
                pendingApk = null;
                needInstallPerm = false;
                bump();
            }
            startApkInstall(pending);
        }
        if (!prevPerm && nowPerm && !nowValid) {
            startAutoScan();
        } else {
            bump();
        }
    }

    void onDestroy() {
        cancelFlag.set(true);
        if (updateDownloadCancel != null) {
            updateDownloadCancel.set(true);
        }
        synchronized (this) {
            if (guardFuture != null && !guardFuture.isDone()) {
                guardFuture.complete("");
            }
        }
        executor.shutdownNow();
        if (installReceiver != null) {
            try {
                activity.unregisterReceiver(installReceiver);
            } catch (IllegalArgumentException ignored) {
            }
            installReceiver = null;
        }
    }

    void requestStoragePermission() {
        activity.runOnUiThread(() -> {
            if (Build.VERSION.SDK_INT >= 30) {
                try {
                    Intent intent = new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION);
                    intent.setData(Uri.parse("package:" + activity.getPackageName()));
                    activity.startActivity(intent);
                    return;
                } catch (Exception ignored) {
                }
                try {
                    activity.startActivity(new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION));
                } catch (Exception ignored) {
                }
            } else if (Build.VERSION.SDK_INT >= 23) {
                activity.requestPermissions(new String[] {
                    Manifest.permission.READ_EXTERNAL_STORAGE,
                    Manifest.permission.WRITE_EXTERNAL_STORAGE
                }, REQ_STORAGE);
            }
        });
    }

    void onRequestPermissionsResult(int requestCode, int[] grantResults) {
        if (requestCode == REQ_STORAGE && grantResults != null && grantResults.length > 0
            && grantResults[0] == PackageManager.PERMISSION_GRANTED) {
            startAutoScan();
        } else {
            bump();
        }
    }

    void startAutoScan() {
        synchronized (this) {
            if (busy) return;
            busy = true;
            taskKind = "scan";
            permille = -1;
            lastError = "";
            status = isPt() ? "Procurando Deus Ex..." : "Searching for Deus Ex...";
            detail = isPt()
                ? "Verificando armazenamento interno e cartão SD..."
                : "Scanning internal storage and SD card...";
            bump();
        }
        executor.execute(() -> {
            OverlayInstaller.ensureInstalled(activity);
            File found = GameLocator.discoverGameRoot(activity);
            File gog = (found == null) ? GameLocator.discoverGogInstaller(activity) : null;
            synchronized (this) {
                busy = false;
                taskKind = "";
                permille = 0;
                if (found != null && GameLocator.isValidGameRoot(found)) {
                    GameLocator.saveGameRoot(activity, found);
                    gameValid = true;
                    gameRoot = found.getAbsolutePath();
                    foundGogPath = "";
                    foundGogLabel = "";
                    status = isPt() ? "Deus Ex encontrado!" : "Deus Ex found!";
                    detail = gameRoot;
                } else if (gog != null) {
                    foundGogPath = gog.getAbsolutePath();
                    double mb = gog.length() / (1024.0 * 1024.0);
                    foundGogLabel = String.format(Locale.US, "%s (%.0f MB)", gog.getName(), mb);
                    status = isPt() ? "Instalador GOG encontrado!" : "GOG Installer found!";
                    detail = foundGogLabel;
                } else {
                    foundGogPath = "";
                    foundGogLabel = "";
                    status = isPt() ? "Deus Ex não encontrado." : "Deus Ex not found.";
                    detail = isPt()
                        ? "Selecione o instalador GOG (.exe), baixe pela Steam ou escolha a pasta do jogo."
                        : "Select the GOG installer (.exe), download from Steam, or pick the game folder.";
                }
                bump();
            }
        });
    }

    void pickGameFolder() {
        activity.runOnUiThread(() -> {
            try {
                Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
                intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
                activity.startActivityForResult(intent, REQ_PICK_FOLDER);
            } catch (Exception e) {
                synchronized (this) {
                    lastError = isPt() ? "Não foi possível abrir o seletor de pastas." : "Could not open folder picker.";
                    bump();
                }
            }
        });
    }

    void pickGogInstaller() {
        activity.runOnUiThread(() -> {
            try {
                Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
                intent.addCategory(Intent.CATEGORY_OPENABLE);
                intent.setType("*/*");
                activity.startActivityForResult(intent, REQ_PICK_GOG_EXE);
            } catch (Exception e) {
                synchronized (this) {
                    lastError = isPt() ? "Não foi possível abrir o seletor de arquivos." : "Could not open file picker.";
                    bump();
                }
            }
        });
    }

    void startGogExtractFound() {
        String path;
        String label;
        synchronized (this) {
            if (busy || foundGogPath.isEmpty()) return;
            path = foundGogPath;
            label = foundGogLabel.isEmpty() ? new File(path).getName() : foundGogLabel;
        }
        startGogExtraction(path, null, label);
    }

    private void startGogExtraction(String filePath, Uri uri, String label) {
        synchronized (this) {
            if (busy) return;
            busy = true;
            taskKind = "gog";
            permille = 0;
            lastError = "";
            cancelFlag.set(false);
            status = (isPt() ? "Extraindo " : "Extracting ") + label + "...";
            detail = "";
            bump();
        }
        final long[] lastUiMs = { 0L };
        File outDir = GogInstaller.getDefaultExtractDir(activity);
        executor.execute(() -> {
            OverlayInstaller.ensureInstalled(activity);
            String err = GogInstaller.extractFromUriOrPath(activity, filePath, uri, outDir,
                (bytesDone, bytesTotal, filesDone, filesTotal, currentFile) -> {
                    long now = System.currentTimeMillis();
                    if (now - lastUiMs[0] >= 80 || filesDone == filesTotal) {
                        lastUiMs[0] = now;
                        int p = bytesTotal > 0
                            ? (int) Math.min(1000L, (bytesDone * 1000L) / bytesTotal)
                            : (filesTotal > 0 ? (filesDone * 1000) / filesTotal : 0);
                        double doneMb = bytesDone / (1024.0 * 1024.0);
                        double totalMb = bytesTotal / (1024.0 * 1024.0);
                        synchronized (this) {
                            permille = p;
                            status = String.format(Locale.US, "%d%%  (%.1f / %.1f MB)  •  %d/%d",
                                p / 10, doneMb, totalMb, filesDone, filesTotal);
                            detail = currentFile != null ? currentFile : "";
                            bump();
                        }
                    }
                    return !cancelFlag.get();
                });

            synchronized (this) {
                busy = false;
                taskKind = "";
                if (err != null && err.isEmpty() && GameLocator.isValidGameRoot(outDir)) {
                    GameLocator.saveGameRoot(activity, outDir);
                    gameValid = true;
                    gameRoot = outDir.getAbsolutePath();
                    foundGogPath = "";
                    foundGogLabel = "";
                    permille = 1000;
                    status = isPt() ? "Extração concluída!" : "Extraction complete!";
                    detail = gameRoot;
                } else {
                    lastError = (err != null && !err.isEmpty())
                        ? err
                        : (isPt() ? "Extração concluída, mas System/DeusEx.u não foi encontrado."
                                  : "Extraction finished, but System/DeusEx.u was not found.");
                    permille = 0;
                    status = "";
                    detail = "";
                }
                bump();
            }
        });
    }

    void startSteamDownload(boolean useQr, String username, String password) {
        synchronized (this) {
            if (busy) return;
            busy = true;
            taskKind = "steam";
            permille = -1;
            lastError = "";
            qrSeq = 0;
            qrRgba = null;
            qrWidth = 0;
            qrHeight = 0;
            guardPending = false;
            cancelFlag.set(false);
            status = isPt() ? "Conectando à Steam..." : "Connecting to Steam...";
            detail = "";
            bump();
        }
        File outDir = GogInstaller.getDefaultExtractDir(activity);
        final long[] lastUiMs = { 0L };
        executor.execute(() -> {
            OverlayInstaller.ensureInstalled(activity);
            String err = SteamInstaller.downloadDeusEx(activity, useQr, username, password, outDir, cancelFlag,
                new SteamInstaller.Listener() {
                    @Override
                    public void onStatus(String message) {
                        synchronized (AndroidBridge.this) {
                            status = message != null ? message : "";
                            bump();
                        }
                    }

                    @Override
                    public void onQrBitmap(Bitmap qrBitmap) {
                        if (qrBitmap == null) return;
                        int w = qrBitmap.getWidth();
                        int h = qrBitmap.getHeight();
                        int[] px = new int[w * h];
                        qrBitmap.getPixels(px, 0, w, 0, 0, w, h);
                        byte[] rgba = new byte[w * h * 4];
                        for (int i = 0; i < px.length; i++) {
                            int c = px[i];
                            rgba[i * 4]     = (byte) ((c >> 16) & 0xFF);
                            rgba[i * 4 + 1] = (byte) ((c >> 8) & 0xFF);
                            rgba[i * 4 + 2] = (byte) (c & 0xFF);
                            rgba[i * 4 + 3] = (byte) ((c >>> 24) & 0xFF);
                        }
                        synchronized (AndroidBridge.this) {
                            qrRgba = rgba;
                            qrWidth = w;
                            qrHeight = h;
                            qrSeq++;
                            permille = 0;
                            bump();
                        }
                    }

                    @Override
                    public void onProgress(int p, String d) {
                        long now = System.currentTimeMillis();
                        if (now - lastUiMs[0] >= 100 || p >= 1000) {
                            lastUiMs[0] = now;
                            synchronized (AndroidBridge.this) {
                                qrRgba = null;
                                qrWidth = 0;
                                qrHeight = 0;
                                qrSeq = 0;
                                permille = p;
                                status = String.format(Locale.US, "Steam: %.1f%%", p / 10.0);
                                detail = d != null ? d : "";
                                bump();
                            }
                        }
                    }

                    @Override
                    public CompletableFuture<String> requestSteamGuardCode(boolean isEmail, String emailDomain, boolean previousIncorrect) {
                        CompletableFuture<String> future = new CompletableFuture<>();
                        synchronized (AndroidBridge.this) {
                            guardFuture = future;
                            guardIsEmail = isEmail;
                            guardDomain = emailDomain != null ? emailDomain : "";
                            guardPrevIncorrect = previousIncorrect;
                            guardPending = true;
                            bump();
                        }
                        return future;
                    }
                });

            synchronized (this) {
                busy = false;
                taskKind = "";
                qrRgba = null;
                qrWidth = 0;
                qrHeight = 0;
                qrSeq = 0;
                guardPending = false;
                if (err == null && GameLocator.isValidGameRoot(outDir)) {
                    GameLocator.saveGameRoot(activity, outDir);
                    gameValid = true;
                    gameRoot = outDir.getAbsolutePath();
                    permille = 1000;
                    status = isPt() ? "Download da Steam concluído!" : "Steam download complete!";
                    detail = gameRoot;
                } else {
                    lastError = err != null ? err : (isPt() ? "Falha no download da Steam." : "Steam download failed.");
                    permille = 0;
                    status = "";
                    detail = "";
                }
                bump();
            }
        });
    }

    synchronized void submitSteamGuardCode(String code) {
        guardPending = false;
        if (code == null || code.isEmpty()) {
            cancelFlag.set(true);
        }
        if (guardFuture != null) {
            guardFuture.complete(code != null ? code.trim() : "");
            guardFuture = null;
        }
        bump();
    }

    void pickModArchive() {
        activity.runOnUiThread(() -> {
            try {
                Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
                intent.addCategory(Intent.CATEGORY_OPENABLE);
                intent.setType("*/*");
                activity.startActivityForResult(intent, REQ_PICK_MOD_ARCHIVE);
            } catch (Exception e) {
                synchronized (this) {
                    lastError = isPt() ? "Não foi possível abrir o seletor de arquivos." : "Could not open file picker.";
                    bump();
                }
            }
        });
    }

    void pickModFolder() {
        activity.runOnUiThread(() -> {
            try {
                Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
                intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
                activity.startActivityForResult(intent, REQ_PICK_MOD_FOLDER);
            } catch (Exception e) {
                synchronized (this) {
                    lastError = isPt() ? "Não foi possível abrir o seletor de pastas." : "Could not open folder picker.";
                    bump();
                }
            }
        });
    }

    private void startModInstall(Uri uri, boolean isFolder) {
        File rootDir;
        synchronized (this) {
            refreshEnvironmentLocked();
            if (!gameValid || gameRoot.isEmpty() || busy) return;
            rootDir = new File(gameRoot);
            busy = true;
            taskKind = "mod";
            permille = 50;
            lastError = "";
            cancelFlag.set(false);
            status = isPt() ? "Instalando Mod..." : "Installing Mod...";
            detail = "";
            bump();
        }
        executor.execute(() -> {
            try {
                ModInstaller.ProgressCallback cb = (msg, p) -> {
                    synchronized (this) {
                        status = msg != null ? msg : "";
                        permille = p;
                        bump();
                    }
                };
                ModManager.ModInfo installed = isFolder
                    ? ModInstaller.installFolder(activity, rootDir, uri, cancelFlag, cb)
                    : ModInstaller.installArchive(activity, rootDir, uri, cancelFlag, cb);
                ModManager.setSelectedModId(activity, installed.id);
                synchronized (this) {
                    busy = false;
                    taskKind = "";
                    permille = 1000;
                    installedModId = installed.id;
                    status = (isPt() ? "Mod instalado: " : "Mod installed: ") + installed.name;
                    detail = "";
                    bump();
                }
            } catch (Exception e) {
                synchronized (this) {
                    busy = false;
                    taskKind = "";
                    permille = 0;
                    lastError = (isPt() ? "Erro ao instalar mod: " : "Mod install failed: ")
                        + (e.getMessage() != null ? e.getMessage() : e.getClass().getSimpleName());
                    status = "";
                    detail = "";
                    bump();
                }
            }
        });
    }

    boolean deleteMod(String modId) {
        if (modId == null || modId.isEmpty()) {
            return false;
        }
        File rootDir;
        synchronized (this) {
            refreshEnvironmentLocked();
            if (!gameValid || gameRoot.isEmpty()) return false;
            rootDir = new File(gameRoot);
        }
        List<ModManager.ModInfo> mods = ModManager.listMods(rootDir);
        for (ModManager.ModInfo mod : mods) {
            if (mod.id.equalsIgnoreCase(modId)) {
                boolean ok = ModManager.deleteMod(activity, rootDir, mod);
                bump();
                return ok;
            }
        }
        return false;
    }

    void checkUpdates(boolean manual) {
        synchronized (this) {
            if (busy) return;
            if (manual) {
                busy = true;
                taskKind = "update_check";
                permille = -1;
                lastError = "";
                status = isPt() ? "Verificando atualizações no GitHub..." : "Checking GitHub for updates...";
                detail = UpdateChecker.REPO;
                bump();
            }
        }
        UpdateChecker.check(activity, manual, (release, error) -> {
            synchronized (this) {
                if (manual) {
                    busy = false;
                    taskKind = "";
                    permille = 0;
                }
                if (error != null) {
                    if (manual) {
                        lastError = (isPt() ? "Falha ao verificar atualizações: " : "Update check failed: ") + error;
                    }
                } else if (release == null) {
                    updateAvailable = false;
                    latestRelease = null;
                    if (manual) {
                        status = isPt() ? "Você já está na versão mais recente." : "You are already on the latest version.";
                        detail = "v" + appVersion;
                    }
                } else {
                    latestRelease = release;
                    updateAvailable = true;
                    updateTag = release.tag != null ? release.tag : "";
                    updateSize = release.size;
                    updateNotes = sanitizeNotes(release.notes);
                    if (manual) {
                        status = (isPt() ? "Nova versão disponível: " : "New version available: ") + updateTag;
                        detail = "";
                    }
                }
                bump();
            }
        });
    }

    private static String sanitizeNotes(String raw) {
        if (raw == null) return "";
        String cleaned = raw.trim()
            .replace("**", "")
            .replace("`", "")
            .replaceAll("(?m)^#{1,6}\\s*", "");
        if (cleaned.length() > 500) {
            cleaned = cleaned.substring(0, 500).trim() + "...";
        }
        return cleaned;
    }

    void downloadAndInstallUpdate() {
        UpdateChecker.Release rel;
        synchronized (this) {
            if (busy || latestRelease == null) return;
            rel = latestRelease;
            busy = true;
            taskKind = "update_dl";
            permille = -1;
            lastError = "";
            needInstallPerm = false;
            status = (isPt() ? "Baixando " : "Downloading ") + rel.tag + "...";
            detail = "";
            bump();
        }
        updateDownloadCancel = UpdateChecker.download(activity, rel, new UpdateChecker.DownloadCallback() {
            @Override
            public void onProgress(long done, long total) {
                synchronized (AndroidBridge.this) {
                    if (total > 0) {
                        permille = (int) Math.min(1000L, (done * 1000L) / total);
                        status = String.format(Locale.ROOT, "%s %s", isPt() ? "Baixando" : "Downloading", rel.tag);
                        detail = String.format(Locale.ROOT, "%.1f / %.1f MB",
                            done / (1024.0 * 1024.0), total / (1024.0 * 1024.0));
                        bump();
                    }
                }
            }

            @Override
            public void onDone(File apk) {
                synchronized (AndroidBridge.this) {
                    busy = false;
                    taskKind = "";
                    permille = 1000;
                    if (!UpdateChecker.canInstallPackages(activity)) {
                        pendingApk = apk;
                        needInstallPerm = true;
                        status = isPt()
                            ? "Permita instalar apps desta fonte nos Ajustes do Android."
                            : "Allow OpenDE to install unknown apps in Android Settings.";
                        detail = "";
                        bump();
                        return;
                    }
                    pendingApk = null;
                    needInstallPerm = false;
                    status = isPt() ? "Abrindo instalador do Android..." : "Opening Android package installer...";
                    bump();
                }
                startApkInstall(apk);
            }

            @Override
            public void onError(String message) {
                synchronized (AndroidBridge.this) {
                    busy = false;
                    taskKind = "";
                    permille = 0;
                    if (!"cancelled".equals(message)) {
                        lastError = (isPt() ? "Falha no download: " : "Download failed: ") + message;
                    }
                    bump();
                }
            }
        });
    }

    void openInstallUnknownPermission() {
        activity.runOnUiThread(() -> {
            try {
                Intent intent = new Intent(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES,
                    Uri.parse("package:" + activity.getPackageName()));
                activity.startActivity(intent);
            } catch (Exception e) {
                synchronized (this) {
                    lastError = e.getMessage() != null ? e.getMessage() : "Settings unavailable";
                    bump();
                }
            }
        });
    }

    private void startApkInstall(File apk) {
        registerInstallReceiver();
        try {
            UpdateChecker.install(activity, apk);
        } catch (Exception e) {
            synchronized (this) {
                lastError = (isPt() ? "Falha ao instalar APK: " : "APK install failed: ") + e.getMessage();
                bump();
            }
        }
    }

    @SuppressWarnings("deprecation")
    private void registerInstallReceiver() {
        if (installReceiver != null) {
            return;
        }
        installReceiver = new BroadcastReceiver() {
            @Override
            public void onReceive(Context context, Intent intent) {
                int st = intent.getIntExtra(PackageInstaller.EXTRA_STATUS, PackageInstaller.STATUS_FAILURE);
                if (st == PackageInstaller.STATUS_PENDING_USER_ACTION) {
                    Intent confirm = intent.getParcelableExtra(Intent.EXTRA_INTENT);
                    if (confirm != null) {
                        confirm.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                        activity.startActivity(confirm);
                    }
                } else if (st != PackageInstaller.STATUS_SUCCESS) {
                    String msg = intent.getStringExtra(PackageInstaller.EXTRA_STATUS_MESSAGE);
                    synchronized (AndroidBridge.this) {
                        lastError = (isPt() ? "Instalação não concluída: " : "Install incomplete: ")
                            + (msg != null ? msg : String.valueOf(st));
                        bump();
                    }
                }
            }
        };
        IntentFilter filter = new IntentFilter(UpdateChecker.ACTION_INSTALL_STATUS);
        if (Build.VERSION.SDK_INT >= 33) {
            activity.registerReceiver(installReceiver, filter, Context.RECEIVER_NOT_EXPORTED);
        } else {
            activity.registerReceiver(installReceiver, filter);
        }
    }

    void cancelActiveTask() {
        cancelFlag.set(true);
        if (updateDownloadCancel != null) {
            updateDownloadCancel.set(true);
        }
        synchronized (this) {
            if (guardPending && guardFuture != null) {
                guardPending = false;
                guardFuture.complete("");
                guardFuture = null;
            }
            bump();
        }
    }

    boolean handleActivityResult(int requestCode, int resultCode, Intent data) {
        if (requestCode == REQ_PICK_FOLDER) {
            if (resultCode == Activity.RESULT_OK && data != null) {
                File picked = GameLocator.resolveTreeUri(activity, data.getData());
                synchronized (this) {
                    if (picked != null) {
                        GameLocator.saveGameRoot(activity, picked);
                        gameValid = true;
                        gameRoot = picked.getAbsolutePath();
                        foundGogPath = "";
                        foundGogLabel = "";
                        lastError = "";
                        status = isPt() ? "Pasta selecionada!" : "Folder selected!";
                        detail = gameRoot;
                    } else {
                        lastError = isPt()
                            ? "Essa pasta não contém System/DeusEx.u e System/Engine.u."
                            : "Selected folder is missing System/DeusEx.u and System/Engine.u.";
                    }
                    bump();
                }
            }
            return true;
        }
        if (requestCode == REQ_PICK_GOG_EXE) {
            if (resultCode == Activity.RESULT_OK && data != null && data.getData() != null) {
                startGogExtraction(null, data.getData(), "GOG (.exe)");
            }
            return true;
        }
        if (requestCode == REQ_PICK_MOD_ARCHIVE) {
            if (resultCode == Activity.RESULT_OK && data != null && data.getData() != null) {
                startModInstall(data.getData(), false);
            }
            return true;
        }
        if (requestCode == REQ_PICK_MOD_FOLDER) {
            if (resultCode == Activity.RESULT_OK && data != null && data.getData() != null) {
                startModInstall(data.getData(), true);
            }
            return true;
        }
        return false;
    }
}
