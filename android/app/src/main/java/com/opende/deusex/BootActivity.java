package com.opende.deusex;

import android.Manifest;
import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.graphics.Bitmap;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.provider.Settings;
import android.text.InputType;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.EditText;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.TextView;

import java.io.File;
import java.util.Locale;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.atomic.AtomicBoolean;

public class BootActivity extends Activity {
    private static final int REQ_STORAGE = 1001;
    private static final int REQ_PICK_FOLDER = 1002;
    private static final int REQ_PICK_INSTALLER = 1003;
    private static final int REQ_PICK_MOD_ARCHIVE = 1004;
    private static final int REQ_PICK_MOD_FOLDER = 1005;

    private final ExecutorService executor = Executors.newSingleThreadExecutor();
    private final AtomicBoolean cancelExtract = new AtomicBoolean(false);
    private boolean scanning = false;
    private boolean launched = false;
    private File activeGameRoot;

    private TextView titleView;
    private TextView bodyView;
    private ImageView qrView;
    private DeusExTheme.SegmentedBarView segmentedBar;
    private Button primaryBtn;
    private Button secondaryBtn;
    private Button tertiaryBtn;
    private Button steamBtn;

    private boolean isPt() {
        String lang = Locale.getDefault().getLanguage();
        return lang != null && lang.toLowerCase(Locale.ROOT).startsWith("pt");
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        buildUi();
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (!launched && !scanning) {
            evaluateAndProceed();
        }
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        executor.shutdownNow();
    }

    private void buildUi() {
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setGravity(Gravity.CENTER);
        root.setBackground(new DeusExTheme.BackdropDrawable(this));
        int pad = dp(24);
        root.setPadding(pad, pad, pad, pad);

        LinearLayout card = new LinearLayout(this);
        card.setOrientation(LinearLayout.VERTICAL);
        card.setGravity(Gravity.CENTER_HORIZONTAL);
        card.setBackground(new DeusExTheme.ChamferDrawable(
            Color.parseColor("#E81D2025"),
            Color.parseColor("#E81D2025"),
            Color.parseColor("#7E858D"),
            Color.parseColor("#7E858D"),
            dp(1),
            dp(10)
        ));

        LinearLayout.LayoutParams cardLp = new LinearLayout.LayoutParams(dp(560), LinearLayout.LayoutParams.WRAP_CONTENT);
        root.addView(card, cardLp);

        // Header bar ("openDE - Deus Ex")
        LinearLayout headerBar = new LinearLayout(this);
        headerBar.setOrientation(LinearLayout.HORIZONTAL);
        headerBar.setGravity(Gravity.CENTER_VERTICAL);
        headerBar.setBackground(new DeusExTheme.ChamferDrawable(
            Color.parseColor("#2B3037"),
            Color.parseColor("#2B3037"),
            Color.parseColor("#7E858D"),
            Color.parseColor("#7E858D"),
            dp(1),
            dp(6)
        ));
        headerBar.setPadding(dp(16), dp(10), dp(16), dp(10));
        LinearLayout.LayoutParams hdrLp = new LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT,
            LinearLayout.LayoutParams.WRAP_CONTENT
        );
        card.addView(headerBar, hdrLp);

        titleView = new TextView(this);
        titleView.setTextColor(Color.parseColor("#F0F4F8"));
        titleView.setTextSize(TypedValue.COMPLEX_UNIT_SP, 16);
        titleView.setTypeface(Typeface.DEFAULT_BOLD);
        titleView.setGravity(Gravity.START);
        headerBar.addView(titleView);

