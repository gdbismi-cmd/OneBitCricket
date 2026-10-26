package com.onebit.vesuvius;

import java.util.Arrays;
import java.util.Calendar;
import java.util.Random;

/** Simulates everything that moves: the clock, sun and moon, eruption pulses, lightning and ships. */
final class SceneState {
    static final int MAX_SHIPS = 6;
    private static final int SAUCER = 0, CRUISER = 1, MOTHERSHIP = 2;
    private static final float CRATER_X = 0.04f;
    private static final double SYNODIC_MONTH = 29.530588853;
    private static final long NEW_MOON_2000 = 947182440000L; // 2000-01-06 18:14 UTC

    // shader inputs
    float time, pan;
    final float[] sun = new float[4];
    final float[] sunDir = new float[3];
    final float[] moon = new float[4];
    float lava, erupt, flash, boltSeed;
    int shipCount;
    final float[] shipA = new float[MAX_SHIPS * 4];
    final float[] shipB = new float[MAX_SHIPS * 4];

    private final Random rnd = new Random();
    private Ship[] ships = new Ship[0];
    private int builtCount = -1;
    private boolean builtMothership;
    private double elapsed;
    private double cycleHour = Double.NaN;
    private float burst;
    private float flashAge = 99f;
    private float nextStrike = 2f;

    void tap() {
        burst = 1f;
    }

    void update(float dt, Settings s, float panTarget) {
        dt = Math.min(Math.max(dt, 0f), 0.1f);
        elapsed += dt;
        time = (float) (elapsed % 3600.0);
        pan += (panTarget - pan) * Math.min(1f, dt * 6f);

        updateSky(dt, s);
        updateVolcano(dt, s);
        updateShips(dt, s);
    }

    // ------------------------------------------------------------ sun & moon
    private void updateSky(float dt, Settings s) {
        Calendar now = Calendar.getInstance();
        double realHour = now.get(Calendar.HOUR_OF_DAY) + now.get(Calendar.MINUTE) / 60.0 + now.get(Calendar.SECOND) / 3600.0;
        double hour;
        switch (s.timeMode) {
            case "fixed":
                hour = s.fixedHour;
                cycleHour = Double.NaN;
                break;
            case "cycle":
                if (Double.isNaN(cycleHour)) cycleHour = realHour;
                cycleHour = (cycleHour + dt * 24.0 / (Math.max(1f, s.cycleMinutes) * 60.0)) % 24.0;
                hour = cycleHour;
                break;
            default:
                hour = realHour;
                cycleHour = Double.NaN;
        }

        double a = (hour - 6.0) / 12.0 * Math.PI;
        float ca = (float) Math.cos(a), sa = (float) Math.sin(a);
        sun[0] = -ca * 0.5f;
        sun[1] = sa * 0.72f;
        sun[2] = sa * 0.9f;
        sunDir[0] = -ca * 0.8f;
        sunDir[1] = Math.max(sa * 0.9f, -0.1f) + 0.05f;
        sunDir[2] = -0.6f * ca - 0.1f;

        double phase = ((System.currentTimeMillis() - NEW_MOON_2000) / 86400000.0 / SYNODIC_MONTH) % 1.0;
        if (phase < 0) phase += 1.0;
        double am = a - phase * 2.0 * Math.PI;
        moon[0] = (float) (-Math.cos(am) * 0.45);
        moon[1] = (float) (Math.sin(am) * 0.62);
        moon[2] = (float) (Math.sin(am) * 0.8);
        moon[3] = (float) phase;
    }

