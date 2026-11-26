# OpenDE-Android — Project Rules & Engineering Context

## 1. Repository & Upstream Context
- **Workspace:** `C:\Users\Accorsi\.gemini\antigravity\scratch\OpenDE-Android`
- **Branch / Remote:** `master` → `https://github.com/JMAccorsi/OpenDE.git` (fork of `jarmitage99/OpenDE`).
- **Package / Activity:** `com.opende.deusex` / `com.opende.deusex/.OpenDEActivity`.
- **Key Reference Projects:**
  - `jarmitage99/OpenDE` — upstream desktop engine.
  - `JuggyMcNutty/deusex-launcher` — reference for SDL3 DXLauncher UI and 3D controller preview/remapper.
  - `JuggyMcNutty/DXEngine` — reference for UE1 rendering behaviors (e.g., `EndFlash` screen flash overlay).
  - `DXController` (`dxcontroller-src/`) — UnrealScript gamepad support & `DeusExPlayer.uc` reference.

## 2. Communication & Git Workflow
- Respond concisely in **Portuguese** in chat, using clickable `file:///` links with forward slashes.
- GitHub release notes and public issue/PR text should be in **English**.
- **Never commit, tag, or push** until the user explicitly asks.
- Publish GitHub releases as **Pre-release** until the user explicitly approves `v1.1`.

## 3. Windows, PowerShell 5.1 & ADB Conventions
- **PowerShell 5.1:** Never use `&&` (unsupported). Chain dependent commands with `; if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }`.
- **Binary PNG screenshots via ADB:** Never redirect `adb exec-out screencap -p` directly in PowerShell (`>` corrupts binary as UTF-16). Always wrap in `cmd /c`:
  ```powershell
  cmd /c "adb -s <serial> exec-out screencap -p > C:\path\to\shot.png"
  ```
- **Visual verification:** Always inspect captured screenshots via `read_file` on real hardware when debugging graphics/UI issues.
- **Test Devices:**
  - **Retroid (Adreno):** serial `MC94528AYF122800013` — `Adreno (TM) 22`, OpenGL ES 3.2, `1920×1080` (up to `120 FPS`).
  - **Samsung Galaxy A06 (Mali):** serial `R9XX901F43T` — Helio G85 / `Mali-G52 MC2`, `1600×720` (primary low/mid-range optimization target).

## 4. Build, Signing & OTA Release Rules
- **Debug build (uses Release CMake flags `-O3 -DNDEBUG -ffast-math -flto=thin` + `-Werror`):**
  ```powershell
  cd C:\Users\Accorsi\.gemini\antigravity\scratch\OpenDE-Android\android
  .\gradlew.bat assembleDebug
  ```
- **Install & launch:**
  ```powershell
  adb -s MC94528AYF122800013 install -r app\build\outputs\apk\debug\app-debug.apk
  adb -s MC94528AYF122800013 shell am start -n com.opende.deusex/com.opende.deusex.OpenDEActivity
  ```
- **CRITICAL — Release Signing Key (OTA Compatibility):**
  - Always sign GitHub release APKs with `$env:USERPROFILE\.android\debug.keystore` (`--ks-pass pass:android --ks-key-alias androiddebugkey`), **NOT** `android/opende-release.jks`.
  - Required certificate SHA-256: `f4386ba9642d015cc2d2bfaa2ebe76d682b9c068444e5008b0ad426b0118104f`.
  - Breaking this signature breaks in-app GitHub OTA auto-updates for existing users on `v0.5` / `v0.6` / `v0.6.1`.
- **Versioning:** Bump `appVersionCode` and `appVersionName` in `android/app/build.gradle` before release builds (current: `0.6.1-beta`, `versionCode=8`).

## 5. Android Architecture & Performance Invariants (Do Not Regress!)
When porting upstream `jarmitage99/OpenDE` commits, preserve these Android fork optimizations and design choices:

