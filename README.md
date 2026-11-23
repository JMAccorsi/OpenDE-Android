# OpenDE Android

A native Android (`arm64-v8a`) port of **[OpenDE](https://github.com/jarmitage99/OpenDE)** — an open-source C++23 reimplementation of the *Deus Ex* (2000) engine running the original game's UnrealScript bytecode from the shipped `.u` packages on **SDL3** and **OpenGL ES 3.2**.

> **Game Data Required:** The original *Deus Ex: Game of the Year Edition* (`1112fm`, GOG or Steam) files are required and are **not** included. You can install them directly on-device from the built-in launcher without needing a PC.

---

## Key Features

- **Unified SDL3 Launcher (`DXLauncher`)**
  - Adapted from **[JuggyMcNutty/deusex-launcher](https://github.com/JuggyMcNutty/deusex-launcher)** with full touch and gamepad navigation, authentic *Deus Ex* visuals, Continue with live save thumbnail preview, Saves browser, Screenshots gallery, Mods & Profiles manager, Settings, and built-in **Install & Updates** — completely replacing the legacy Java UI.
- **On-Device Game Data Installation (No PC Required)**
  - **Steam Login & Download:** Sign in directly via Steam Guard QR code or credentials to download *Deus Ex GOTY* (App `6910` / Depot `6911`) using **[JavaSteam](https://github.com/Longi94/JavaSteam)**.
  - **GOG / Inno Setup Extractor:** Native C++ extractor (`inno_extract.cc`) unpacks `setup_deus_ex_goty*.exe` directly on Android, including concatenated zlib streams and multi-part chunks.
  - **Folder Picker & Auto-Scan:** Point directly to an existing *Deus Ex* installation on internal storage or SD card.
- **Full Gamepad Support (`DXController`)**
  - Powered by **[dsgls/DXController](https://github.com/dsgls/DXController)**: weapon & augmentation radial wheels, on-screen keyboard, full controller navigation across every menu, terminal, ATM, and keypad, configurable stick curves & deadzones, and rotating periodic autosaves.
- **Gyroscope Camera Aiming**
  - High-rate Android NDK IMU gyroscope + external SDL3 controller gyro with gravity-assisted yaw (`Auto (Gravity)`, `Yaw`, `Roll`) and configurable activation modes (`Off`, `Always On`, `Aim`, `Scope`).
- **Customizable Touch Controls**
  - Virtual stick, action buttons, quick-save button, and live Layout Editor (drag, scale, opacity, sensitivity) that auto-hide whenever a physical gamepad or keyboard is used.
- **Modern Graphics, Performance & Localization**
  - OpenGL ES 3.2 backend with Hor+ widescreen FOV, anisotropic & trilinear filtering, MSAA/FXAA, bicubic smooth lightmaps, granular UI scaling (`0.25x` steps + `Auto`), Performance HUD, thermal frame pacing, and full **English** and **Portuguese (PT-BR)** localization.
- **Built-in OTA Updates**
  - Checks GitHub Releases directly from the launcher, verifies downloads via SHA-256, and installs updates in-place via Android `PackageInstaller`.

---

## Quick Start (Android)

1. Download the latest `openDE-v*-arm64-v8a.apk` from the **[Releases](https://github.com/JMAccorsi/OpenDE-Android/releases)** page (requires Android 8.0+ / API 26+, `arm64-v8a`).
2. Open **openDE** and go to **Install & Updates...** (or follow the first-run setup):
   - Sign in with **Steam** (QR code or username/password), **or**
   - Pick your **GOG** `setup_deus_ex_goty*.exe` installer, **or**
   - Select an existing **Deus Ex GOTY** folder (`System/`, `Maps/`, `Textures/`, `Sounds/`, `Music/`).
3. Tap **Play** or **Continue** — supports touch controls, external Bluetooth/USB controllers, handhelds (Retroid, Odin, etc.), and keyboard/mouse.

---

## Building

### Android

Requires Android SDK (API 34), NDK `26.3.11579264`, CMake `3.30.5`, and JDK 17:

```bash
cd android
./gradlew assembleRelease
```

### Desktop (Linux / Windows / macOS) & Web

All upstream CMake targets (`deusex`, `deused`, `ucc`, `unit-tests`, and Emscripten WebGL 2) remain supported. Every C/C++ dependency is vendored under `vendor/`:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
```

- Copy *Deus Ex GOTY (1112fm)* data into `game-data/` or set `DEUSEX_GAME_DATA=/path/to/deusex`.
- Useful options: `-DSKIP_QA=ON` (skip `clang-format`), `-DSKIP_UT=ON` (skip unit tests).

---

## Credits & Upstream Projects

This project stands on the shoulders of the *Deus Ex* and open-source communities:

### Core Projects
- **[OpenDE](https://github.com/jarmitage99/OpenDE)** — Upstream open-source *Deus Ex* engine & editor by **[jarmitage99](https://github.com/jarmitage99)** (MIT License).
  - Special thanks to the Unreal Engine 1 / *Deus Ex* legends credited upstream: **gildor**, **dpJudas**, **jmarshall**, **AnthraX**, and **Marco888**.
- **[DXLauncher (`deusex-launcher`)](https://github.com/JuggyMcNutty/deusex-launcher)** — Native SDL3 launcher UI toolkit, asset parser, and font/theme system adapted for Android by **[JuggyMcNutty](https://github.com/JuggyMcNutty)**.
- **[DXController](https://github.com/dsgls/DXController)** — Full gamepad navigation, radial weapon/aug wheels, on-screen keyboard, and rotating autosave manager by **[dsgls](https://github.com/dsgls)** (with thanks to **mkentie** for [Deus Exe](https://kentie.net/article/dxguide/)).
- **[JavaSteam](https://github.com/Longi94/JavaSteam)** — Native Java Steam client & DepotDownloader by **[Longi94 / Dragonbra](https://github.com/Longi94)**.

### Third-Party Libraries
- **[SDL3](https://github.com/libsdl-org/SDL)** & **[SDL3_ttf](https://github.com/libsdl-org/SDL_ttf)** — Windowing, input, gamepad/gyroscope sensors, audio, and text rendering
- **[FreeType](https://gitlab.freedesktop.org/freetype/freetype)** — Font rasterization
- **[libxmp](https://github.com/libxmp/libxmp)** — Tracker music (`.umx`) playback
- **[miniaudio](https://github.com/mackron/miniaudio)** — Low-level audio backend
- **[glad](https://github.com/Dav1dde/glad)** — OpenGL / OpenGL ES loader
- **[stb](https://github.com/nothings/stb)** — Image decoding (`stb_image`)
- **[ZXing](https://github.com/zxing/zxing)** — Steam Guard QR code rendering
- **[BouncyCastle](https://github.com/bcgit/bc-java)** — Cryptography for Steam authentication
- **[Apache Commons Compress](https://github.com/apache/commons-compress)** & **[XZ for Java](https://github.com/tukaani-project/xz-java)** — Archive & LZMA decompression

### Legal & Transparency
- **Deus Ex (2000)** is owned by **Ion Storm / Eidos Interactive / Square Enix / Embracer Group**. No proprietary game assets are distributed in this repository or its releases.
- **AI-Assisted Development Notice:** Parts of this Android port, its launcher integration, native installers, and bug fixes were developed and debugged with AI assistance.
