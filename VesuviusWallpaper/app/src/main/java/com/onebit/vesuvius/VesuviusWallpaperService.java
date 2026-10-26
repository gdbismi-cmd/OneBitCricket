package com.onebit.vesuvius;

import android.app.WallpaperManager;
import android.content.Context;
import android.content.SharedPreferences;
import android.content.res.Configuration;
import android.graphics.PixelFormat;
import android.opengl.GLSurfaceView;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.preference.PreferenceManager;
import android.service.wallpaper.WallpaperService;
import android.util.DisplayMetrics;
import android.view.SurfaceHolder;
import android.view.WindowManager;

import java.util.ArrayList;
import java.util.List;

public class VesuviusWallpaperService extends WallpaperService {
    private final List<SceneEngine> engines = new ArrayList<>();

    @Override
    public Engine onCreateEngine() {
        PreferenceManager.setDefaultValues(this, R.xml.prefs, false);
        return new SceneEngine();
    }

    @Override
    public void onConfigurationChanged(Configuration config) {
        super.onConfigurationChanged(config);
        for (SceneEngine e : engines) e.applyQuality();
    }

    class SceneEngine extends Engine implements SharedPreferences.OnSharedPreferenceChangeListener {
        private final Handler handler = new Handler(Looper.getMainLooper());
        private final Settings settings = new Settings();
        private SharedPreferences prefs;
        private WallpaperGLView view;
        private SceneRenderer renderer;
        private boolean visible;
        private long nextFrame;

        private final Runnable tick = new Runnable() {
            @Override
            public void run() {
                if (!visible) return;
                view.requestRender();
                long step = 1000L / Math.max(5, settings.fps);
                long now = SystemClock.uptimeMillis();
                nextFrame = Math.max(nextFrame + step, now + 1);
                handler.postAtTime(this, nextFrame);
            }
        };

        @Override
        public void onCreate(SurfaceHolder holder) {
            super.onCreate(holder);
            engines.add(this);
            prefs = PreferenceManager.getDefaultSharedPreferences(VesuviusWallpaperService.this);
            settings.load(prefs);
            prefs.registerOnSharedPreferenceChangeListener(this);

            holder.setFormat(PixelFormat.RGBX_8888);
            renderer = new SceneRenderer(VesuviusWallpaperService.this, settings);
            view = new WallpaperGLView(VesuviusWallpaperService.this);
            view.setEGLContextClientVersion(2);
            view.setEGLConfigChooser(8, 8, 8, 0, 0, 0);
            view.setPreserveEGLContextOnPause(true);
            view.setRenderer(renderer);
            view.setRenderMode(GLSurfaceView.RENDERMODE_WHEN_DIRTY);
            applyQuality();
        }

        void applyQuality() {
            SurfaceHolder holder = getSurfaceHolder();
            float q = settings.quality;
            if (q >= 0.99f) {
                holder.setSizeFromLayout();
                return;
            }
            DisplayMetrics dm = new DisplayMetrics();
            WindowManager wm = (WindowManager) getSystemService(Context.WINDOW_SERVICE);
            wm.getDefaultDisplay().getRealMetrics(dm);
            holder.setFixedSize(Math.max(64, Math.round(dm.widthPixels * q)),
                    Math.max(64, Math.round(dm.heightPixels * q)));
        }

        @Override
        public void onVisibilityChanged(boolean v) {
            if (v == visible) return;
            visible = v;
            handler.removeCallbacks(tick);
            if (v) {
                view.onResume();
                nextFrame = SystemClock.uptimeMillis();
                handler.post(tick);
            } else {
                view.onPause();
            }
        }

        @Override
        public void onOffsetsChanged(float xOffset, float yOffset, float xStep, float yStep, int xPixels, int yPixels) {
            renderer.setPan(xOffset * 2f - 1f);
        }

        @Override
        public Bundle onCommand(String action, int x, int y, int z, Bundle extras, boolean resultRequested) {
            if (WallpaperManager.COMMAND_TAP.equals(action) && settings.tapEruption) renderer.tap();
            return null;
        }

        @Override
        public void onSharedPreferenceChanged(SharedPreferences p, String key) {
            float oldQuality = settings.quality;
            settings.load(p);
            if (settings.quality != oldQuality) applyQuality();
        }

        @Override
        public void onDestroy() {
            visible = false;
            handler.removeCallbacks(tick);
            prefs.unregisterOnSharedPreferenceChangeListener(this);
            engines.remove(this);
            view.destroy();
            super.onDestroy();
        }

        /** GLSurfaceView that draws into the wallpaper's surface instead of its own window. */
        private class WallpaperGLView extends GLSurfaceView {
            WallpaperGLView(Context context) {
                super(context);
            }

            @Override
            public SurfaceHolder getHolder() {
                return getSurfaceHolder();
            }

            void destroy() {
                super.onDetachedFromWindow();
            }
        }
    }
}
