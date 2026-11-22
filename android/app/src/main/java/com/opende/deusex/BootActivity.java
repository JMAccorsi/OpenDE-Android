package com.opende.deusex;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;

/**
 * Headless trampoline activity used solely when exiting the game to respawn
 * OpenDEActivity (:game) with a fresh native process into DXLauncher.
 */
public class BootActivity extends Activity {
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        Intent intent = new Intent(this, OpenDEActivity.class);
        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_NO_ANIMATION);
        startActivity(intent);
        overridePendingTransition(0, 0);
        finish();
    }
}