        // Inner content container
        LinearLayout inner = new LinearLayout(this);
        inner.setOrientation(LinearLayout.VERTICAL);
        inner.setGravity(Gravity.CENTER_HORIZONTAL);
        inner.setPadding(dp(22), dp(18), dp(22), dp(20));
        card.addView(inner, new LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT,
            LinearLayout.LayoutParams.WRAP_CONTENT
        ));

        // Status readout box + segmented bar
        LinearLayout statusBox = new LinearLayout(this);
        statusBox.setOrientation(LinearLayout.VERTICAL);
        statusBox.setBackground(new DeusExTheme.ChamferDrawable(
            Color.parseColor("#14171B"),
            Color.parseColor("#14171B"),
            Color.parseColor("#4A5058"),
            Color.parseColor("#4A5058"),
            dp(1),
            dp(4)
        ));
        statusBox.setPadding(dp(14), dp(10), dp(14), dp(10));
        LinearLayout.LayoutParams sbLp = new LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT,
            LinearLayout.LayoutParams.WRAP_CONTENT
        );
        sbLp.bottomMargin = dp(16);
        inner.addView(statusBox, sbLp);

        bodyView = new TextView(this);
        bodyView.setTextColor(Color.parseColor("#D2D8DE"));
        bodyView.setTextSize(TypedValue.COMPLEX_UNIT_SP, 14);
        bodyView.setGravity(Gravity.CENTER);
        LinearLayout.LayoutParams bodyLp = new LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT,
            LinearLayout.LayoutParams.WRAP_CONTENT
        );
        bodyLp.bottomMargin = dp(8);
        statusBox.addView(bodyView, bodyLp);

        segmentedBar = new DeusExTheme.SegmentedBarView(this);
        LinearLayout.LayoutParams segLp = new LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT,
            dp(8)
        );
        statusBox.addView(segmentedBar, segLp);

        qrView = new ImageView(this);
        qrView.setVisibility(View.GONE);
        LinearLayout.LayoutParams qrLp = new LinearLayout.LayoutParams(dp(156), dp(156));
        qrLp.bottomMargin = dp(14);
        inner.addView(qrView, qrLp);

        primaryBtn = makeButton("#343A42", "#464E58", "#B8C0C8", "#FFFFFF", true);
        LinearLayout.LayoutParams priLp = new LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT,
            LinearLayout.LayoutParams.WRAP_CONTENT
        );
        priLp.bottomMargin = dp(12);
        inner.addView(primaryBtn, priLp);

        LinearLayout btnRow = new LinearLayout(this);
        btnRow.setOrientation(LinearLayout.HORIZONTAL);
        btnRow.setGravity(Gravity.CENTER);
        inner.addView(btnRow, new LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT,
            LinearLayout.LayoutParams.WRAP_CONTENT
        ));

        steamBtn = makeButton("#22262C", "#323840", "#7E858D", "#C8CFD6", false);
        secondaryBtn = makeButton("#22262C", "#323840", "#7E858D", "#C8CFD6", false);
        tertiaryBtn = makeButton("#22262C", "#323840", "#7E858D", "#C8CFD6", false);

        LinearLayout.LayoutParams secLp1 = new LinearLayout.LayoutParams(
            0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f);
        secLp1.rightMargin = dp(8);
        btnRow.addView(steamBtn, secLp1);

        LinearLayout.LayoutParams secLp2 = new LinearLayout.LayoutParams(
            0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f);
        secLp2.rightMargin = dp(8);
        btnRow.addView(secondaryBtn, secLp2);

        LinearLayout.LayoutParams secLp3 = new LinearLayout.LayoutParams(
            0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f);
        btnRow.addView(tertiaryBtn, secLp3);

        setContentView(root);
    }

    private Button makeButton(String fillHex, String focusFillHex, String strokeHex, String focusStrokeHex, boolean isPrimary) {
        Button btn = new Button(this);
        btn.setAllCaps(false);
        btn.setTextColor(isPrimary ? Color.WHITE : Color.parseColor("#D4DCE3"));
        btn.setTextSize(TypedValue.COMPLEX_UNIT_SP, isPrimary ? 16 : 13);
        btn.setTypeface(isPrimary ? Typeface.DEFAULT_BOLD : Typeface.DEFAULT);
        btn.setPadding(dp(14), dp(isPrimary ? 14 : 10), dp(14), dp(isPrimary ? 14 : 10));
        btn.setSingleLine(true);
        btn.setBackground(new DeusExTheme.ChamferDrawable(
            Color.parseColor(fillHex),
            Color.parseColor(focusFillHex),
            Color.parseColor(strokeHex),
            Color.parseColor(focusStrokeHex),
            dp(isPrimary ? 2 : 1),
            dp(isPrimary ? 7 : 5)
        ));
        btn.setFocusable(true);
        return btn;
    }

    private int dp(int v) {
        return Math.round(TypedValue.applyDimension(
            TypedValue.COMPLEX_UNIT_DIP, v, getResources().getDisplayMetrics()));
    }

    private void evaluateAndProceed() {
        File fastPrivate = GameLocator.findPrivateGameRoot(this);
        if (fastPrivate != null) {
            showLauncherHub(fastPrivate);
            return;
        }

        File cached = GameLocator.getCachedGameRoot(this);
        if (cached != null && GameLocator.hasStoragePermission(this)) {
            showLauncherHub(cached);
            return;
        }

        if (!GameLocator.hasStoragePermission(this)) {
            showPermissionPrompt();
            return;
        }

        startAutoScan();
    }

    private void showPermissionPrompt() {
        boolean pt = isPt();
        titleView.setText("OpenDE - Deus Ex");
        bodyView.setText(pt
            ? "Cole a pasta do Deus Ex ou o instalador GOG (.exe) em qualquer lugar do aparelho.\n\nVocê também pode selecionar o instalador GOG diretamente abaixo."
            : "Paste your Deus Ex folder or GOG installer (.exe) anywhere on your device.\n\nYou can also pick the GOG installer directly below.");
        qrView.setVisibility(View.GONE);
        segmentedBar.setIndeterminateMode(false);
        segmentedBar.setProgressPermille(0);
        primaryBtn.setVisibility(View.VISIBLE);
        primaryBtn.setText(pt ? "Permitir Arquivos" : "Allow File Access");
        primaryBtn.setOnClickListener(v -> requestStoragePermission());
        secondaryBtn.setVisibility(View.VISIBLE);
        secondaryBtn.setText(pt ? "Instalador GOG (.exe)" : "GOG Installer (.exe)");
        secondaryBtn.setOnClickListener(v -> openInstallerPicker());
        tertiaryBtn.setVisibility(View.VISIBLE);
        tertiaryBtn.setText(pt ? "Escolher Pasta" : "Pick Folder");
        tertiaryBtn.setOnClickListener(v -> openFolderPicker());
        steamBtn.setVisibility(View.VISIBLE);
        steamBtn.setText(pt ? "Entrar com Steam" : "Download from Steam");
        steamBtn.setOnClickListener(v -> showSteamAuthChooser());
        primaryBtn.requestFocus();
    }

    private void requestStoragePermission() {
        if (Build.VERSION.SDK_INT >= 30) {
            try {
                Intent intent = new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION);
                intent.setData(Uri.parse("package:" + getPackageName()));
                startActivity(intent);
            } catch (Exception e) {
                try {
                    startActivity(new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION));
                } catch (Exception ignored) {
                }
            }
        } else if (Build.VERSION.SDK_INT >= 23) {
            requestPermissions(new String[] {
                Manifest.permission.READ_EXTERNAL_STORAGE,
                Manifest.permission.WRITE_EXTERNAL_STORAGE
            }, REQ_STORAGE);
        }
    }

    private void startAutoScan() {
        scanning = true;
        boolean pt = isPt();
        titleView.setText(pt ? "Procurando Deus Ex..." : "Searching for Deus Ex...");
        bodyView.setText(pt
            ? "Verificando armazenamento interno e cartão SD.\nControles e tradução já estão incluídos no APK."
            : "Scanning internal storage and SD card.\nController support & localization are already bundled in the APK.");
        qrView.setVisibility(View.GONE);
        segmentedBar.setIndeterminateMode(true);
        primaryBtn.setVisibility(View.GONE);
        secondaryBtn.setVisibility(View.GONE);
        tertiaryBtn.setVisibility(View.GONE);
        steamBtn.setVisibility(View.GONE);

        executor.execute(() -> {
            OverlayInstaller.ensureInstalled(this);
            File found = GameLocator.discoverGameRoot(this);
            File installer = (found == null) ? GameLocator.discoverGogInstaller(this) : null;
            runOnUiThread(() -> {
                scanning = false;
                if (isFinishing() || isDestroyed()) {
                    return;
                }
                if (found != null) {
                    prepareAndLaunch(found);
                } else if (installer != null) {
                    showInstallerFoundPrompt(installer);
                } else {
                    showNotFoundPrompt(null);
                }
            });
        });
    }

    private void showInstallerFoundPrompt(File installer) {
        boolean pt = isPt();
        double mb = installer.length() / (1024.0 * 1024.0);
        titleView.setText(pt ? "Instalador GOG Encontrado!" : "GOG Installer Found!");
        bodyView.setText(String.format(Locale.US,
            pt ? "%s (%.0f MB)\n\nToque em Instalar e Jogar para extrair o Deus Ex automaticamente e iniciar o jogo."
               : "%s (%.0f MB)\n\nTap Install & Play to automatically extract Deus Ex and launch the game.",
            installer.getName(), mb));
        qrView.setVisibility(View.GONE);
        segmentedBar.setIndeterminateMode(false);
        segmentedBar.setProgressPermille(0);
        primaryBtn.setVisibility(View.VISIBLE);
        primaryBtn.setText(pt ? "Instalar e Jogar" : "Install & Play");
        primaryBtn.setOnClickListener(v -> startGogExtraction(installer.getAbsolutePath(), null, installer.getName()));
        secondaryBtn.setVisibility(View.VISIBLE);
        secondaryBtn.setText(pt ? "Outro .exe" : "Pick .exe");
        secondaryBtn.setOnClickListener(v -> openInstallerPicker());
        tertiaryBtn.setVisibility(View.VISIBLE);
        tertiaryBtn.setText(pt ? "Escolher Pasta" : "Pick Folder");
        tertiaryBtn.setOnClickListener(v -> openFolderPicker());
        steamBtn.setVisibility(View.VISIBLE);
        steamBtn.setText(pt ? "Entrar com Steam" : "Download from Steam");
        steamBtn.setOnClickListener(v -> showSteamAuthChooser());
        primaryBtn.requestFocus();
    }

    private void showNotFoundPrompt(String customMessage) {
        boolean pt = isPt();
        titleView.setText(pt ? "Deus Ex não encontrado" : "Deus Ex Not Found");
        if (customMessage != null) {
            bodyView.setText(customMessage);
        } else {
            bodyView.setText(pt
                ? "Selecione o instalador da GOG (.exe), faça login na Steam ou escolha a pasta do Deus Ex já extraída."
                : "Select your GOG installer (.exe), sign in with Steam, or pick an already-extracted Deus Ex folder.");
        }
        qrView.setVisibility(View.GONE);
        segmentedBar.setIndeterminateMode(false);
        segmentedBar.setProgressPermille(0);
        primaryBtn.setVisibility(View.VISIBLE);
        primaryBtn.setText(pt ? "Instalador GOG (.exe)" : "GOG Installer (.exe)");
        primaryBtn.setOnClickListener(v -> openInstallerPicker());
        secondaryBtn.setVisibility(View.VISIBLE);
        secondaryBtn.setText(pt ? "Procurar Novamente" : "Scan Again");
        secondaryBtn.setOnClickListener(v -> startAutoScan());
        tertiaryBtn.setVisibility(View.VISIBLE);
        tertiaryBtn.setText(pt ? "Selecionar Pasta" : "Select Folder");
        tertiaryBtn.setOnClickListener(v -> openFolderPicker());
        steamBtn.setVisibility(View.VISIBLE);
        steamBtn.setText(pt ? "Entrar com Steam" : "Download from Steam");
        steamBtn.setOnClickListener(v -> showSteamAuthChooser());
        primaryBtn.requestFocus();
    }

    private void showSteamAuthChooser() {
        boolean pt = isPt();
        String[] options = pt
            ? new String[] {"QR Code (App Steam no celular)", "Usuário e Senha (+ Steam Guard)"}
            : new String[] {"QR Code (Steam Mobile App)", "Username & Password (+ Steam Guard)"};
        new AlertDialog.Builder(this)
            .setTitle(pt ? "Entrar com Steam" : "Sign in with Steam")
            .setItems(options, (d, which) -> {
                if (which == 0) {
                    startSteamDownload(true, null, null);
                } else {
                    showSteamCredentialsDialog();
                }
            })
            .setNegativeButton(pt ? "Cancelar" : "Cancel", null)
            .show();
    }

    private void showSteamCredentialsDialog() {
        boolean pt = isPt();
        LinearLayout form = new LinearLayout(this);
        form.setOrientation(LinearLayout.VERTICAL);
        form.setPadding(dp(20), dp(12), dp(20), dp(4));

        EditText userEdit = new EditText(this);
        userEdit.setHint(pt ? "Usuário Steam" : "Steam Username");
        userEdit.setSingleLine(true);
        form.addView(userEdit);

        EditText passEdit = new EditText(this);
        passEdit.setHint(pt ? "Senha" : "Password");
        passEdit.setSingleLine(true);
        passEdit.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_PASSWORD);
        form.addView(passEdit);

        new AlertDialog.Builder(this)
            .setTitle(pt ? "Login Steam" : "Steam Login")
            .setView(form)
            .setPositiveButton(pt ? "Entrar" : "Sign In", (d, w) -> {
                String u = userEdit.getText().toString().trim();
                String p = passEdit.getText().toString();
                if (!u.isEmpty() && !p.isEmpty()) {
                    startSteamDownload(false, u, p);
                }
            })
            .setNegativeButton(pt ? "Cancelar" : "Cancel", null)
            .show();
    }

    private CompletableFuture<String> promptSteamGuardCode(boolean isEmail, String emailDomain, boolean previousIncorrect) {
        CompletableFuture<String> future = new CompletableFuture<>();
        runOnUiThread(() -> {
            if (isFinishing() || isDestroyed()) {
                future.complete("");
                return;
            }
            boolean pt = isPt();
            String msg;
            if (isEmail) {
                msg = (previousIncorrect ? (pt ? "Código incorreto.\n" : "Incorrect code.\n") : "")
                    + (pt ? "Digite o código Steam Guard enviado para o seu e-mail" : "Enter the Steam Guard code sent to your email")
                    + (emailDomain != null && !emailDomain.isEmpty() ? " (" + emailDomain + ")" : "") + ":";
            } else {
                msg = (previousIncorrect ? (pt ? "Código incorreto.\n" : "Incorrect code.\n") : "")
                    + (pt ? "Digite o código Steam Guard do seu app:" : "Enter your Steam Guard mobile code:");
            }
            EditText input = new EditText(this);
            input.setSingleLine(true);
            new AlertDialog.Builder(this)
                .setTitle("Steam Guard")
                .setMessage(msg)
                .setView(input)
                .setCancelable(false)
                .setPositiveButton("OK", (d, w) -> future.complete(input.getText().toString().trim()))
                .setNegativeButton(pt ? "Cancelar" : "Cancel", (d, w) -> {
                    cancelExtract.set(true);
                    future.complete("");
                })
                .show();
        });
        return future;
    }

    private void startSteamDownload(boolean useQr, String username, String password) {
        scanning = true;
        cancelExtract.set(false);
        boolean pt = isPt();
        File outDir = GogInstaller.getDefaultExtractDir(this);

        titleView.setText(pt ? "Steam - Deus Ex (6910)" : "Steam - Deus Ex (6910)");
        bodyView.setText(pt ? "Iniciando conexão com a Steam..." : "Connecting to Steam...");
        qrView.setVisibility(View.GONE);
        segmentedBar.setIndeterminateMode(true);
        primaryBtn.setVisibility(View.GONE);
        secondaryBtn.setVisibility(View.VISIBLE);
        secondaryBtn.setText(pt ? "Cancelar" : "Cancel");
        secondaryBtn.setOnClickListener(v -> cancelExtract.set(true));
        tertiaryBtn.setVisibility(View.GONE);
        steamBtn.setVisibility(View.GONE);

        final long[] lastUiMs = {0L};
        executor.execute(() -> {
            OverlayInstaller.ensureInstalled(this);
            String err = SteamInstaller.downloadDeusEx(this, useQr, username, password, outDir, cancelExtract,
                new SteamInstaller.Listener() {
                    @Override
                    public void onStatus(String message) {
                        runOnUiThread(() -> {
                            if (!isFinishing() && !isDestroyed()) {
                                bodyView.setText(message);
                            }
                        });
                    }

                    @Override
                    public void onQrBitmap(Bitmap qrBitmap) {
                        runOnUiThread(() -> {
                            if (!isFinishing() && !isDestroyed()) {
                                segmentedBar.setIndeterminateMode(false);
                                segmentedBar.setProgressPermille(0);
                                qrView.setImageBitmap(qrBitmap);
                                qrView.setVisibility(View.VISIBLE);
                            }
                        });
                    }

                    @Override
                    public void onProgress(int permille, String detail) {
                        long now = System.currentTimeMillis();
                        if (now - lastUiMs[0] >= 100 || permille >= 1000) {
                            lastUiMs[0] = now;
                            runOnUiThread(() -> {
                                if (!isFinishing() && !isDestroyed()) {
                                    qrView.setVisibility(View.GONE);
                                    segmentedBar.setIndeterminateMode(false);
                                    segmentedBar.setProgressPermille(permille);
                                    bodyView.setText(String.format(Locale.US, "%.1f%%\n%s",
                                        permille / 10.0, detail != null ? detail : ""));
                                }
                            });
                        }
                    }

                    @Override
                    public CompletableFuture<String> requestSteamGuardCode(boolean isEmail, String emailDomain, boolean previousIncorrect) {
                        return promptSteamGuardCode(isEmail, emailDomain, previousIncorrect);
                    }
                });

            runOnUiThread(() -> {
                scanning = false;
                if (isFinishing() || isDestroyed()) {
                    return;
                }
                qrView.setVisibility(View.GONE);
                if (err == null) {
                    prepareAndLaunch(outDir);
                } else {
                    showNotFoundPrompt(err);
                }
            });
        });
    }

    private void openInstallerPicker() {
        try {
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            intent.addCategory(Intent.CATEGORY_OPENABLE);
            intent.setType("*/*");
            startActivityForResult(intent, REQ_PICK_INSTALLER);
        } catch (Exception e) {
            boolean pt = isPt();
            showNotFoundPrompt(pt
                ? "Não foi possível abrir o seletor de arquivos."
                : "Could not open file picker.");
        }
    }

    private void startGogExtraction(String filePath, Uri uri, String label) {
        scanning = true;
        cancelExtract.set(false);
        boolean pt = isPt();
        File outDir = GogInstaller.getDefaultExtractDir(this);

        titleView.setText(pt ? "Extraindo Deus Ex..." : "Extracting Deus Ex...");
        bodyView.setText((pt ? "Preparando " : "Preparing ") + label + "...");
        qrView.setVisibility(View.GONE);
        segmentedBar.setIndeterminateMode(false);
        segmentedBar.setProgressPermille(0);
        primaryBtn.setVisibility(View.GONE);
        secondaryBtn.setVisibility(View.VISIBLE);
        secondaryBtn.setText(pt ? "Cancelar" : "Cancel");
        secondaryBtn.setOnClickListener(v -> cancelExtract.set(true));
        tertiaryBtn.setVisibility(View.GONE);
        steamBtn.setVisibility(View.GONE);

        final long[] lastUiMs = {0L};
        executor.execute(() -> {
            OverlayInstaller.ensureInstalled(this);
            String err = GogInstaller.extractFromUriOrPath(this, filePath, uri, outDir,
                (bytesDone, bytesTotal, filesDone, filesTotal, currentFile) -> {
                    long now = System.currentTimeMillis();
                    if (now - lastUiMs[0] >= 80 || filesDone == filesTotal) {
                        lastUiMs[0] = now;
                        int permille = bytesTotal > 0
                            ? (int) Math.min(1000L, (bytesDone * 1000L) / bytesTotal)
                            : (filesTotal > 0 ? (filesDone * 1000) / filesTotal : 0);
                        double doneMb = bytesDone / (1024.0 * 1024.0);
                        double totalMb = bytesTotal / (1024.0 * 1024.0);
                        runOnUiThread(() -> {
                            if (isFinishing() || isDestroyed()) return;
                            segmentedBar.setProgressPermille(permille);
                            bodyView.setText(String.format(Locale.US,
                                "%d%%  (%.1f / %.1f MB)  •  %d/%d arquivos\n%s",
                                permille / 10, doneMb, totalMb, filesDone, filesTotal,
                                currentFile != null ? currentFile : ""));
                        });
                    }
                    return !cancelExtract.get();
                });

            runOnUiThread(() -> {
                scanning = false;
                if (isFinishing() || isDestroyed()) return;
                if (err != null && err.isEmpty() && GameLocator.isValidGameRoot(outDir)) {
                    prepareAndLaunch(outDir);
                } else {
                    String msg = (err != null && !err.isEmpty())
                        ? err
                        : (pt ? "Extração concluída, mas System/DeusEx.u não foi encontrado."
                              : "Extraction finished, but System/DeusEx.u was not found.");
                    showNotFoundPrompt(msg);
                }
            });
        });
    }

    private void openFolderPicker() {
        try {
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
            intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
            startActivityForResult(intent, REQ_PICK_FOLDER);
        } catch (Exception e) {
            boolean pt = isPt();
            showNotFoundPrompt(pt
                ? "Não foi possível abrir o seletor de pastas. Use Procurar Novamente após colar o jogo."
                : "Could not open folder picker. Use Scan Again after copying the game.");
        }
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode == REQ_STORAGE && grantResults.length > 0 && grantResults[0] == PackageManager.PERMISSION_GRANTED) {
            startAutoScan();
        }
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == REQ_PICK_FOLDER && resultCode == RESULT_OK && data != null) {
            Uri uri = data.getData();
            File picked = GameLocator.resolveTreeUri(this, uri);
            if (picked != null) {
                prepareAndLaunch(picked);
            } else {
                boolean pt = isPt();
                showNotFoundPrompt(pt
                    ? "Essa pasta não contém System/DeusEx.u e System/Engine.u. Selecione a pasta raiz do Deus Ex."
                    : "That folder does not contain System/DeusEx.u and System/Engine.u. Please select the Deus Ex root folder.");
            }
        } else if (requestCode == REQ_PICK_INSTALLER && resultCode == RESULT_OK && data != null) {
            Uri uri = data.getData();
            if (uri != null) {
                startGogExtraction(null, uri, "instalador GOG");
            }
        } else if (requestCode == REQ_PICK_MOD_ARCHIVE && resultCode == RESULT_OK && data != null) {
            Uri uri = data.getData();
            if (uri != null && activeGameRoot != null) {
                startModInstall(uri, false);
            }
        } else if (requestCode == REQ_PICK_MOD_FOLDER && resultCode == RESULT_OK && data != null) {
            Uri uri = data.getData();
            if (uri != null && activeGameRoot != null) {
                startModInstall(uri, true);
            }
        }
    }

    private void showLauncherHub(File gameRoot) {
        activeGameRoot = gameRoot;
        GameLocator.saveGameRoot(this, gameRoot);
        OverlayInstaller.ensureInstalled(this);

        boolean pt = isPt();
        java.util.List<ModManager.ModInfo> mods = ModManager.listMods(gameRoot);
        ModManager.ModInfo selected = ModManager.findSelectedMod(this, gameRoot);

        titleView.setText("OpenDE - Deus Ex");
        String profileLabel = selected != null
            ? selected.name + (selected.version.isEmpty() ? "" : " (" + selected.version + ")")
            : (pt ? "Deus Ex (Original / Vanilla)" : "Deus Ex (Original / Vanilla)");
        String modsCountText = mods.isEmpty()
            ? (pt ? "Nenhum mod instalado." : "No mods installed.")
            : (pt ? mods.size() + " mod(s) instalado(s)." : mods.size() + " mod(s) installed.");
        String isolationNote = selected != null
            ? (pt ? "Saves e configs isolados em mods/" + selected.id : "Isolated saves & configs in mods/" + selected.id)
            : (pt ? "Pasta base: " + gameRoot.getAbsolutePath() : "Base folder: " + gameRoot.getAbsolutePath());

        bodyView.setText((pt ? "Perfil ativo: " : "Active profile: ") + profileLabel + "\n" + modsCountText + "\n" + isolationNote);
        qrView.setVisibility(View.GONE);
        segmentedBar.setIndeterminateMode(false);
        segmentedBar.setProgressPermille(1000);

        primaryBtn.setVisibility(View.VISIBLE);
        primaryBtn.setEnabled(true);
        primaryBtn.setText(selected != null
            ? (pt ? "Jogar Mod: " + selected.name : "Play Mod: " + selected.name)
            : (pt ? "Jogar Deus Ex" : "Play Deus Ex"));
        primaryBtn.setOnClickListener(v -> prepareAndLaunch(gameRoot, selected != null ? selected.id : ""));

        steamBtn.setVisibility(View.VISIBLE);
        steamBtn.setEnabled(true);
        steamBtn.setText(pt ? "Selecionar Mod" : "Select Mod");
        steamBtn.setOnClickListener(v -> showModSelectorDialog(gameRoot));

        secondaryBtn.setVisibility(View.VISIBLE);
        secondaryBtn.setEnabled(true);
        secondaryBtn.setText(pt ? "Instalar Mod" : "Install Mod");
        secondaryBtn.setOnClickListener(v -> openModArchivePicker());

        tertiaryBtn.setVisibility(View.VISIBLE);
        tertiaryBtn.setEnabled(true);
        tertiaryBtn.setText(pt ? "Pasta de Mod" : "Mod Folder");
        tertiaryBtn.setOnClickListener(v -> openModFolderPicker());
    }

    private void showModSelectorDialog(File gameRoot) {
        boolean pt = isPt();
        java.util.List<ModManager.ModInfo> mods = ModManager.listMods(gameRoot);
        String currentId = ModManager.getSelectedModId(this);

        String[] labels = new String[mods.size() + 1];
        labels[0] = "Deus Ex (Original / Vanilla)";
        int checked = 0;
        for (int i = 0; i < mods.size(); i++) {
            ModManager.ModInfo mod = mods.get(i);
            labels[i + 1] = mod.name + (mod.hasCustomScripts ? " [Total/Script]" : "");
            if (mod.id.equals(currentId)) {
                checked = i + 1;
            }
        }

        AlertDialog.Builder builder = new AlertDialog.Builder(this)
            .setTitle(pt ? "Selecionar Perfil / Mod" : "Select Profile / Mod")
            .setSingleChoiceItems(labels, checked, (dialog, which) -> {
                String newId = which == 0 ? "" : mods.get(which - 1).id;
                ModManager.setSelectedModId(this, newId);
                dialog.dismiss();
                showLauncherHub(gameRoot);
            })
            .setNegativeButton(pt ? "Fechar" : "Close", null);

        if (!mods.isEmpty()) {
            builder.setNeutralButton(pt ? "Remover Mod..." : "Delete Mod...", (d, w) -> showDeleteModDialog(gameRoot, mods));
        }
        builder.show();
    }

    private void showDeleteModDialog(File gameRoot, java.util.List<ModManager.ModInfo> mods) {
        boolean pt = isPt();
        String[] names = new String[mods.size()];
        for (int i = 0; i < mods.size(); i++) {
            names[i] = mods.get(i).name + " (" + mods.get(i).id + ")";
        }
        new AlertDialog.Builder(this)
            .setTitle(pt ? "Remover qual mod?" : "Delete which mod?")
            .setItems(names, (dialog, which) -> {
                ModManager.ModInfo target = mods.get(which);
                ModManager.deleteMod(this, gameRoot, target);
                showLauncherHub(gameRoot);
            })
            .setNegativeButton(pt ? "Cancelar" : "Cancel", null)
            .show();
    }

    private void openModArchivePicker() {
        try {
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            intent.addCategory(Intent.CATEGORY_OPENABLE);
            intent.setType("*/*");
            startActivityForResult(intent, REQ_PICK_MOD_ARCHIVE);
        } catch (Exception e) {
            boolean pt = isPt();
            bodyView.setText(pt ? "Não foi possível abrir o seletor de arquivos." : "Could not open file picker.");
        }
    }

    private void openModFolderPicker() {
        try {
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
            intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
            startActivityForResult(intent, REQ_PICK_MOD_FOLDER);
        } catch (Exception e) {
            boolean pt = isPt();
            bodyView.setText(pt ? "Não foi possível abrir o seletor de pastas." : "Could not open folder picker.");
        }
    }

    private void startModInstall(Uri uri, boolean isFolder) {
        boolean pt = isPt();
        scanning = true;
        cancelExtract.set(false);
        titleView.setText(pt ? "Instalando Mod..." : "Installing Mod...");
        bodyView.setText(pt ? "Preparando arquivos..." : "Preparing files...");
        segmentedBar.setIndeterminateMode(false);
        segmentedBar.setProgressPermille(50);
        primaryBtn.setVisibility(View.VISIBLE);
        primaryBtn.setEnabled(true);
        primaryBtn.setText(pt ? "Cancelar" : "Cancel");
        primaryBtn.setOnClickListener(v -> cancelExtract.set(true));
        steamBtn.setVisibility(View.GONE);
        secondaryBtn.setVisibility(View.GONE);
        tertiaryBtn.setVisibility(View.GONE);

        executor.execute(() -> {
            try {
                ModManager.ModInfo installed = isFolder
                    ? ModInstaller.installFolder(this, activeGameRoot, uri, cancelExtract,
                        (msg, permille) -> runOnUiThread(() -> {
                            bodyView.setText(msg);
                            segmentedBar.setProgressPermille(permille);
                        }))
                    : ModInstaller.installArchive(this, activeGameRoot, uri, cancelExtract,
                        (msg, permille) -> runOnUiThread(() -> {
                            bodyView.setText(msg);
                            segmentedBar.setProgressPermille(permille);
                        }));
                ModManager.setSelectedModId(this, installed.id);
                runOnUiThread(() -> {
                    scanning = false;
                    showLauncherHub(activeGameRoot);
                });
            } catch (Exception e) {
                runOnUiThread(() -> {
                    scanning = false;
                    showLauncherHub(activeGameRoot);
                    bodyView.setText((pt ? "Erro ao instalar mod: " : "Mod install failed: ") + e.getMessage());
                });
            }
        });
    }

    private void prepareAndLaunch(File gameRoot) {
        String selectedId = ModManager.getSelectedModId(this);
        prepareAndLaunch(gameRoot, selectedId);
    }

    private void prepareAndLaunch(File gameRoot, String modId) {
        if (launched) {
            return;
        }
        launched = true;
        GameLocator.saveGameRoot(this, gameRoot);
        OverlayInstaller.ensureInstalled(this);
        Intent intent = new Intent(this, OpenDEActivity.class);
        if (modId != null && !modId.isEmpty()) {
            intent.putExtra(OpenDEActivity.EXTRA_MOD_ID, modId);
        }
        intent.addFlags(Intent.FLAG_ACTIVITY_NO_ANIMATION);
        startActivity(intent);
        overridePendingTransition(0, 0);
        finish();
    }
}
