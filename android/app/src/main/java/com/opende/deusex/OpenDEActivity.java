package com.opende.deusex;

import android.os.Bundle;

import org.libsdl.app.SDLActivity;

import java.io.File;

public class OpenDEActivity extends SDLActivity {
    public static final String EXTRA_MOD_ID = "mod_id";

    @Override
    protected String[] getLibraries() {
        return new String[] {
            "SDL2",
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
}
