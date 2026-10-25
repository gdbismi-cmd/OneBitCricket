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