    // ------------------------------------------------------------ eruption & lightning
    private void updateVolcano(float dt, Settings s) {
        burst = Math.max(0f, burst - dt / 10f);
        float e = (float) elapsed;
        float pulse = 0.85f + 0.15f * (float) (Math.sin(e * 0.13) * Math.sin(e * 0.31 + 1.0));
        erupt = clamp01(s.eruption * pulse + burst * 0.7f);
        lava = clamp01(s.lava + burst * 0.35f);

        flashAge += dt;
        if (s.lightning && erupt > 0.45f) {
            nextStrike -= dt * (erupt - 0.45f) * 2.2f;
            if (nextStrike <= 0f) {
                flashAge = 0f;
                boltSeed = rnd.nextFloat() * 100f;
                nextStrike = 0.5f + rnd.nextFloat() * 2.5f;
            }
        }
        if (flashAge < 0.45f) {
            float flicker = 0.55f + 0.45f * (float) Math.sin(flashAge * 70.0);
            flash = (float) Math.exp(-flashAge * 7.0) * flicker;
        } else {
            flash = 0f;
        }
    }

    // ------------------------------------------------------------ ships
    private static final class Ship {
        int type;
        float scale, x, y, vx, vy, tx, ty, dwell, phase, beam, beamTarget, beamTimer;
        int facing = 1;
    }

    private void updateShips(float dt, Settings s) {
        int want = Math.max(0, Math.min(MAX_SHIPS, s.shipCount));
        if (want != builtCount || s.mothership != builtMothership) buildFleet(want, s.mothership);

        float speed = 0.01f + 0.07f * s.shipSpeed;
        float t = (float) elapsed;
        for (Ship sh : ships) {
            float max = speed * (sh.type == MOTHERSHIP ? 0.25f : sh.type == CRUISER ? 1.8f : 1f);
            float dx = sh.tx - sh.x, dy = sh.ty - sh.y;
            float dist = (float) Math.sqrt(dx * dx + dy * dy);
            float want2 = Math.min(max, dist * 0.7f);
            float dvx = dist > 1e-4f ? dx / dist * want2 : 0f;
            float dvy = dist > 1e-4f ? dy / dist * want2 : 0f;
            float k = Math.min(1f, dt * (sh.type == MOTHERSHIP ? 0.4f : 1.4f));
            sh.vx += (dvx - sh.vx) * k;
            sh.vy += (dvy - sh.vy) * k;
            sh.x += sh.vx * dt;
            sh.y += sh.vy * dt;

            if (sh.type == CRUISER && Math.abs(sh.x) > 0.95f && Math.signum(sh.x) == sh.facing) {
                respawnCruiser(sh);
            } else if (dist < 0.012f) {
                sh.dwell -= dt;
                if (sh.dwell <= 0f) pickTarget(sh);
            }

            // tractor beams: saucers hovering over the volcano
            sh.beamTimer -= dt;
            boolean over = Math.abs(sh.x - CRATER_X) < 0.3f && dist < 0.03f;
            if (sh.type == SAUCER && s.beams && over && sh.beamTimer <= 0f) {
                sh.beamTarget = rnd.nextFloat() < 0.55f ? 1f : 0f;
                sh.beamTimer = 3f + rnd.nextFloat() * 6f;
            }
            if (!s.beams || !over || sh.type != SAUCER) sh.beamTarget = 0f;
            sh.beam += (sh.beamTarget - sh.beam) * Math.min(1f, dt * 2.5f);
        }

        // draw far (small) ships first
        Ship[] order = ships.clone();
        Arrays.sort(order, (p, q) -> Float.compare(p.scale, q.scale));
        shipCount = order.length;
        Arrays.fill(shipA, 0f);
        Arrays.fill(shipB, 0f);
        for (int i = 0; i < order.length; i++) {
            Ship sh = order[i];
            float bobAmp = sh.type == MOTHERSHIP ? 0.006f : 0.004f + sh.scale * 0.05f;
            float bob = (float) Math.sin(t * (0.5f + sh.phase * 0.6f) + sh.phase * 10f) * bobAmp;
            float tilt;
            if (sh.type == SAUCER) {
                tilt = clamp(-sh.vx * 5f, -0.3f, 0.3f) + 0.04f * (float) Math.sin(t * 0.9f + sh.phase * 7f);
            } else if (sh.type == CRUISER) {
                tilt = clamp(sh.vy * 4f * sh.facing, -0.25f, 0.25f) + 0.015f * (float) Math.sin(t * 0.7f + sh.phase * 5f);
            } else {
                tilt = 0.01f * (float) Math.sin(t * 0.25f + sh.phase);
            }
            float type = sh.type + (sh.facing < 0 ? 3f : 0f);
            shipA[i * 4] = sh.x;
            shipA[i * 4 + 1] = sh.y + bob;
            shipA[i * 4 + 2] = sh.scale;
            shipA[i * 4 + 3] = type;
            shipB[i * 4] = tilt;
            shipB[i * 4 + 1] = sh.beam;
            shipB[i * 4 + 2] = s.navLights ? 1f : 0f;
            shipB[i * 4 + 3] = sh.phase;
        }
    }

