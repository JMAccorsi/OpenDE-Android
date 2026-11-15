# openDE v1.0 — Android Port of OpenDE (Deus Ex)

**openDE** is an Android (`arm64-v8a`, Android 8.0+) fork of **OpenDE** by `jarmitage99`, bringing Deus Ex (2000) natively to Android with built-in GOG & Steam game-data installation, touch controls, gamepad support, PT-BR localization, and OpenGL ES rendering fixes.

> **Note:** Original Deus Ex game data is **not** included. You can extract your own GOG installer (`.exe`), sign in with Steam (QR code or credentials) to download your copy, or pick an existing Deus Ex folder on your device.
>
> **AI-Assisted Development:** This Android port and its launcher, installers, touch UI, and rendering fixes were developed with AI assistance.

---

## Highlights & Changes in This Release

### Launcher & Game-Data Installers
- **Authentic Deus Ex (2000) Launcher UI** (`BootActivity` + `DeusExTheme`): gunmetal/slate-gray vector interface with 45° chamfered panels, PCB-trace backdrop, and segmented progress bar.
- **Native GOG / Inno Setup Extractor** (`inno_extract.cc`): extracts `setup_deus_ex_goty*.exe` directly on Android, supporting concatenated zlib streams and multi-part `before_install(..., N)` assembly.
- **Native Steam Login & Depot Downloader** (`SteamInstaller.java`): authenticate via Steam Guard QR code (rendered on-screen) or username/password to download App `6910` / Depot `6911` directly to `/storage/emulated/0/OpenDE`.
- **Auto-Scan & Folder Picker**: automatically detects existing installations or GOG `.exe` installers on internal storage / SD card.

### Controls, UI & Localization
- **Touch Controls + In-Game Layout Editor**: virtual analog stick, customizable action buttons, opacity/scale/sensitivity settings, and auto-hide when a physical controller or keyboard is used.
- **Gamepad Support**: `SDL_GameController` mapping, stick curves (linear/power/expo/sigmoid), deadzones, and `DXController.u` menu navigation.
- **Granular UI Scale (`0.25x` steps + `Auto`)**: `Auto` mode targets a `480px` virtual height for clean scaling across Android phones and tablets.
- **PT-BR Localization**: bundled `.int` and `.u` translation assets alongside `DXController.u`.

### Graphics & Engine Fixes
- **Reflective Floor & Translucency Order Fix**: renders translucent level geometry before `flushTranslucent(view)` so held objects and translucent meshes render properly above reflective floors/water.
- **Intro (`00_Intro.dx`) Mirror Fix**: removed the redundant second `BlendMode::Modulated` pass on lightmapped `PF_Translucent` BSP batches and movers (`drawBatch` & `drawGroup`), fixing the black Z-fighting block on the reflective floor.

---

## Credits & Acknowledgments
- **Upstream OpenDE:** [jarmitage99](https://github.com/jarmitage99/OpenDE) (MIT License)
- **Original OpenDE Credits:** `gildor`, `dpJudas`, `jmarshall`, `AnthraX`, `Marco888`
- **Original Game:** *Deus Ex* by **Ion Storm** / **Eidos Interactive**
- **Controller Package:** `DXController` (`DXController.u`)
- **Third-Party Libraries:** `SDL2`, `miniaudio`, `glad`, `stb_image`, `XZ/liblzma`, `zlib`, `JavaSteam` (`in.dragonbra:javasteam`), `BouncyCastle`, `ZXing`
- **AI-Assisted Development:** Porting, native Android UI/installers, and bugfixes developed with AI assistance.
