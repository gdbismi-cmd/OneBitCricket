package com.onebit.vesuvius;

import android.content.SharedPreferences;

/** Snapshot of the user's preferences, read by the GL thread. */
final class Settings {
    volatile String timeMode = "real";
    volatile float fixedHour = 19f;
    volatile float cycleMinutes = 10f;
    volatile float lava = 0.6f, eruption = 0.45f;
    volatile boolean lightning = true, tapEruption = true;
    volatile float snowfall = 0f, snowCover = 0.25f, clouds = 0.4f, haze = 0.3f, wind = 0.3f;
    volatile boolean stars = true;
    volatile float cityLights = 0.8f;
    volatile int shipCount = 3;
    volatile boolean mothership = true, beams = true, navLights = true;
    volatile float shipSpeed = 0.4f;
    volatile int fps = 30;
    volatile float quality = 0.7f;
    volatile boolean parallax = true;

    void load(SharedPreferences p) {
        timeMode = p.getString("time_mode", "real");
        fixedHour = p.getInt("fixed_hour", 76) / 4f;
        cycleMinutes = p.getInt("cycle_minutes", 10);
        lava = p.getInt("lava", 60) / 100f;
        eruption = p.getInt("eruption", 45) / 100f;
        lightning = p.getBoolean("lightning", true);
        tapEruption = p.getBoolean("tap_eruption", true);
        snowfall = p.getInt("snowfall", 0) / 100f;
        snowCover = p.getInt("snow_cover", 25) / 100f;
        clouds = p.getInt("clouds", 40) / 100f;
        haze = p.getInt("haze", 30) / 100f;
        wind = (p.getInt("wind", 65) - 50) / 50f;
        stars = p.getBoolean("stars", true);
        cityLights = p.getInt("city_lights", 80) / 100f;
        shipCount = p.getInt("ship_count", 3);
        mothership = p.getBoolean("mothership", true);
        shipSpeed = p.getInt("ship_speed", 40) / 100f;
        beams = p.getBoolean("beams", true);
        navLights = p.getBoolean("nav_lights", true);
        try {
            fps = Integer.parseInt(p.getString("fps", "30"));
        } catch (NumberFormatException e) {
            fps = 30;
        }
        quality = p.getInt("quality", 70) / 100f;
        parallax = p.getBoolean("parallax", true);
    }
}
