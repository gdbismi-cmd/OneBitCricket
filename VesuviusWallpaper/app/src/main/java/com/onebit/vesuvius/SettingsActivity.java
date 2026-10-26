package com.onebit.vesuvius;

import android.app.Activity;
import android.app.WallpaperManager;
import android.content.ActivityNotFoundException;
import android.content.ComponentName;
import android.content.Intent;
import android.content.SharedPreferences;
import android.os.Bundle;
import android.preference.PreferenceFragment;
import android.preference.PreferenceManager;
import android.widget.Toast;

public class SettingsActivity extends Activity {
    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        if (state == null) {
            getFragmentManager().beginTransaction()
                    .replace(android.R.id.content, new Prefs())
                    .commit();
        }
    }

    public static class Prefs extends PreferenceFragment {
        @Override
        public void onCreate(Bundle state) {
            super.onCreate(state);
            build();
        }

        private void build() {
            addPreferencesFromResource(R.xml.prefs);
            findPreference("set_wallpaper").setOnPreferenceClickListener(p -> {
                openPicker();
                return true;
            });
            findPreference("reset").setOnPreferenceClickListener(p -> {
                SharedPreferences sp = PreferenceManager.getDefaultSharedPreferences(getActivity());
                sp.edit().clear().apply();
                PreferenceManager.setDefaultValues(getActivity(), R.xml.prefs, true);
                setPreferenceScreen(null);
                build();
                return true;
            });
        }

        private void openPicker() {
            Activity a = getActivity();
            Intent i = new Intent(WallpaperManager.ACTION_CHANGE_LIVE_WALLPAPER);
            i.putExtra(WallpaperManager.EXTRA_LIVE_WALLPAPER_COMPONENT,
                    new ComponentName(a, VesuviusWallpaperService.class));
            try {
                startActivity(i);
            } catch (ActivityNotFoundException e) {
                try {
                    startActivity(new Intent(WallpaperManager.ACTION_LIVE_WALLPAPER_CHOOSER));
                } catch (ActivityNotFoundException e2) {
                    Toast.makeText(a, R.string.no_live_picker, Toast.LENGTH_LONG).show();
                }
            }
        }
    }
}
