# OpenDE

An open-source reimplementation of the Deus Ex engine and UnrealEd. It runs
the original game's UnrealScript bytecode from the shipped `.u` packages on a
new C++ engine built on SDL2 and OpenGL 3.3.

The original game data is required and is not included.

## Features

- Runs natively on Linux, Windows, macOS and in the browser
- Widescreen support with Hor+ field of view
- imgui-based editor
- Anisotropic filtering, trilinear filtering
- MSAA and FXAA
- Smooth lighting: bicubic filtering of lightmaps
- Detail textures visible at longer distances

## Requirements

Debian/Ubuntu with:

    $ sudo apt install build-essential cmake pkg-config libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxss-dev libxkbcommon-dev libgl-dev libegl-dev libasound2-dev libpulse-dev libwayland-dev wayland-protocols libdecor-0-dev

Every library dependency is vendored in `vendor/` and built with the project.

## Game data

Copy the contents of an installed Deus Ex (GOTY 1112fm) into `game-data/`, so
that `game-data/System`, `game-data/Maps`, `game-data/Textures`,
`game-data/Sounds` and `game-data/Music` exist. Another location can be given
at configure time with `-DDEUSEX_GAME_DATA=/path/to/deusex`.

The Deus Ex demo (version 1002f) works too, for example
`DEUSEX_GAME_DATA=demo-data build/bin/deusex`.

At runtime the game and the editor look for the data in this order: the
`DEUSEX_GAME_DATA` environment variable, a `game-data` directory next to the
executable (next to the AppImage file for AppImage builds), the Steam install
of Deus Ex (`steamapps/common/Deus Ex` in the default Steam directories and
their library folders), and finally the path given at configure time.

## Building

    $ cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
    $ cmake --build build -j"$(nproc)"

Options:

- `-DSKIP_QA=ON` skips clang-format during the build.
- `-DSKIP_UT=ON` skips building the unit tests.

`scripts/check-debian.sh` builds the project in clean Debian 12 and Debian 13
containers with Docker.

### Windows

Windows builds are cross-compiled with MinGW:

    $ cmake -S . -B build-mingw -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-toolchain.cmake
    $ cmake --build build-mingw -j"$(nproc)"

## Browser builds

Requires Emscripten and WebGL 2. The game data given at configure time is
packaged into `index.data`, without the Windows binaries:

    $ emcmake cmake -S . -B build-web -DCMAKE_BUILD_TYPE=Release -DSKIP_UT=ON -DDEUSEX_GAME_DATA="$PWD/demo-data"
    $ cmake --build build-web --target deusex -j"$(nproc)"
    $ python3 -m http.server -d build-web/bin 8000

Then open `http://localhost:8000/`. The game data is LZ4 compressed
and cached in IndexedDB after the first download, together with settings
and saved games.

## Running

    $ build/bin/deusex

This starts at the title screen with the main menu. You can also specify a
map to be loaded `build/bin/deusex 01_NYC_UNATCOIsland`, or a path
to a `.dx` file.

Controls, settings and saves live in `~/.local/share/deusex` (`%APPDATA%\deusex`
on Windows): `DeusEx.ini` and `User.ini` are read from there when present
and otherwise from the game `System` directory, changed settings are
written there, and saved games are in `Save/`. The game data directory is
never written to. Key bindings are the original ones from `User.ini`, for
example keypad `+` for QuickSave and keypad `/` for QuickLoad.

## Editor

    $ build/bin/deused 01_NYC_UNATCOIsland

The editor is currently a work in progress. But it works!

## UnrealScript compiler

`ucc` exports and compiles UnrealScript, like the UCC that shipped with the
original Deus Ex SDK. Sources and compiled packages live in
`~/.local/share/deusex` and packages in `~/.local/share/deusex/System`
are loaded in place of the shipped ones with the same name.

To export the sources of a package:

    $ build/bin/ucc batchexport DeusEx.u class uc ~/.local/share/deusex/DeusEx/Classes

Compile:

    $ build/bin/ucc make

## Tests

    $ build/bin/unit-tests

Unit tests that need the game data are tagged `[data]`. You can run the ones
that dont need game data with:

    $ build/bin/unit-tests "~[data]"

## Thanks

