#pragma once

#include <string>

namespace game {

    enum class LauncherAction {
        Quit,
        Play
    };

    struct LauncherParams {
        std::string gameRoot;
        std::string fontsDir;
        std::string initialMod;
        std::string versionLabel = "0.6-beta";
    };

    struct LauncherOutcome {
        LauncherAction action = LauncherAction::Quit;
        std::string gameRoot;
        std::string modId;
        std::string startUrl;
        bool returnAfterPlaying = true;
    };

    // Runs the SDL3 + SDL3_ttf DXLauncher UI and returns the chosen action.
    // Creates and destroys its own SDL_Window / SDL_Renderer cleanly so the game
    // can initialize its OpenGL ES context afterwards.
    LauncherOutcome runDXLauncher(const LauncherParams& params);

} // namespace game