    private void buildFleet(int count, boolean mother) {
        builtCount = count;
        builtMothership = mother;
        ships = new Ship[count];
        for (int i = 0; i < count; i++) {
            Ship sh = new Ship();
            sh.phase = rnd.nextFloat();
            if (i == 0 && mother) {
                sh.type = MOTHERSHIP;
                sh.scale = 0.11f + rnd.nextFloat() * 0.03f;
            } else {
                sh.type = (i % 2 == 1) ? SAUCER : CRUISER;
                sh.scale = sh.type == SAUCER ? 0.022f + rnd.nextFloat() * 0.022f : 0.03f + rnd.nextFloat() * 0.025f;
                sh.facing = rnd.nextBoolean() ? 1 : -1;
            }
            pickTarget(sh);
            sh.x = sh.tx + (rnd.nextFloat() - 0.5f) * 0.1f;
            sh.y = sh.ty + (rnd.nextFloat() - 0.5f) * 0.05f;
            sh.dwell = rnd.nextFloat() * 4f;
            ships[i] = sh;
        }
    }

    private void pickTarget(Ship sh) {
        switch (sh.type) {
            case MOTHERSHIP:
                sh.tx = -0.25f + rnd.nextFloat() * 0.5f;
                sh.ty = 0.50f + rnd.nextFloat() * 0.12f;
                sh.dwell = 8f + rnd.nextFloat() * 12f;
                break;
            case CRUISER:
                // always fly forward; occasionally pause mid-screen, eventually leave
                sh.tx = sh.x + sh.facing * (0.15f + rnd.nextFloat() * 0.45f);
                if (rnd.nextFloat() < 0.25f) sh.tx = sh.facing * 1.1f;
                sh.ty = 0.30f + rnd.nextFloat() * 0.25f;
                sh.dwell = 1f + rnd.nextFloat() * 4f;
                break;
            default:
                if (rnd.nextFloat() < 0.5f) {
                    sh.tx = CRATER_X + (rnd.nextFloat() - 0.5f) * 0.45f;   // over the volcano
                    sh.ty = 0.27f + rnd.nextFloat() * 0.12f;
                } else {
                    sh.tx = (rnd.nextFloat() - 0.5f) * 0.9f;
                    sh.ty = 0.28f + rnd.nextFloat() * 0.25f;
                }
                sh.dwell = 2f + rnd.nextFloat() * 8f;
        }
    }

    private void respawnCruiser(Ship sh) {
        sh.facing = rnd.nextBoolean() ? 1 : -1;
        sh.scale = 0.03f + rnd.nextFloat() * 0.025f;
        sh.x = -sh.facing * 0.95f;
        sh.y = 0.30f + rnd.nextFloat() * 0.25f;
        sh.vx = sh.facing * 0.03f;
        sh.vy = 0f;
        pickTarget(sh);
    }

    private static float clamp01(float v) {
        return clamp(v, 0f, 1f);
    }

    private static float clamp(float v, float lo, float hi) {
        return v < lo ? lo : (v > hi ? hi : v);
    }
}
