package com.onebit.vesuvius;

import android.content.Context;
import android.opengl.GLES20;
import android.opengl.GLSurfaceView;
import android.util.Log;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.FloatBuffer;

import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

/** Draws the whole scene as one full-screen fragment shader. */
final class SceneRenderer implements GLSurfaceView.Renderer {
    private static final String TAG = "Vesuvius";
    private static final String VERTEX =
            "attribute vec2 aPos;\nvoid main() { gl_Position = vec4(aPos, 0.0, 1.0); }\n";
    private static final String[] SCALARS = {
            "uTime", "uPan", "uLava", "uErupt", "uSnowFall", "uSnowCover", "uClouds", "uHaze",
            "uWind", "uFlash", "uBoltSeed", "uStars", "uCityLights", "uShipCount"};

    private final Context context;
    private final Settings settings;
    private final SceneState state = new SceneState();
    private final FloatBuffer quad;

    private int program;
    private int width = 1, height = 1;
    private int locRes, locSun, locSunDir, locMoon, locShipA, locShipB;
    private final int[] locScalar = new int[SCALARS.length];
    private long lastNanos;
    private volatile float panTarget;
    private volatile boolean tapPending;

    SceneRenderer(Context context, Settings settings) {
        this.context = context.getApplicationContext();
        this.settings = settings;
        float[] v = {-1f, -1f, 1f, -1f, -1f, 1f, 1f, 1f};
        quad = ByteBuffer.allocateDirect(v.length * 4).order(ByteOrder.nativeOrder()).asFloatBuffer();
        quad.put(v).position(0);
    }

    void setPan(float pan) {
        panTarget = pan;
    }

    void tap() {
        tapPending = true;
    }

    @Override
    public void onSurfaceCreated(GL10 unused, EGLConfig config) {
        program = 0;
        try {
            program = link(VERTEX, readAsset("scene.frag"));
        } catch (RuntimeException | IOException e) {
            Log.e(TAG, "Shader setup failed", e);
            return;
        }
        locRes = GLES20.glGetUniformLocation(program, "uRes");
        locSun = GLES20.glGetUniformLocation(program, "uSun");
        locSunDir = GLES20.glGetUniformLocation(program, "uSunDir");
        locMoon = GLES20.glGetUniformLocation(program, "uMoon");
        locShipA = GLES20.glGetUniformLocation(program, "uShipA");
        locShipB = GLES20.glGetUniformLocation(program, "uShipB");
        for (int i = 0; i < SCALARS.length; i++) {
            locScalar[i] = GLES20.glGetUniformLocation(program, SCALARS[i]);
        }
        lastNanos = System.nanoTime();
    }

    @Override
    public void onSurfaceChanged(GL10 unused, int w, int h) {
        width = Math.max(1, w);
        height = Math.max(1, h);
        GLES20.glViewport(0, 0, width, height);
    }

    @Override
    public void onDrawFrame(GL10 unused) {
        long now = System.nanoTime();
        float dt = (now - lastNanos) / 1e9f;
        lastNanos = now;
        if (tapPending) {
            tapPending = false;
            state.tap();
        }
        Settings s = settings;
        state.update(dt, s, s.parallax ? panTarget : 0f);

        if (program == 0) {
            GLES20.glClearColor(0.05f, 0.03f, 0.03f, 1f);
            GLES20.glClear(GLES20.GL_COLOR_BUFFER_BIT);
            return;
        }
        GLES20.glUseProgram(program);
        GLES20.glUniform2f(locRes, width, height);
        GLES20.glUniform4fv(locSun, 1, state.sun, 0);
        GLES20.glUniform3fv(locSunDir, 1, state.sunDir, 0);
        GLES20.glUniform4fv(locMoon, 1, state.moon, 0);
        GLES20.glUniform4fv(locShipA, SceneState.MAX_SHIPS, state.shipA, 0);
        GLES20.glUniform4fv(locShipB, SceneState.MAX_SHIPS, state.shipB, 0);
        float[] values = {
                state.time, state.pan, state.lava, state.erupt, s.snowfall, s.snowCover, s.clouds, s.haze,
                s.wind, state.flash, state.boltSeed, s.stars ? 1f : 0f, s.cityLights, state.shipCount};
        for (int i = 0; i < values.length; i++) {
            GLES20.glUniform1f(locScalar[i], values[i]);
        }
        GLES20.glEnableVertexAttribArray(0);
        GLES20.glVertexAttribPointer(0, 2, GLES20.GL_FLOAT, false, 0, quad);
        GLES20.glDrawArrays(GLES20.GL_TRIANGLE_STRIP, 0, 4);
    }

    private String readAsset(String name) throws IOException {
        try (InputStream in = context.getAssets().open(name)) {
            ByteArrayOutputStream out = new ByteArrayOutputStream();
            byte[] buf = new byte[8192];
            int n;
            while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
            return out.toString("UTF-8");
        }
    }

    private static int compile(int type, String src) {
        int sh = GLES20.glCreateShader(type);
        GLES20.glShaderSource(sh, src);
        GLES20.glCompileShader(sh);
        int[] ok = new int[1];
        GLES20.glGetShaderiv(sh, GLES20.GL_COMPILE_STATUS, ok, 0);
        if (ok[0] == 0) {
            String log = GLES20.glGetShaderInfoLog(sh);
            GLES20.glDeleteShader(sh);
            throw new RuntimeException("Shader compile error: " + log);
        }
        return sh;
    }

    private static int link(String vs, String fs) {
        int p = GLES20.glCreateProgram();
        GLES20.glAttachShader(p, compile(GLES20.GL_VERTEX_SHADER, vs));
        GLES20.glAttachShader(p, compile(GLES20.GL_FRAGMENT_SHADER, fs));
        GLES20.glBindAttribLocation(p, 0, "aPos");
        GLES20.glLinkProgram(p);
        int[] ok = new int[1];
        GLES20.glGetProgramiv(p, GLES20.GL_LINK_STATUS, ok, 0);
        if (ok[0] == 0) {
            String log = GLES20.glGetProgramInfoLog(p);
            GLES20.glDeleteProgram(p);
            throw new RuntimeException("Program link error: " + log);
        }
        return p;
    }
}
