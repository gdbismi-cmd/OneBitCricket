# 1-Bit Cricket (Android + Nintendo Switch)

Top-down pixel cricket, in colour or the original black and white (Look: Colour / 1-Bit in the menu). The whole game lives in `app/src/main/assets/index.html`
and runs inside a full-screen WebView on Android. `switch/` holds a C++/SDL2 port for Nintendo Switch homebrew.

## Build the APK

**Android Studio:** File > Open this folder, let Gradle sync, then Build > Build App Bundle(s) / APK(s) > Build APK(s).
The APK lands in `app/build/outputs/apk/`.

**GitHub (no install needed):** push this folder to a GitHub repo. The included workflow builds the APK
on every push; download it from the run's *Artifacts* section under the Actions tab.

**Command line:** with the Android SDK and Gradle 8.7+ installed, run `gradle :app:assembleRelease`.

The release build is signed with the debug key, so it installs directly (allow "Install unknown apps"),
but it isn't suitable for the Play Store without your own signing key.

## Nintendo Switch (homebrew)

`switch/` is a C++/SDL2 port built with the open-source devkitPro/libnx toolchain. It produces
`OneBitCricket.nro`, which runs from the Homebrew Menu on a Switch with custom firmware. It is not an
official Nintendo release: that needs Nintendo's developer program and SDK.

The game logic in `switch/source/main.cpp` mirrors the JS function for function, so a gameplay change
in `index.html` can be carried across line by line.

**GitHub:** the `Build Switch NRO` workflow builds it on every push; download `OneBitCricket-switch`
from the run's *Artifacts*.

**Locally:** install devkitPro with `switch-dev` and `switch-sdl2`, then run `make -C switch`.
To try the port on a PC, run `make -C switch -f Makefile.desktop` (needs SDL2) and start `switch/onebit-cricket`.

**Install:** copy `OneBitCricket.nro` to `sdmc:/switch/` and open it from the Homebrew Menu. Launch the
Homebrew Menu by holding R while opening a game rather than from the Album applet: applet mode has less memory.
Records are saved to `sdmc:/switch/OneBitCricket/records.txt`.

| Control | Action |
| --- | --- |
| Left stick | Pull away from where you want to hit, let go to play the shot (outer ring lofts) |
| A | Play the shot at the current pull; with the stick centred, defend |
| D-pad | Quick shots: left = leg side, right = off side, up = lofted straight, down = defend |
| Touch screen | Drag the on-screen stick and let go, tap buttons (handheld) |
| L / R | Change ground (menu) |
| X / Y / R | Training: pace or spin, bowling speed, timing guide |
| + | Pause; exits from the main menu |

The pixel font is [Silkscreen](https://github.com/googlefonts/silkscreen) (SIL Open Font License,
`switch/fonts/OFL.txt`), baked into `switch/source/font.h` by `switch/tools/mkfont.py`.
