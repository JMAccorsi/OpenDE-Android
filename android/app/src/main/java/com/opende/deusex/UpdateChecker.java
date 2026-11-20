package com.opende.deusex;

import android.app.Activity;
import android.app.PendingIntent;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageInstaller;
import android.content.pm.PackageManager;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.security.MessageDigest;
import java.util.Locale;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * Self-update from the project's GitHub Releases page.
 *
 * Flow: check (GitHub REST API) -> download APK (verified by size + SHA-256 when GitHub provides it)
 * -> install through PackageInstaller (system shows its standard confirmation prompt).
 * Updates only install over the existing app when signed with the same key as the installed build.
 */
final class UpdateChecker {
    static final String REPO = "JMAccorsi/OpenDE-Android";
    static final String RELEASES_PAGE = "https://github.com/" + REPO + "/releases";
    static final String ACTION_INSTALL_STATUS = "com.opende.deusex.UPDATE_INSTALL_STATUS";

    private static final String API_URL = "https://api.github.com/repos/" + REPO + "/releases?per_page=15";
    private static final long AUTO_CHECK_INTERVAL_MS = 6L * 60L * 60L * 1000L;
    private static final String PREFS = "opende_update";
    private static final String PREF_LAST_CHECK = "last_check";
    private static final String PREF_SKIPPED_TAG = "skipped_tag";
    private static final Handler MAIN = new Handler(Looper.getMainLooper());

    private UpdateChecker() {
    }

    static final class Release {
        String tag = "";
        String name = "";
        String notes = "";
        String pageUrl = RELEASES_PAGE;
        String apkName = "";
        String apkUrl = "";
        String sha256 = "";
        long size;
    }

    interface CheckCallback {
        /** newer == null && error == null means "already up to date". Always called on the UI thread. */
        void onResult(Release newer, String error);
    }

    interface DownloadCallback {
        void onProgress(long done, long total);

        void onDone(File apk);

        void onError(String message);
    }

    static String currentVersionName(Context context) {
        try {
            String name = context.getPackageManager().getPackageInfo(context.getPackageName(), 0).versionName;
            return name == null ? "0" : name;
        } catch (PackageManager.NameNotFoundException e) {
            return "0";
        }
    }

    /** "v0.4.1-beta" / "0.4.1" -> {0,4,1}. Anything after the numeric part is ignored. */
    static int[] parseVersion(String text) {
        Matcher m = Pattern.compile("(\\d+(?:\\.\\d+)*)").matcher(text == null ? "" : text);
        if (!m.find()) {
            return new int[] {0};
        }
        String[] parts = m.group(1).split("\\.");
        int[] out = new int[parts.length];
        for (int i = 0; i < parts.length; i++) {
            try {
                out[i] = Integer.parseInt(parts[i]);
            } catch (NumberFormatException e) {
                out[i] = 0;
            }
        }
        return out;
    }

    static int compareVersions(int[] a, int[] b) {
        int n = Math.max(a.length, b.length);
        for (int i = 0; i < n; i++) {
            int x = i < a.length ? a[i] : 0;
            int y = i < b.length ? b[i] : 0;
            if (x != y) {
                return x < y ? -1 : 1;
            }
        }
        return 0;
    }

    static boolean shouldAutoCheck(Context context) {
        long last = prefs(context).getLong(PREF_LAST_CHECK, 0L);
        return System.currentTimeMillis() - last >= AUTO_CHECK_INTERVAL_MS;
    }

    static void skipVersion(Context context, String tag) {
        prefs(context).edit().putString(PREF_SKIPPED_TAG, tag).apply();
    }

    private static SharedPreferences prefs(Context context) {
        return context.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
    }

    /** @param manual true when the user asked for the check: ignores "skip this version" and reports errors. */
    static void check(Context context, boolean manual, CheckCallback callback) {
        final Context app = context.getApplicationContext();
        final String current = currentVersionName(app);
        new Thread(() -> {
            Release newer = null;
            String error = null;
            try {
                Release best = fetchBestRelease();
                prefs(app).edit().putLong(PREF_LAST_CHECK, System.currentTimeMillis()).apply();
                if (best != null && compareVersions(parseVersion(best.tag), parseVersion(current)) > 0) {
                    boolean skipped = best.tag.equals(prefs(app).getString(PREF_SKIPPED_TAG, ""));
                    if (manual || !skipped) {
                        newer = best;
                    }
                }
            } catch (Exception e) {
                error = e.getMessage() == null ? e.getClass().getSimpleName() : e.getMessage();
            }
            final Release result = newer;
            final String err = error;
            MAIN.post(() -> callback.onResult(result, err));
        }, "OpenDE-UpdateCheck").start();
    }

