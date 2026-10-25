package com.onebit.aquarium;

import android.app.Activity;
import android.app.WallpaperManager;
import android.content.ActivityNotFoundException;
import android.content.ComponentName;
import android.content.Intent;
import android.os.Bundle;
import android.widget.Toast;

/** Opens the system live-wallpaper preview for the aquarium, then gets out of the way. */
public class SetWallpaperActivity extends Activity {
    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        Intent preview = new Intent(WallpaperManager.ACTION_CHANGE_LIVE_WALLPAPER)
                .putExtra(WallpaperManager.EXTRA_LIVE_WALLPAPER_COMPONENT,
                        new ComponentName(this, AquariumWallpaperService.class));
        try {
            startActivity(preview);
        } catch (ActivityNotFoundException e) {
            try {
                startActivity(new Intent(WallpaperManager.ACTION_LIVE_WALLPAPER_CHOOSER));
            } catch (ActivityNotFoundException e2) {
                Toast.makeText(this, "Live wallpapers aren't supported on this device", Toast.LENGTH_LONG).show();
            }
        }
        finish();
    }
}