### Renderer & OpenGL ES 3 (`src/render/`)
- **Visual fidelity rule:** Optimizations must maintain original graphical fidelity (no visual shortcuts that degrade the look).
- **`GLVertex` packing:** Packed to 40 bytes (`uint8_t` normalized color/fog, `int8_t` normalized normal, `mediump` varyings).
- **Streaming & batching:** 3-slot VBO/EBO ring buffers, direct mapped VBO uploads, single-map EBO upload for opaque BSP passes on Android, batched decals/brushes/translucent draws.
- **Shader state caching (`src/render/gl_shader.cc`):** Per-program `dirtyMask`, culling cache, and `UberShader::activeGamma()` which bypasses gamma (`1.0f`) **only** for `BlendMode::Modulated` — `BlendMode::Translucent` **must** apply `state.gamma` so dark translucent surfaces (like `drtywater_a`) match desktop OpenDE and `DeusEx.exe`.
- **Mirrors (`src/render/scene_renderer.cc`):**
  - `#ifdef __ANDROID__` uses `MaxFrameRecursion = 2` (desktop: `4`).
  - `SceneRenderer::drawMirrors` returns `bool` (`drewAny`) so `drawScene` only clears depth when a mirror actually rendered.
  - Use `actor->hasClassBit(CLS_Mover)` instead of `actor->isA(nameMover)` for fast bitmask dispatch.
  - **Z-fighting guard (Adreno/Mali):** Keep `invariant gl_Position;` in `vertexSource` (`src/render/gl_shader.cc`) AND wrap all mirror depth-only pre-passes (`drawLevelGeometry` and `drawMoverMirrorDepth`) with `device->setPolygonOffset(true, 1.0f, 1.0f)` / `(false, 0.0f, 0.0f)`. Without both, any shader/uniform change (like `MaxDynamicLights` 8→32) can trigger 1-ULP `GL_LEQUAL` flickering on `PF_Mirrored | PF_Translucent` floors.
- **Occlusion & async passes:** `OccNode`/`occPoints`, `SpanBuffer` 256×128 + `rowOpenMask` bitmask, span-tile mesh occlusion culling (`actorOccludedBySpanTiles`), and parallel `worker.run` for `collectLights` + `collectDynamicLights` alongside `occludeView`.
- **Translucent BSP backface culling (`scene_renderer.cc`):**
  - Never CPU-cull `PF_Mirrored` translucent surfaces (`singleSided = (batch.polyFlags & (PF_TwoSided | PF_Mirrored)) == 0`).
  - Never compute the normal from only the first triangle `(v0, v1, v2)` — BSP fans (e.g. `node=1026` in `00_Training.dx`) can begin with a collinear sliver whose cross product flips sign from float noise. Always accumulate `n += (b - a).cross(c - a)` across **all** triangles in `[first, first + count)` and require `nLenSq > 1.0f` before testing `d < 0.0f && d * d > nLenSq`.
- **Underwater / Screen Flash (`EndFlash`):** `SceneRenderer::drawScreenFlash(FlashScale, FlashFog)` runs in `src/game/main.cc` between `PostRenderFlash` and `PostRender` using `BlendMode::Highlight` (`GL_ONE, GL_ONE_MINUS_SRC_ALPHA`) and vertex color `(fogR, fogG, fogB, 1.0f - scale)`.
- **Performance HUD:** Built-in `PerfHUD` (`0=Off`, `1=Compact`, `2=Full Debug`, via Extra Graphics or `perfhud 0|1|2`).