This project would not be possible without the incredible work from these
legends:

- **jarmitage99** — Creator and maintainer of upstream [OpenDE](https://github.com/jarmitage99/OpenDE)
- **gildor**
- **dpJudas**
- **jmarshall**
- **AnthraX**
- **Marco888**

---

## Android Port (`openDE`)

This fork brings **OpenDE** natively to Android (`arm64-v8a`, Android 8.0+ / API 26+) under the app name **openDE**, with touch controls, controller support, authentic Deus Ex launcher UI, native game-data installers, and OpenGL ES rendering fixes.

> **AI-Assisted Development Notice:** This Android port, its native UI/installers, and the rendering/UX improvements listed below were developed with AI assistance.

### What Was Built & Fixed in This Port

- **Native Android & OpenGL ES 3.0 Backend**
  - SDL2 Android lifecycle, APK-bundled assets (`DXController.u`, `DeusEx.u` overlay, and PT-BR `.int` / `.u` localization packages), and Android 15 adaptive launcher icon.
- **Authentic Deus Ex (2000) Launcher (`BootActivity` + `DeusExTheme`)**
  - Custom gunmetal/slate-gray vector UI with 45-degree chamfered corners, subtle PCB-trace backdrop, segmented progress bar, and automatic game-data detection.
- **Built-in Game Data Acquisition (No PC Required)**
  - **Native GOG / Inno Setup Extractor** (`inno_extract.cc`): extracts `setup_deus_ex_goty*.exe` directly on-device, including concatenated zlib streams and multi-part `before_install(..., N)` chunks.
  - **Native Steam Login & Downloader** (`SteamInstaller.java` via `JavaSteam` + `ZXing`): sign in directly with Steam Guard QR code or username/password to download Deus Ex GOTY (App 6910 / Depot 6911) into `/storage/emulated/0/OpenDE`.
  - **Folder Picker & Auto-Scan**: point directly to an existing Deus Ex installation folder on internal storage or SD card.
- **Customizable Touch Controls & Layout Editor**
  - On-screen virtual stick, action buttons, and custom layout editor (drag, scale, opacity, and sensitivity), which automatically hide when physical gamepad/keyboard input is detected.
- **Granular UI Scaling (`0.25x` steps + `Auto`)**
  - Added fine-grained `0.25x` UI scale steps and an `Auto` mode targeting a `480px` virtual height so menus, HUD, and subtitles fit comfortably on Android phone/tablet screens.
- **Rendering Fixes**
  - **Water / Reflective Floor Translucency Fix** (`scene_renderer.cc`): fixed draw order so translucent world geometry renders before `flushTranslucent(view)` without clipping held/world translucent meshes over reflective surfaces.
  - **Intro (`00_Intro.dx`) Mirror Reflection Fix** (`scene_renderer.cc`): removed the destructive second `BlendMode::Modulated` pass on lightmapped `PF_Translucent` BSP batches and movers (`drawBatch` & `drawGroup`), eliminating the black Z-fighting block over the floor reflection.
- **On-Device Debugging Bridge (`android/debug_bridge.ps1`)**
  - Command/state bridge (`debug_cmd.txt` / `debug_state.json`) for live inspection, console command execution, and native crash symbolication (`-ResolveCrash`).

### Building for Android

Requires Android SDK (API 34), NDK `26.3.11579264`, CMake `3.30.5`, and JDK 17:

    $ cd android
    $ ./gradlew assembleRelease

### Android Port Credits & Third-Party Libraries

- **OpenDE** — [jarmitage99](https://github.com/jarmitage99/OpenDE) (MIT License)
- **DXController** — Controller navigation and curve/deadzone settings (`DXController.u`)
- **JavaSteam & DepotDownloader** — [Longi94 / Dragonbra](https://github.com/Longi94/JavaSteam)
- **BouncyCastle** (`bcprov-jdk15to18`) & **ZXing** (`com.google.zxing:core`)
- **SDL2**, **miniaudio**, **glad**, **stb_image**, **XZ Embedded / liblzma**, **zlib**
- **Deus Ex** — Original game by **Ion Storm** / **Eidos Interactive** (game data not included)
- **AI-Assisted Engineering** — Built and debugged interactively with AI coding assistance
