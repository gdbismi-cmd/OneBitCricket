# Vesuvius Visitors (Android live wallpaper)

Spaceships hover over Mount Vesuvius and the Bay of Naples. Everything is drawn live by one
OpenGL ES 2.0 fragment shader (`app/src/main/assets/scene.frag`); the Java side simulates the clock,
sun and moon (with the real lunar phase), eruption pulses, volcanic lightning and ship flight paths.

## Adjustable from the settings screen
- **Time of day:** follow the real clock, pin a fixed time, or run a fast day/night cycle (1-60 min per day)
- **Volcano:** lava flow reach, eruption strength (steam wisp to ash column with lava bombs),
  volcanic lightning, tap-to-erupt
- **Weather:** snowfall, snow cover on the summit, clouds, haze/mist, wind direction and strength,
  stars and Milky Way, town lights
- **Spaceships:** count (0-6), mothership, flight speed, tractor beams, navigation lights
- **Battery:** frame rate (15-60 fps), render resolution, parallax scrolling between home screens

## Build the APK
Same as the cricket game in the repo root: open this folder in Android Studio and build, run
`gradle :app:assembleRelease` with the Android SDK installed, or let the
`Build Vesuvius wallpaper APK` GitHub workflow build it and download the artifact from the run.
The release build is signed with the debug key so it installs directly (allow "Install unknown apps").

After installing, open **Vesuvius Visitors** from the app drawer and tap **Set as live wallpaper**.
