package com.onebit.aquarium;

import android.app.WallpaperManager;
import android.graphics.Canvas;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.service.wallpaper.WallpaperService;
import android.view.SurfaceHolder;

public class AquariumWallpaperService extends WallpaperService {
    private static final long FRAME_MS = 33;

    @Override
    public Engine onCreateEngine() {
        return new AquariumEngine();
    }

    private class AquariumEngine extends Engine {
        private final Handler handler = new Handler(Looper.getMainLooper());
        private final Aquarium aquarium = new Aquarium(getResources().getDisplayMetrics().density);
        private boolean visible;
        private long lastFrame;

        private final Runnable frame = new Runnable() {
            @Override
            public void run() {
                long now = SystemClock.uptimeMillis();
                float dt = Math.min(0.05f, (now - lastFrame) / 1000f);
                lastFrame = now;
                aquarium.update(dt);
                drawFrame();
                handler.removeCallbacks(this);
                if (visible) {
                    long spent = SystemClock.uptimeMillis() - now;
                    handler.postDelayed(this, Math.max(1, FRAME_MS - spent));
                }
            }
        };

        @Override
        public void onVisibilityChanged(boolean v) {
            visible = v;
            handler.removeCallbacks(frame);
            if (v) {
                lastFrame = SystemClock.uptimeMillis();
                handler.post(frame);
            }
        }

        @Override
        public void onSurfaceChanged(SurfaceHolder holder, int format, int width, int height) {
            super.onSurfaceChanged(holder, format, width, height);
            aquarium.setSize(width, height);
            drawFrame();
        }

        @Override
        public void onSurfaceDestroyed(SurfaceHolder holder) {
            visible = false;
            handler.removeCallbacks(frame);
            super.onSurfaceDestroyed(holder);
        }

        @Override
        public void onDestroy() {
            visible = false;
            handler.removeCallbacks(frame);
            aquarium.release();
            super.onDestroy();
        }

        @Override
        public void onOffsetsChanged(float xOffset, float yOffset, float xStep, float yStep, int xPixels, int yPixels) {
            aquarium.setOffset(xOffset);
        }

        @Override
        public Bundle onCommand(String action, int x, int y, int z, Bundle extras, boolean resultRequested) {
            if (WallpaperManager.COMMAND_TAP.equals(action)) aquarium.tap(x, y);
            return null;
        }

        private void drawFrame() {
            if (!aquarium.hasSize()) return;
            SurfaceHolder holder = getSurfaceHolder();
            Canvas c = null;
            try {
                c = Build.VERSION.SDK_INT >= 28 ? holder.lockHardwareCanvas() : holder.lockCanvas();
                if (c != null) aquarium.draw(c);
            } catch (IllegalStateException | IllegalArgumentException e) {
                // Surface went away mid-frame; the next visibility/surface callback restarts drawing.
            } finally {
                if (c != null) {
                    try {
                        holder.unlockCanvasAndPost(c);
                    } catch (IllegalStateException | IllegalArgumentException ignored) {
                    }
                }
            }
        }
    }
}
