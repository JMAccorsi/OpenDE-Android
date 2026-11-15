package com.opende.deusex;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Color;
import android.util.Log;

import com.google.zxing.BarcodeFormat;
import com.google.zxing.EncodeHintType;
import com.google.zxing.common.BitMatrix;
import com.google.zxing.qrcode.QRCodeWriter;

import org.bouncycastle.jce.provider.BouncyCastleProvider;

import java.io.Closeable;
import java.io.File;
import java.security.Security;
import java.util.EnumMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicReference;

import in.dragonbra.javasteam.depotdownloader.DepotDownloader;
import in.dragonbra.javasteam.depotdownloader.IDownloadListener;
import in.dragonbra.javasteam.depotdownloader.data.AppItem;
import in.dragonbra.javasteam.depotdownloader.data.DownloadItem;
import in.dragonbra.javasteam.enums.EOSType;
import in.dragonbra.javasteam.enums.EResult;
import in.dragonbra.javasteam.protobufs.steamclient.SteammessagesAuthSteamclient.EAuthTokenPlatformType;
import in.dragonbra.javasteam.steam.authentication.AuthPollResult;
import in.dragonbra.javasteam.steam.authentication.AuthSessionDetails;
import in.dragonbra.javasteam.steam.authentication.CredentialsAuthSession;
import in.dragonbra.javasteam.steam.authentication.IAuthenticator;
import in.dragonbra.javasteam.steam.authentication.QrAuthSession;
import in.dragonbra.javasteam.steam.authentication.SteamAuthentication;
import in.dragonbra.javasteam.steam.handlers.steamapps.License;
import in.dragonbra.javasteam.steam.handlers.steamapps.callback.LicenseListCallback;
import in.dragonbra.javasteam.steam.handlers.steamuser.LogOnDetails;
import in.dragonbra.javasteam.steam.handlers.steamuser.SteamUser;
import in.dragonbra.javasteam.steam.handlers.steamuser.callback.LoggedOnCallback;
import in.dragonbra.javasteam.steam.steamclient.SteamClient;
import in.dragonbra.javasteam.steam.steamclient.callbackmgr.CallbackManager;
import in.dragonbra.javasteam.steam.steamclient.callbacks.ConnectedCallback;
import in.dragonbra.javasteam.steam.steamclient.callbacks.DisconnectedCallback;
import in.dragonbra.javasteam.types.JobID;

public final class SteamInstaller {
    public static final int DEUS_EX_GOTY_APP_ID = 6910;

    static {
        try {
            Security.removeProvider("BC");
            Security.addProvider(new BouncyCastleProvider());
        } catch (Throwable ignored) {
        }
    }

    public interface Listener {
        void onStatus(String message);
        void onQrBitmap(Bitmap qrBitmap);
        void onProgress(int permille, String detail);
        CompletableFuture<String> requestSteamGuardCode(boolean isEmail, String emailDomain, boolean previousIncorrect);
    }

    private SteamInstaller() {}

