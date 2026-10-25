# 1-Bit Cricket (Android)

Top-down, black-and-white pixel cricket. The whole game lives in `app/src/main/assets/index.html`
and runs inside a full-screen WebView.

## Build the APK

**Android Studio:** File > Open this folder, let Gradle sync, then Build > Build App Bundle(s) / APK(s) > Build APK(s).
The APK lands in `app/build/outputs/apk/`.

**GitHub (no install needed):** push this folder to a GitHub repo. The included workflow builds the APK
on every push; download it from the run's *Artifacts* section under the Actions tab.

**Command line:** with the Android SDK and Gradle 8.7+ installed, run `gradle :app:assembleRelease`.

The release build is signed with the debug key, so it installs directly (allow "Install unknown apps"),
but it isn't suitable for the Play Store without your own signing key.

## Aquarium live wallpaper

The `aquarium` module is a separate app: an animated reef-tank live wallpaper drawn with Android's Canvas
(`aquarium/src/main/java/com/onebit/aquarium/Aquarium.java`). Swaying plants, light rays, a bubbler, a
jellyfish, a neon-tetra school and other fish swim on two parallax layers that slide as you swipe home-screen pages.
Tap the home screen to drop food and the nearby fish will race over and eat it.

Build it with `gradle :aquarium:assembleRelease` (the GitHub workflow uploads it as *AquariumWallpaper-apk*).
After installing, open the **Aquarium** app icon to go straight to the wallpaper preview, then tap *Set wallpaper*.
Animation pauses whenever the wallpaper isn't visible, so it doesn't drain the battery in the background.