    private static Release fetchBestRelease() throws Exception {
        HttpURLConnection conn = open(API_URL);
        try {
            int code = conn.getResponseCode();
            if (code != 200) {
                throw new IOException("GitHub HTTP " + code);
            }
            String body = readAll(conn.getInputStream());
            JSONArray releases = new JSONArray(body);
            Release best = null;
            int[] bestVer = null;
            for (int i = 0; i < releases.length(); i++) {
                JSONObject r = releases.getJSONObject(i);
                if (r.optBoolean("draft", false)) {
                    continue;
                }
                JSONObject apk = pickApkAsset(r.optJSONArray("assets"));
                if (apk == null) {
                    continue;
                }
                String tag = r.optString("tag_name", "");
                int[] ver = parseVersion(tag);
                if (best != null && compareVersions(ver, bestVer) <= 0) {
                    continue;
                }
                Release rel = new Release();
                rel.tag = tag;
                rel.name = r.optString("name", tag);
                rel.notes = r.optString("body", "");
                rel.pageUrl = r.optString("html_url", RELEASES_PAGE);
                rel.apkName = apk.optString("name", "openDE.apk");
                rel.apkUrl = apk.optString("browser_download_url", "");
                rel.size = apk.optLong("size", 0L);
                String digest = apk.optString("digest", "");
                if (digest.startsWith("sha256:")) {
                    rel.sha256 = digest.substring(7).toLowerCase(Locale.ROOT);
                }
                if (rel.apkUrl.isEmpty()) {
                    continue;
                }
                best = rel;
                bestVer = ver;
            }
            return best;
        } finally {
            conn.disconnect();
        }
    }

    private static JSONObject pickApkAsset(JSONArray assets) {
        if (assets == null) {
            return null;
        }
        String abi = Build.SUPPORTED_ABIS.length > 0 ? Build.SUPPORTED_ABIS[0].toLowerCase(Locale.ROOT) : "";
        JSONObject fallback = null;
        for (int i = 0; i < assets.length(); i++) {
            JSONObject a = assets.optJSONObject(i);
            if (a == null) {
                continue;
            }
            String name = a.optString("name", "").toLowerCase(Locale.ROOT);
            if (!name.endsWith(".apk")) {
                continue;
            }
            if (!abi.isEmpty() && name.contains(abi)) {
                return a;
            }
            if (fallback == null) {
                fallback = a;
            }
        }
        return fallback;
    }