    public static Bitmap generateQrBitmap(String text, int sizePx) {
        try {
            Map<EncodeHintType, Object> hints = new EnumMap<>(EncodeHintType.class);
            hints.put(EncodeHintType.MARGIN, 1);
            BitMatrix matrix = new QRCodeWriter().encode(text, BarcodeFormat.QR_CODE, sizePx, sizePx, hints);
            int w = matrix.getWidth();
            int h = matrix.getHeight();
            int[] pixels = new int[w * h];
            for (int y = 0; y < h; y++) {
                int row = y * w;
                for (int x = 0; x < w; x++) {
                    pixels[row + x] = matrix.get(x, y) ? Color.BLACK : Color.WHITE;
                }
            }
            Bitmap bmp = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888);
            bmp.setPixels(pixels, 0, w, 0, 0, w, h);
            return bmp;
        } catch (Exception e) {
            return null;
        }
    }

    public static String downloadDeusEx(
        Context context,
        boolean useQr,
        String username,
        String password,
        File outDir,
        AtomicBoolean cancelFlag,
        Listener listener
    ) {
        boolean pt = isPt();
        SteamClient steamClient = new SteamClient();
        CallbackManager manager = new CallbackManager(steamClient);

        AtomicBoolean running = new AtomicBoolean(true);
        CountDownLatch connectedLatch = new CountDownLatch(1);
        CountDownLatch loggedOnLatch = new CountDownLatch(1);
        CountDownLatch licenseLatch = new CountDownLatch(1);

        AtomicReference<String> connectError = new AtomicReference<>(null);
        AtomicReference<EResult> logonResult = new AtomicReference<>(null);
        AtomicReference<List<License>> licensesRef = new AtomicReference<>(null);

        Closeable subConnected = manager.subscribe(ConnectedCallback.class, cb -> {
            Log.i("OpenDE_Steam", "ConnectedCallback received");
            connectedLatch.countDown();
        });
        Closeable subDisconnected = manager.subscribe(DisconnectedCallback.class, cb -> {
            Log.i("OpenDE_Steam", "DisconnectedCallback received, userInitiated=" + cb.isUserInitiated());
            if (connectedLatch.getCount() > 0) {
                connectError.compareAndSet(null, pt ? "Desconectado da Steam." : "Disconnected from Steam.");
                connectedLatch.countDown();
            }
        });
        Closeable subLoggedOn = manager.subscribe(LoggedOnCallback.class, cb -> {
            Log.i("OpenDE_Steam", "LoggedOnCallback result=" + cb.getResult());
            logonResult.set(cb.getResult());
            loggedOnLatch.countDown();
        });
        Closeable subLicenses = manager.subscribe(LicenseListCallback.class, cb -> {
            Log.i("OpenDE_Steam", "LicenseListCallback count=" + (cb.getLicenseList() != null ? cb.getLicenseList().size() : 0));
            licensesRef.set(cb.getLicenseList());
            licenseLatch.countDown();
        });

        Thread cbThread = new Thread(() -> {
            while (running.get() && !cancelFlag.get()) {
                try {
                    manager.runWaitCallbacks(300L);
                } catch (Throwable t) {
                    Log.e("OpenDE_Steam", "runWaitCallbacks error", t);
                }
            }
        }, "OpenDE-SteamCallbacks");
        cbThread.setDaemon(true);
        cbThread.start();

        try {
            Log.i("OpenDE_Steam", "Calling steamClient.connect()");
            listener.onStatus(pt ? "Conectando aos servidores da Steam..." : "Connecting to Steam servers...");
            steamClient.connect();

            int connectWaitMs = 0;
            while (!connectedLatch.await(300, TimeUnit.MILLISECONDS)) {
                if (cancelFlag.get()) {
                    return pt ? "Cancelado pelo usuário." : "Cancelled by user.";
                }
                connectWaitMs += 300;
                if (connectWaitMs >= 20000) {
                    return pt ? "Tempo esgotado ao conectar na Steam." : "Timed out connecting to Steam.";
                }
            }
            if (connectError.get() != null) {
                return connectError.get();
            }

            SteamAuthentication auth = new SteamAuthentication(steamClient);
            AuthSessionDetails details = new AuthSessionDetails();
            details.platformType = EAuthTokenPlatformType.k_EAuthTokenPlatformType_SteamClient;
            details.clientOSType = EOSType.Windows10;
            details.deviceFriendlyName = "openDE Android";
            details.persistentSession = false;

            AuthPollResult pollResult;
            if (useQr) {
                listener.onStatus(pt ? "Gerando QR Code Steam..." : "Generating Steam QR Code...");
                QrAuthSession qrSession = auth.beginAuthSessionViaQR(details).get(20, TimeUnit.SECONDS);
                Bitmap bmp = generateQrBitmap(qrSession.getChallengeUrl(), 360);
                if (bmp != null) {
                    listener.onQrBitmap(bmp);
                }
                qrSession.setChallengeUrlChanged(updated -> {
                    Bitmap updatedBmp = generateQrBitmap(updated.getChallengeUrl(), 360);
                    if (updatedBmp != null) {
                        listener.onQrBitmap(updatedBmp);
                    }
                });
                listener.onStatus(pt
                    ? "Escaneie o QR Code no app oficial da Steam no celular e confirme o login."
                    : "Scan the QR Code with the official Steam mobile app and approve login.");
                CompletableFuture<AuthPollResult> pollFuture = qrSession.pollingWaitForResult();
                while (!pollFuture.isDone()) {
                    if (cancelFlag.get()) {
                        pollFuture.cancel(true);
                        return pt ? "Cancelado pelo usuário." : "Cancelled by user.";
                    }
                    Thread.sleep(250);
                }
                pollResult = pollFuture.get();
            } else {
                details.username = username;
                details.password = password;
                details.authenticator = new IAuthenticator() {
                    @Override
                    public CompletableFuture<String> getDeviceCode(boolean previousCodeWasIncorrect) {
                        return listener.requestSteamGuardCode(false, null, previousCodeWasIncorrect);
                    }

                    @Override
                    public CompletableFuture<String> getEmailCode(String email, boolean previousCodeWasIncorrect) {
                        return listener.requestSteamGuardCode(true, email, previousCodeWasIncorrect);
                    }

                    @Override
                    public CompletableFuture<Boolean> acceptDeviceConfirmation() {
                        listener.onStatus(pt
                            ? "Confirme o login no app da Steam no seu celular..."
                            : "Approve the login in your Steam mobile app...");
                        return CompletableFuture.completedFuture(true);
                    }
                };
                listener.onStatus(pt ? "Autenticando na Steam..." : "Authenticating with Steam...");
                CredentialsAuthSession credSession = auth.beginAuthSessionViaCredentials(details).get(25, TimeUnit.SECONDS);
                CompletableFuture<AuthPollResult> pollFuture = credSession.pollingWaitForResult();
                while (!pollFuture.isDone()) {
                    if (cancelFlag.get()) {
                        pollFuture.cancel(true);
                        return pt ? "Cancelado pelo usuário." : "Cancelled by user.";
                    }
                    Thread.sleep(250);
                }
                pollResult = pollFuture.get();
            }

            listener.onStatus(pt ? "Entrando na conta Steam..." : "Logging into Steam account...");
            SteamUser steamUser = steamClient.getHandler(SteamUser.class);
            if (steamUser == null) {
                return pt ? "Falha interna no handler SteamUser." : "Internal SteamUser handler failure.";
            }
            LogOnDetails logOn = new LogOnDetails();
            logOn.setUsername(pollResult.getAccountName());
            logOn.setAccessToken(pollResult.getRefreshToken());
            steamUser.logOn(logOn);

            while (!loggedOnLatch.await(300, TimeUnit.MILLISECONDS)) {
                if (cancelFlag.get()) {
                    return pt ? "Cancelado pelo usuário." : "Cancelled by user.";
                }
            }
            if (logonResult.get() != EResult.OK) {
                return (pt ? "Falha no login Steam: " : "Steam logon failed: ") + logonResult.get();
            }

            listener.onStatus(pt ? "Verificando licença de Deus Ex (6910)..." : "Checking Deus Ex (6910) license...");
            int waitLicenseMs = 0;
            while (!licenseLatch.await(300, TimeUnit.MILLISECONDS)) {
                if (cancelFlag.get()) {
                    return pt ? "Cancelado pelo usuário." : "Cancelled by user.";
                }
                waitLicenseMs += 300;
                if (waitLicenseMs >= 15000) {
                    break;
                }
            }

            List<License> licenses = licensesRef.get();
            if (licenses == null || licenses.isEmpty()) {
                return pt
                    ? "Não foi possível obter a lista de licenças da sua conta Steam."
                    : "Could not retrieve license list from your Steam account.";
            }

            if (!outDir.exists() && !outDir.mkdirs()) {
                return (pt ? "Não foi possível criar pasta: " : "Could not create directory: ") + outDir.getAbsolutePath();
            }

            listener.onStatus(pt ? "Baixando Deus Ex da Steam..." : "Downloading Deus Ex from Steam...");
            AtomicReference<String> dlError = new AtomicReference<>(null);
            try (DepotDownloader downloader = new DepotDownloader(steamClient, licenses, true)) {
                downloader.addListener(new IDownloadListener() {
                    @Override
                    public void onStatusUpdate(String status) {
                        if (status != null && !status.isEmpty()) {
                            listener.onStatus(status);
                        }
                    }

                    @Override
                    public void onChunkCompleted(int depotId, float depotPercentComplete, long compressedBytes, long uncompressedBytes) {
                        int permille = Math.max(0, Math.min(1000, Math.round(depotPercentComplete * 1000f)));
                        listener.onProgress(permille, String.format(Locale.US, "Depot %d - %.1f%%", depotId, depotPercentComplete * 100f));
                    }

                    @Override
                    public void onFileCompleted(int depotId, String fileName, float depotPercentComplete) {
                        int permille = Math.max(0, Math.min(1000, Math.round(depotPercentComplete * 1000f)));
                        listener.onProgress(permille, fileName);
                    }

                    @Override
                    public void onDownloadFailed(DownloadItem item, Throwable error) {
                        String msg = (error != null && error.getMessage() != null) ? error.getMessage() : "Download error";
                        dlError.compareAndSet(null, msg);
                    }
                });

                AppItem appItem = new AppItem(
                    DEUS_EX_GOTY_APP_ID,
                    false,
                    outDir.getAbsolutePath(),
                    "public",
                    null,
                    false,
                    "windows",
                    false,
                    null,
                    false,
                    null,
                    false
                );
                downloader.add(appItem);
                downloader.finishAdding();

                CompletableFuture<Void> completion = downloader.getCompletion();
                while (!completion.isDone()) {
                    if (cancelFlag.get()) {
                        return pt ? "Cancelado pelo usuário." : "Cancelled by user.";
                    }
                    Thread.sleep(300);
                }
                completion.get();
            }

            if (dlError.get() != null) {
                return dlError.get();
            }
            if (!GameLocator.isValidGameRoot(outDir)) {
                return pt
                    ? "Download concluído, mas os arquivos de Deus Ex não foram encontrados em " + outDir.getAbsolutePath()
                    : "Download finished, but Deus Ex files were not found in " + outDir.getAbsolutePath();
            }
            return null;
        } catch (Throwable t) {
            String msg = t.getMessage();
            Throwable cause = t.getCause();
            if ((msg == null || msg.isEmpty()) && cause != null) {
                msg = cause.getMessage();
            }
            if (msg == null || msg.isEmpty()) {
                msg = t.getClass().getSimpleName();
            }
            return (pt ? "Erro Steam: " : "Steam error: ") + msg;
        } finally {
            running.set(false);
            try { subConnected.close(); } catch (Throwable ignored) {}
            try { subDisconnected.close(); } catch (Throwable ignored) {}
            try { subLoggedOn.close(); } catch (Throwable ignored) {}
            try { subLicenses.close(); } catch (Throwable ignored) {}
            try { steamClient.disconnect(); } catch (Throwable ignored) {}
        }
    }

    private static boolean isPt() {
        String lang = Locale.getDefault().getLanguage();
        return "pt".equalsIgnoreCase(lang);
    }
}