### Engine, VM & Simulation (`src/core/`, `src/engine/`, `src/deusex/`)
- **VM script override hook:** `vm::registerScriptOverride(className, funcName, fn)` + `vm::findScriptOverride(UFunction*)` invoked in `UFunction::linkFunction()` (`src/core/object.cc`), promoting overridden script functions to `FUNC_Native` at link time with zero per-call overhead.
- **High-FPS `ViewFlash` fix:** `PlayerPawn.ViewFlash` (`src/engine/natives_misc.cc`) and `DeusExPlayer.ViewFlash` (`src/deusex/natives_deusex.cc`) are overridden natively so deadzone snapping (`FlashScale.x > 0.981f`, `FlashFog.{x,y,z} < 0.019f`) **only** triggers when the target (`goalScale` / `goalFog`) is actually at neutral. Without this, entering water at 60–120+ FPS never escapes the deadzone.
- **Threading & thermals:** Main and worker threads pinned to Android big cores; thermal pacing enabled to prevent Retroid overheating.
- **Visibility / AI:** Prevent pre-tick and occluded `LastRenderTime` updates on Android (fixes #18); direct `UObject::collisionSlot` + stationary brush cache; distant unseen actor/inventory/FlockPawn throttling.

### UI, Launcher, Controls & Localization
- **Unified SDL3 DXLauncher (`src/launcher/opende_launcher.cc`):** Replaced legacy Java UI; includes interactive 3D controller preview & button remapper (`User.ini`), GOG installer extraction, mod/profile management, save preview cards, and GitHub OTA auto-update.
- **Ultrawide (`> 16:9`) support:** `extendedVirtualWidth()` removes the 16:9 cap so `DeusExHUD` spans full ultrawide displays (20:9 / 21:9+) while keeping menus/Persona modals centered; DXLauncher expands up to `1680 dp`.
- **Touch & Gamepad:** Toggleable touch overlay (with dedicated Security Pad Exit button), `SDLK_AC_BACK` mapped to Escape, gyro aiming, landscape lock, and `DXController.u` gamepad support.
- **Localization:** Live in-game language switching including PT-BR (`DeusExText` / `DeusExConText` + INI reload healing).

## 6. Known Regression Traps
- **`00_Intro.dx` Coplanar Floor Mover (`z = -128`):**
  - In the opening reflection scene (Bob Page & Walton Simons), there is a `Mover` at `(-48, -3056, -144)` to `(48, -2736, -128)` whose top face (`z = -128`) is **coplanar** with the BSP mirrored floor and has `PF_Translucent` + lightmap (and/or `PF_Mirrored`).
  - Previously caused a flickering black block via `drawGroup`'s 2-pass `BlendMode::Modulated` lightmap.
  - Also watch out for `moverMirrors` (`collectMoverMirrors` / `drawMoverMirrorDepth` / `drawMirrors`) colliding/Z-fighting with coplanar BSP mirrors at `z = -128`.
- **High-FPS UE1 Script Deadzones:** Any UE1 script that snaps an exponentially interpolated value to `0`/`1` without checking its target will stall permanently at 120 FPS (like `ViewFlash` did).
- **INI Bloat / Reload:** Keep INI section clearing on reload and bloated INI healing intact (`7d00d30`).
- **Collinear BSP Slivers on Translucent Floors (`00_Training.dx`):** In the UNATCO Training entrance corridor, the right-side marble floor polygon (`node=1026`) starts with a nearly collinear triangle; single-triangle normal culling silently drops the entire floor strip under the right columns, exposing raw unblended reflections.

## 7. Upstream Porting Checklist (`jarmitage99/OpenDE` → Android)
Whenever porting commits from `jarmitage99/OpenDE` that touch `src/render/`, `src/engine/`, or `src/deusex/`:
1. **Preserve Android invariants before applying:** Check `invariant gl_Position`, mirror depth `setPolygonOffset(true, 1.0f, 1.0f)`, `UberShader::activeGamma()` (gamma active on `BlendMode::Translucent`), `singleSided` translucent area-sum normal + `PF_Mirrored` exclusion, `CLS_Mover`, `bool drawMirrors`, `MaxFrameRecursion = 2`, 40B `GLVertex`, and `-DSKIP_UT=ON`.
2. **Build with `-Werror` clean:** Run `.\gradlew.bat assembleDebug` and install on Retroid (`MC94528AYF122800013`).
3. **Visual regression smoke test (when `src/render/` changed):** Capture screenshots via `cmd /c "adb -s MC94528AYF122800013 exec-out screencap -p > ..."` and inspect via `read_file` on:
   - **`00_Intro.dx`** (Bob Page & Walton Simons floor reflection): verify zero flickering black blocks/stripes.
   - **`00_Training.dx`** (entrance hall columns): verify symmetric marble-blended reflections under both left and right columns.
   - **Water / underwater transition** (e.g. `01_NYC_UNATCOIsland` dock): verify water surface gamma, no missing underwater triangles, and `ViewFlash` blue tint working at 60–120 FPS on re-entry.