    /** Downloads the release APK into the app cache. Returns a handle whose set(true) cancels the download. */
    static AtomicBoolean download(Context context, Release release, DownloadCallback callback) {
        final Context app = context.getApplicationContext();
        final AtomicBoolean cancel = new AtomicBoolean(false);
        new Thread(() -> {
            File dir = new File(app.getCacheDir(), "updates");
            File target = new File(dir, "openDE-" + release.tag + ".apk");
            File part = new File(dir, "openDE-" + release.tag + ".apk.part");
            try {
                deleteOldUpdates(dir);
                if (!dir.isDirectory() && !dir.mkdirs()) {
                    throw new IOException("cannot create " + dir);
                }
                HttpURLConnection conn = open(release.apkUrl);
                try {
                    int code = conn.getResponseCode();
                    if (code != 200) {
                        throw new IOException("HTTP " + code);
                    }
                    long total = conn.getContentLengthLong();
                    if (total <= 0) {
                        total = release.size;
                    }
                    MessageDigest sha = MessageDigest.getInstance("SHA-256");
                    long done = 0;
                    long lastReport = 0;
                    try (InputStream in = conn.getInputStream(); OutputStream out = new FileOutputStream(part)) {
                        byte[] buf = new byte[64 * 1024];
                        int n;
                        while ((n = in.read(buf)) > 0) {
                            if (cancel.get()) {
                                throw new IOException("cancelled");
                            }
                            out.write(buf, 0, n);
                            sha.update(buf, 0, n);
                            done += n;
                            long now = System.currentTimeMillis();
                            if (now - lastReport > 120) {
                                lastReport = now;
                                final long d = done;
                                final long t = total;
                                MAIN.post(() -> callback.onProgress(d, t));
                            }
                        }
                    }
                    if (release.size > 0 && done != release.size) {
                        throw new IOException("incomplete download (" + done + "/" + release.size + ")");
                    }
                    if (!release.sha256.isEmpty() && !release.sha256.equals(hex(sha.digest()))) {
                        throw new IOException("SHA-256 mismatch");
                    }
                } finally {
                    conn.disconnect();
                }
                if (target.exists() && !target.delete()) {
                    throw new IOException("cannot replace " + target);
                }
                if (!part.renameTo(target)) {
                    throw new IOException("cannot finalize " + target);
                }
                MAIN.post(() -> callback.onDone(target));
            } catch (Exception e) {
                //noinspection ResultOfMethodCallIgnored
                part.delete();
                final String msg = cancel.get() ? "cancelled" : (e.getMessage() == null ? e.getClass().getSimpleName() : e.getMessage());
                MAIN.post(() -> callback.onError(msg));
            }
        }, "OpenDE-UpdateDownload").start();
        return cancel;
    }

    private static void deleteOldUpdates(File dir) {
        File[] files = dir.listFiles();
        if (files != null) {
            for (File f : files) {
                //noinspection ResultOfMethodCallIgnored
                f.delete();
            }
        }
    }

    static boolean canInstallPackages(Context context) {
        return Build.VERSION.SDK_INT < Build.VERSION_CODES.O || context.getPackageManager().canRequestPackageInstalls();
    }

    /** Streams the APK into a PackageInstaller session; the result arrives via ACTION_INSTALL_STATUS. */
    static void install(Activity activity, File apk) throws IOException {
        PackageInstaller installer = activity.getPackageManager().getPackageInstaller();
        PackageInstaller.SessionParams params = new PackageInstaller.SessionParams(PackageInstaller.SessionParams.MODE_FULL_INSTALL);
        params.setAppPackageName(activity.getPackageName());
        int sessionId = installer.createSession(params);
        try (PackageInstaller.Session session = installer.openSession(sessionId)) {
            try (InputStream in = new FileInputStream(apk); OutputStream out = session.openWrite("openDE.apk", 0, apk.length())) {
                byte[] buf = new byte[64 * 1024];
                int n;
                while ((n = in.read(buf)) > 0) {
                    out.write(buf, 0, n);
                }
                session.fsync(out);
            }
            Intent status = new Intent(ACTION_INSTALL_STATUS).setPackage(activity.getPackageName());
            int flags = PendingIntent.FLAG_UPDATE_CURRENT | (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S ? PendingIntent.FLAG_MUTABLE : 0);
            PendingIntent pending = PendingIntent.getBroadcast(activity, sessionId, status, flags);
            session.commit(pending.getIntentSender());
        }
    }

    private static HttpURLConnection open(String url) throws IOException {
        HttpURLConnection conn = (HttpURLConnection) new URL(url).openConnection();
        conn.setConnectTimeout(10000);
        conn.setReadTimeout(20000);
        conn.setInstanceFollowRedirects(true);
        conn.setRequestProperty("User-Agent", "OpenDE-Android-Updater");
        conn.setRequestProperty("Accept", "application/vnd.github+json");
        return conn;
    }

    private static String readAll(InputStream in) throws IOException {
        try (InputStream is = in) {
            java.io.ByteArrayOutputStream out = new java.io.ByteArrayOutputStream();
            byte[] buf = new byte[8192];
            int n;
            while ((n = is.read(buf)) > 0) {
                out.write(buf, 0, n);
            }
            return out.toString("UTF-8");
        }
    }

    private static String hex(byte[] bytes) {
        StringBuilder sb = new StringBuilder(bytes.length * 2);
        for (byte b : bytes) {
            sb.append(String.format(Locale.ROOT, "%02x", b));
        }
        return sb.toString();
    }
}
