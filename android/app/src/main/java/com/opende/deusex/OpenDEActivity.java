package com.opende.deusex;

import android.os.Bundle;

import org.libsdl.app.SDLActivity;

import java.io.File;

public class OpenDEActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] {
            "SDL2",
            "main"
        };
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
