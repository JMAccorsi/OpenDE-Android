package com.opende.deusex;

import android.app.Activity;
import android.content.pm.ActivityInfo;
import android.os.Build;
import android.os.Bundle;
import android.view.Display;
import android.view.Surface;

import org.libsdl.app.SDLActivity;

import java.io.File;

public class OpenDEActivity extends SDLActivity {
    public static final String EXTRA_MOD_ID = "mod_id";

    @Override
    protected String[] getLibraries() {
        return new String[] {
            "SDL3",
            "main"
        };
    }

    @Override
    protected String[] getArguments() {
        String modId = getIntent() != null ? getIntent().getStringExtra(EXTRA_MOD_ID) : null;
        if (modId != null && !modId.trim().isEmpty()) {
            return new String[] { "--mod", modId.trim() };
        }
        return new String[0];
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        OverlayInstaller.ensureInstalled(this);
        if (GameLocator.getCachedGameRoot(this) == null && GameLocator.hasStoragePermission(this)) {
            File found = GameLocator.discoverGameRoot(this);
            if (found != null) {
                GameLocator.saveGameRoot(this, found);
            }
        }
        super.onCreate(savedInstanceState);
    }

    @Override
    protected void onResume() {
        super.onResume();
        OverlayInstaller.ensureInstalled(this);
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
}
