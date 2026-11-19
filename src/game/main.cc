#include "audio/audio.h"
#include "core/config.h"
#include "core/objects.h"
#include "core/prof_clock.h"
#include "core/url.h"
#include "core/vm.h"
#include "deusex/deusex.h"
#include "engine/actor.h"
#include "engine/canvas_hooks.h"
#include "engine/console.h"
#include "engine/input.h"
#include "engine/resources.h"
#include "engine/savegame.h"
#include "engine/world.h"
#include "extension/extension.h"
#include "game/perf_overlay.h"
#include "game/touch_overlay.h"
#include "render/lighting.h"
#include "render/scene_renderer.h"

#include <SDL.h>
#include <glad/gl.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>
#endif
#ifdef __ANDROID__
#include <sys/stat.h>
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {
#ifdef __EMSCRIPTEN__
    constexpr bool browser = true;
#else
    constexpr bool browser = false;
#endif

    uint8_t translateKey(SDL_Keycode key)
    {
        if (key >= SDLK_a && key <= SDLK_z) {
            return static_cast<uint8_t>(dx::IK_A + (key - SDLK_a));
        }
        if (key >= SDLK_0 && key <= SDLK_9) {
            return static_cast<uint8_t>(dx::IK_0 + (key - SDLK_0));
        }
        if (key >= SDLK_F1 && key <= SDLK_F12) {
            return static_cast<uint8_t>(dx::IK_F1 + (key - SDLK_F1));
        }
        if (key >= SDLK_KP_1 && key <= SDLK_KP_9) {
            return static_cast<uint8_t>(dx::IK_NumPad0 + 1 + (key - SDLK_KP_1));
        }
        switch (key) {
        case SDLK_KP_0:
            return dx::IK_NumPad0;
        case SDLK_BACKSPACE:
            return dx::IK_Backspace;
        case SDLK_TAB:
            return dx::IK_Tab;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
        case SDLK_SELECT:
            return dx::IK_Enter;
        case SDLK_LSHIFT:
        case SDLK_RSHIFT:
            return dx::IK_Shift;
        case SDLK_LCTRL:
        case SDLK_RCTRL:
            return dx::IK_Ctrl;
        case SDLK_LALT:
        case SDLK_RALT:
            return dx::IK_Alt;
        case SDLK_PAUSE:
            return dx::IK_Pause;
        case SDLK_CAPSLOCK:
            return dx::IK_CapsLock;
        case SDLK_ESCAPE:
            return dx::IK_Escape;
        case SDLK_SPACE:
            return dx::IK_Space;
        case SDLK_PAGEUP:
            return dx::IK_PageUp;
        case SDLK_PAGEDOWN:
            return dx::IK_PageDown;
        case SDLK_END:
            return dx::IK_End;
        case SDLK_HOME:
            return dx::IK_Home;
        case SDLK_LEFT:
            return dx::IK_Left;
        case SDLK_UP:
            return dx::IK_Up;
        case SDLK_RIGHT:
            return dx::IK_Right;
        case SDLK_DOWN:
            return dx::IK_Down;
        case SDLK_INSERT:
            return dx::IK_Insert;
        case SDLK_DELETE:
            return dx::IK_Delete;
        case SDLK_KP_MULTIPLY:
            return dx::IK_GreyStar;
        case SDLK_KP_PLUS:
            return dx::IK_GreyPlus;
        case SDLK_KP_MINUS:
            return dx::IK_GreyMinus;
        case SDLK_KP_PERIOD:
            return dx::IK_NumPadPeriod;
        case SDLK_KP_DIVIDE:
            return dx::IK_GreySlash;
        case SDLK_NUMLOCKCLEAR:
            return dx::IK_NumLock;
        case SDLK_SCROLLLOCK:
            return dx::IK_ScrollLock;
        case SDLK_SEMICOLON:
            return dx::IK_Semicolon;
        case SDLK_EQUALS:
            return dx::IK_Equals;
        case SDLK_COMMA:
            return dx::IK_Comma;
        case SDLK_MINUS:
            return dx::IK_Minus;
        case SDLK_PERIOD:
            return dx::IK_Period;
        case SDLK_SLASH:
            return dx::IK_Slash;
        case SDLK_BACKQUOTE:
            return dx::IK_Tilde;
        case SDLK_LEFTBRACKET:
            return dx::IK_LeftBracket;
        case SDLK_BACKSLASH:
            return dx::IK_Backslash;
        case SDLK_RIGHTBRACKET:
            return dx::IK_RightBracket;
        case SDLK_QUOTE:
            return dx::IK_SingleQuote;
        default:
            return dx::IK_None;
        }
    }

    uint8_t translateButton(uint8_t button)
    {
        switch (button) {
        case SDL_BUTTON_LEFT:
            return dx::IK_LeftMouse;
        case SDL_BUTTON_RIGHT:
            return dx::IK_RightMouse;
        case SDL_BUTTON_MIDDLE:
            return dx::IK_MiddleMouse;
        default:
            return dx::IK_None;
        }
    }

    struct Options {
        std::string map;
        std::string screenshot;
        std::string ini;
        std::string userIni;
        std::string mod;
        int frames = -1;
        int width = 1280;
        int height = 720;
        bool sizeGiven = false;
        std::vector<std::string> commands;
        std::vector<std::pair<int, std::string>> actions;
    };

    Options parseOptions(int argc, char** argv)
    {
        Options options;
        for (int i = 1; i < argc; i++) {
            std::string arg = argv[i];
            if (arg == "--screenshot" && i + 1 < argc) {
                options.screenshot = argv[++i];
            } else if (arg == "--frames" && i + 1 < argc) {
                options.frames = std::atoi(argv[++i]);
            } else if (arg == "--width" && i + 1 < argc) {
                options.width = std::atoi(argv[++i]);
                options.sizeGiven = true;
            } else if (arg == "--height" && i + 1 < argc) {
                options.height = std::atoi(argv[++i]);
                options.sizeGiven = true;
            } else if (arg == "--ini" && i + 1 < argc) {
                options.ini = argv[++i];
            } else if (arg == "--userini" && i + 1 < argc) {
                options.userIni = argv[++i];
            } else if (arg == "--mod" && i + 1 < argc) {
                options.mod = argv[++i];
            } else if (arg == "--exec" && i + 1 < argc) {
                options.commands.push_back(argv[++i]);
            } else if (arg == "--at" && i + 1 < argc) {
                std::string spec = argv[++i];
                size_t colon = spec.find(':');
                if (colon != std::string::npos) {
                    options.actions.emplace_back(std::atoi(spec.substr(0, colon).c_str()), spec.substr(colon + 1));
                }
            } else {
                options.map = arg;
            }
        }
        return options;
    }

    void saveBmp(const std::string& path, const std::vector<uint8_t>& rgba, int width, int height)
    {
        std::vector<uint8_t> pixels = rgba;
        SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormatFrom(pixels.data(), width, height, 32, width * 4, SDL_PIXELFORMAT_ABGR8888);
        SDL_SaveBMP(surface, path.c_str());
        SDL_FreeSurface(surface);
    }

    class Game {
    public:
        Game(SDL_Window* target, const Options& settings)
            : window(target)
            , options(settings)
        {
        }

        ~Game()
        {
            closeController();
        }

        std::string videoSection()
        {
            return dx::config::system().get("Engine.Engine", "ViewportManager").value_or("WinDrv.WindowsClient");
        }

        int videoSetting(const char* key, int fallback)
        {
            std::optional<std::string> value = dx::config::system().get(videoSection(), key);
            if (!value) {
                return fallback;
            }
            if (*value == "True" || *value == "true" || *value == "Verdadeiro" || *value == "verdadeiro") {
                return 1;
            }
            if (*value == "False" || *value == "false" || *value == "Falso" || *value == "falso") {
                return 0;
            }
            return std::atoi(value->c_str());
        }

        void saveVideoMode(bool fullscreen, int width, int height)
        {
            dx::Ini& ini = dx::config::system();
            std::string section = videoSection();
            ini.set(section, fullscreen ? "FullscreenViewportX" : "WindowedViewportX", std::to_string(width));
            ini.set(section, fullscreen ? "FullscreenViewportY" : "WindowedViewportY", std::to_string(height));
            ini.set(section, "StartupFullscreen", fullscreen ? "True" : "False");
            ini.save();
        }

        void applyVideoMode(bool fullscreen, int width, int height)
        {
            if (fullscreen && browser) {
                SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);
            } else if (fullscreen) {
                int display = SDL_GetWindowDisplayIndex(window);
                SDL_DisplayMode wanted {};
                wanted.w = width;
                wanted.h = height;
                SDL_DisplayMode mode {};
                if (SDL_GetClosestDisplayMode(display, &wanted, &mode)) {
                    SDL_SetWindowDisplayMode(window, &mode);
                }
                SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN);
            } else {
                SDL_SetWindowFullscreen(window, 0);
                SDL_SetWindowSize(window, width, height);
            }
        }

        std::string availableResolutions()
        {
            int display = SDL_GetWindowDisplayIndex(window);
            std::vector<std::pair<int, int>> sizes;
            for (int i = 0; i < SDL_GetNumDisplayModes(display); i++) {
                SDL_DisplayMode mode {};
                if (SDL_GetDisplayMode(display, i, &mode) == 0 && mode.w >= 800 && mode.h >= 600) {
                    sizes.emplace_back(mode.w, mode.h);
                }
            }
            std::pair<int, int> current;
            SDL_GetWindowSize(window, &current.first, &current.second);
            sizes.push_back(current);
            std::sort(sizes.begin(), sizes.end());
            sizes.erase(std::unique(sizes.begin(), sizes.end()), sizes.end());
            while (sizes.size() > 16) {
                sizes.erase(sizes.front() == current ? sizes.begin() + 1 : sizes.begin());
            }
            std::string text;
            for (const auto& [width, height] : sizes) {
                if (!text.empty()) {
                    text += " ";
                }
                text += std::to_string(width) + "x" + std::to_string(height);
            }
            return text;
        }

        float displayFov(float fov, int width, int height)
        {
            int index = std::clamp(videoSetting("FieldOfViewIndex", 3), 0, 12);
            float wanted = 60.0f + 5.0f * static_cast<float>(index);
            float adjusted = fov + (wanted - 75.0f) * std::clamp((fov - 25.0f) / 50.0f, 0.0f, 1.0f);
            float aspect = static_cast<float>(width) / static_cast<float>(height) * 0.75f;
            return std::atan(std::tan(adjusted * dx::Pi / 360.0f) * aspect) * 360.0f / dx::Pi;
        }

        void loadPackageClasses(const std::string& package)
        {
            dx::Linker* linker = dx::objects::loadPackage(dx::Name(package));
            if (!linker) {
                return;
            }
            const dx::Package& file = linker->file();
            for (size_t i = 0; i < file.exports().size(); i++) {
                if (file.exports()[i].classRef == 0) {
                    dx::objects::ensureLoaded(linker->object(static_cast<int32_t>(i + 1)));
                }
            }
            dx::objects::flushLoads();
        }

        dx::UClass* extraGameplayScreen = nullptr;
        dx::UClass* extraGraphicsScreen = nullptr;
        dx::UClass* languageChoiceClass = nullptr;
        bool hasDxControllerMenu = false;

        void setStringProp(dx::UObject* obj, const char* propName, std::string_view utf8, int index = 0)
        {
            if (uint8_t* addr = obj ? obj->propertyAddress(dx::Name(propName), index) : nullptr) {
                *reinterpret_cast<std::string*>(addr) = dx::deusex::utf8ToCp1252(utf8);
            }
        }

        void syncExtraMenusLocalization()
        {
            const bool pt = dx::iequals(dx::config::language(), "ptb");
            if (extraGameplayScreen && extraGameplayScreen->defaultObject) {
                setStringProp(extraGameplayScreen->defaultObject, "Title", pt ? "Opções Extras de Jogo" : "Extra Gameplay Options");
            }
            if (extraGraphicsScreen && extraGraphicsScreen->defaultObject) {
                setStringProp(extraGraphicsScreen->defaultObject, "Title", pt ? "Opções Extras de Vídeo" : "Extra Graphics Options");
            }
            if (dx::UClass* uiWindow = dx::objects::findClass("DeusEx.MenuUIWindow")) {
                if (dx::UObject* uiDef = uiWindow->defaultObject) {
                    for (dx::UClass* extraScreen : { extraGameplayScreen, extraGraphicsScreen }) {
                        if (!extraScreen || !extraScreen->defaultObject) {
                            continue;
                        }
                        for (const char* propName : { "btnLabelOK", "btnLabelCancel", "btnLabelPrevious", "btnLabelResetToDefaults" }) {
                            uint8_t* dst = extraScreen->defaultObject->propertyAddress(dx::Name(propName));
                            uint8_t* src = uiDef->propertyAddress(dx::Name(propName));
                            if (dst && src) {
                                *reinterpret_cast<std::string*>(dst) = *reinterpret_cast<const std::string*>(src);
                            }
                        }
                    }
                }
            }

            struct ChoiceTextSpec {
                const char* classPath;
                const char* actionEn;
                const char* actionPt;
                const char* helpEn;
                const char* helpPt;
                std::vector<const char*> enumEn;
                std::vector<const char*> enumPt;
            };
            static const std::vector<ChoiceTextSpec> specs = {
                { "DeusEx.MenuChoice_Language", "|&Language", "I|&dioma",
                  "Game text, subtitles, menus, and controller interface language. Changes apply immediately.",
                  "Idioma dos textos, legendas, menus e interface do controle. A alteração é aplicada na hora.",
                  { "English", "Português (BR)" }, { "English", "Português (BR)" } },
                { "DeusEx.MenuChoice_PlayerSpeed", "Player Sp|&eed", "Velocidade do |&Jogador",
                  "Player movement speed in percent of normal speed.",
                  "Velocidade de movimento do jogador em porcentagem da velocidade normal.",
                  {}, {} },
                { "DeusEx.MenuChoice_TouchOverlaysEnabled", "|&Touch Overlays", "|&Overlays Touch",
                  "Shows configurable on-screen touch controls when you tap the screen, and hides them immediately when using a controller or keyboard. Off keeps gameplay touch disabled.",
                  "Exibe controles virtuais na tela ao tocar e os oculta na hora ao usar controle ou teclado. Desligado desativa o toque no jogo.",
                  { "Off", "On" }, { "Desl.", "Lig." } },
                { "DeusEx.MenuChoice_TouchOverlayOpacity", "Touch |&Opacity", "O|&pacidade do Touch",
                  "Transparency of the on-screen touch controls during gameplay.",
                  "Transparência dos controles virtuais na tela durante o jogo.",
                  { "25%", "40%", "55%", "75%", "95%" }, { "25%", "40%", "55%", "75%", "95%" } },
                { "DeusEx.MenuChoice_TouchOverlayScale", "|&Touch Button Size", "|&Tamanho do Touch",
                  "Global size multiplier applied to all on-screen touch buttons.",
                  "Multiplicador global de tamanho aplicado aos botões touch na tela.",
                  { "75%", "100%", "125%", "150%" }, { "75%", "100%", "125%", "150%" } },
                { "DeusEx.MenuChoice_TouchLookSensitivity", "Touch |&Look Speed", "|&Sensib. Câmera Touch",
                  "Camera rotation speed when dragging the right side of the screen.",
                  "Velocidade de rotação da câmera ao arrastar o dedo na tela.",
                  { "0.5x", "0.75x", "1.0x", "1.5x", "2.0x" }, { "0.5x", "0.75x", "1.0x", "1.5x", "2.0x" } },
                { "DeusEx.MenuChoice_TouchLayout", "Customize Touch |&Layout", "Editar |&Layout Touch",
                  "Opens the visual Touch Layout Editor to move, resize, show, or hide each on-screen control.",
                  "Abre o Editor de Layout Touch para mover, redimensionar, mostrar ou ocultar cada botão.",
                  { "Open Editor", "Open Editor" }, { "Abrir Editor", "Abrir Editor" } },
                { "DeusEx.MenuChoice_FieldOfView", "Field of |&View", "Campo de |&Visão",
                  "Horizontal field of view on a 4:3 screen. Wider screens show more at the sides.",
                  "Campo de visão horizontal em tela 4:3. Telas mais largas mostram mais nas laterais.",
                  {}, {} },
                { "DeusEx.MenuChoice_Anisotropy", "|&Anisotropic Filtering", "Filtragem |&Anisotrópica",
                  "Sharpens textures seen at a steep angle, such as floors in the distance.",
                  "Deixa texturas vistas em ângulos inclinados mais nítidas, como pisos à distância.",
                  { "Off", "2x", "4x", "8x", "16x" }, { "Desl.", "2x", "4x", "8x", "16x" } },
                { "DeusEx.MenuChoice_Trilinear", "|&Trilinear Filtering", "Filtragem |&Trilinear",
                  "Blends between texture detail levels to remove visible bands on floors and walls.",
                  "Suaviza a transição entre níveis de detalhe para remover faixas visíveis em pisos e paredes.",
                  { "Off", "On" }, { "Desl.", "Lig." } },
                { "DeusEx.MenuChoice_Multisampling", "Multi|&sample AA", "Anti-Aliasing |&MSAA",
                  "Smooths the edges of world geometry. Higher values cost more performance.",
                  "Suaviza as bordas da geometria do cenário. Valores mais altos reduzem o desempenho.",
                  { "Off", "2x", "4x", "8x" }, { "Desl.", "2x", "4x", "8x" } },
                { "DeusEx.MenuChoice_FXAA", "|&FXAA", "|&FXAA",
                  "Fast edge smoothing of the 3D view that also covers masked textures such as fences.",
                  "Suavização rápida de bordas na visão 3D que também cobre texturas vazadas, como grades.",
                  { "Off", "On" }, { "Desl.", "Lig." } },
                { "DeusEx.MenuChoice_SmoothLighting", "Smooth |&Lighting", "Iluminação |&Suave",
                  "Filters lightmaps with a bicubic filter to soften blocky shadow edges.",
                  "Aplica filtro bicúbico nos mapas de luz para suavizar bordas quadradas de sombras.",
                  { "Off", "On" }, { "Desl.", "Lig." } },
                { "DeusEx.MenuChoice_DetailDistance", "|&Detail Distance", "Distância de |&Detalhe",
                  "How far away detail textures stay visible. Normal matches the original game.",
                  "Até que distância as texturas de detalhe ficam visíveis. Normal corresponde ao jogo original.",
                  { "Normal", "Far", "Very Far" }, { "Normal", "Longe", "Muito Longe" } },
                { "DeusEx.MenuChoice_VSync", "|&Vertical Sync", "Sincronização |&Vertical",
                  "Waits for the display refresh. Adaptive skips the wait when a frame is late.",
                  "Aguarda a atualização da tela. Adaptativo evita a espera quando um quadro atrasa.",
                  { "Off", "On", "Adaptive" }, { "Desl.", "Lig.", "Adaptativo" } },
                { "DeusEx.MenuChoice_FrameRateLimit", "Frame Rate L|&imit", "L|&imite de Quadros",
                  "Caps the frame rate to save power and reduce heat.",
                  "Limita a taxa de quadros para economizar bateria e reduzir o aquecimento.",
                  { "Off", "30", "60", "75", "120", "144", "165", "240" }, { "Desl.", "30", "60", "75", "120", "144", "165", "240" } },
                { "DeusEx.MenuChoice_LimitTextureAnimation", "Limit Texture A|&nimation", "Limitar Anim. |&Textura",
                  "Keeps water and fire textures at 60 updates per second. Off matches the original game.",
                  "Mantém texturas de água e fogo a 60 atualizações por segundo. Desligado segue o original.",
                  { "Off", "On" }, { "Desl.", "Lig." } },
                { "DeusEx.MenuChoice_AmbientOcclusion", "Ambient |&Occlusion", "Oclusão de |&Ambiente",
                  "Adds soft contact shadows under characters and objects. Higher levels are smoother and cost more performance. Off matches the original game.",
                  "Adiciona sombras suaves de contato sob personagens e objetos. Níveis maiores reduzem o desempenho.",
                  { "Off", "Low", "Medium", "High" }, { "Desl.", "Baixo", "Médio", "Alto" } },
                { "DeusEx.MenuChoice_UIScale", "|&UI Scale", "Escala da |&Interface",
                  "Size of menus and the HUD. Auto keeps the whole interface on screen. Larger scales can push long conversations and some menus off the screen.",
                  "Tamanho dos menus e do HUD. Auto mantém toda a interface na tela.",
                  { "Auto", "1x", "2x", "3x", "4x" }, { "Auto", "1x", "2x", "3x", "4x" } },
                { "DeusEx.MenuChoice_PerformanceHUD", "Performance |&HUD", "HUD de |&Desempenho",
                  "Shows an on-screen FPS counter, frame time graph, and real-time CPU/GPU render stats.",
                  "Exibe contador de FPS, gráfico de frame time e estatísticas de CPU/GPU em tempo real na tela.",
                  { "Off", "Compact", "Full Debug" }, { "Desl.", "Compacto", "Debug Completo" } },
            };

            for (const ChoiceTextSpec& spec : specs) {
                dx::UClass* cls = dx::objects::findClass(spec.classPath);
                if (!cls || !cls->defaultObject) {
                    continue;
                }
                dx::UObject* def = cls->defaultObject;
                setStringProp(def, "actionText", pt ? spec.actionPt : spec.actionEn);
                setStringProp(def, "helpText", pt ? spec.helpPt : spec.helpEn);
                const auto& enums = pt ? spec.enumPt : spec.enumEn;
                for (size_t i = 0; i < enums.size(); i++) {
                    setStringProp(def, "enumText", enums[i], static_cast<int>(i));
                }
            }
            if (dx::UClass* choiceBase = dx::objects::findClass("DeusEx.MenuUIChoice"); choiceBase && choiceBase->defaultObject) {
                setStringProp(choiceBase->defaultObject, "FalseTrue", "False", 0);
                setStringProp(choiceBase->defaultObject, "FalseTrue", "True", 1);
            }
        }

        dx::UClass* createExtraOptionsScreen(const char* name, const char* title)
        {
            dx::UClass* screen = dx::objects::createSubclass(dx::objects::findClass("DeusEx.MenuScreenOptions"), dx::Name(name));
            dx::UObject* defaults = screen->defaultObject;
            *reinterpret_cast<std::string*>(defaults->propertyAddress(dx::Name("Title"))) = title;
            for (int i = 0; i < screen->findProperty(dx::Name("choices"))->arrayDim; i++) {
                *reinterpret_cast<dx::UObject**>(defaults->propertyAddress(dx::Name("choices"), i)) = nullptr;
            }
            return screen;
        }

        dx::UClass* addChoice(dx::UClass* screen, int index, const char* parentPath, const char* name, const char* action, const char* help, const char* key, const std::vector<const char*>& values, int fallback, const std::string& seed)
        {
            dx::UClass* choice = dx::objects::createSubclass(dx::objects::findClass(parentPath), dx::Name(name));
            dx::UObject* defaults = choice->defaultObject;
            *reinterpret_cast<std::string*>(defaults->propertyAddress(dx::Name("actionText"))) = action;
            *reinterpret_cast<std::string*>(defaults->propertyAddress(dx::Name("helpText"))) = help;
            *reinterpret_cast<std::string*>(defaults->propertyAddress(dx::Name("configSetting"))) = key[0] ? (std::string("ini:Engine.Engine.ViewportManager ") + key) : std::string();
            *reinterpret_cast<int32_t*>(defaults->propertyAddress(dx::Name("defaultValue"))) = fallback;
            *reinterpret_cast<int32_t*>(defaults->propertyAddress(dx::Name("defaultInfoWidth"))) = 88;
            *reinterpret_cast<int32_t*>(defaults->propertyAddress(dx::Name("defaultInfoPosX"))) = 270;
            for (size_t i = 0; i < 40; i++) {
                *reinterpret_cast<std::string*>(defaults->propertyAddress(dx::Name("enumText"), static_cast<int32_t>(i))) = i < values.size() ? values[i] : "";
            }
            *reinterpret_cast<dx::UObject**>(screen->defaultObject->propertyAddress(dx::Name("choices"), index)) = choice;
            if (key[0] && !dx::config::system().get(videoSection(), key)) {
                dx::config::system().set(videoSection(), key, seed);
            }
            return choice;
        }

        void addEnumChoice(dx::UClass* screen, int index, const char* name, const char* action, const char* help, const char* key, const std::vector<const char*>& values, int fallback)
        {
            addChoice(screen, index, "DeusEx.MenuUIChoiceEnum", name, action, help, key, values, fallback, std::to_string(fallback));
        }

        void addSwitchChoice(dx::UClass* screen, int index, const char* name, const char* action, const char* help, const char* key, bool fallback)
        {
            addChoice(screen, index, "DeusEx.MenuChoice_DetailTextures", name, action, help, key, { "Off", "On" }, fallback ? 1 : 0, fallback ? "True" : "False");
        }

        void applyLiveLanguageChange(std::string_view newLanguage)
        {
            dx::config::setLanguage(newLanguage);
            dx::deusex::reloadWorldLocalization(world.get());
            syncExtraMenusLocalization();
            dx::UObject* settings = dx::objects::findClass("DeusEx.MenuSettings")->defaultObject;
            if (hasDxControllerMenu) {
                dx::deusex::refreshControllerSettingsMenu(settings, extraGameplayScreen, extraGraphicsScreen);
            } else if (settings) {
                const bool pt = dx::iequals(dx::config::language(), "ptb");
                setStringProp(settings, "ButtonNames", pt ? "Jogabilidade Extra" : "Extra Gameplay", 3);
                setStringProp(settings, "ButtonNames", pt ? "Gráficos Extras" : "Extra Graphics", 5);
            }
            dx::deusex::refreshOpenMenus();
        }

        void addExtraOptions()
        {
            hasDxControllerMenu = dx::objects::findClass("DXController.ControllerConsole") != nullptr;
            dx::UClass* gameplay = createExtraOptionsScreen("MenuScreenExtraGameplayOptions", "Extra Gameplay Options");
            dx::UClass* screen = createExtraOptionsScreen("MenuScreenExtraGraphicsOptions", "Extra Graphics Options");
            extraGameplayScreen = gameplay;
            extraGraphicsScreen = screen;
            std::string trilinearStr = dx::config::system().get("OpenGLDrv.OpenGLRenderDevice", "UseTrilinear").value_or("False");
            bool trilinear = trilinearStr == "True" || trilinearStr == "true" || trilinearStr == "Verdadeiro" || trilinearStr == "verdadeiro";
            addEnumChoice(screen, 0, "MenuChoice_Anisotropy", "|&Anisotropic Filtering", "Sharpens textures seen at a steep angle, such as floors in the distance.", "Anisotropy", { "Off", "2x", "4x", "8x", "16x" }, 0);
            addSwitchChoice(screen, 1, "MenuChoice_Trilinear", "|&Trilinear Filtering", "Blends between texture detail levels to remove visible bands on floors and walls.", "UseTrilinear", trilinear);
            addEnumChoice(screen, 2, "MenuChoice_Multisampling", "Multi|&sample AA", "Smooths the edges of world geometry. Higher values cost more performance.", "Multisampling", { "Off", "2x", "4x", "8x" }, 0);
            addSwitchChoice(screen, 3, "MenuChoice_FXAA", "|&FXAA", "Fast edge smoothing of the 3D view that also covers masked textures such as fences.", "FXAA", false);
            addSwitchChoice(screen, 4, "MenuChoice_SmoothLighting", "Smooth |&Lighting", "Filters lightmaps with a bicubic filter to soften blocky shadow edges.", "SmoothLightmaps", false);
            addEnumChoice(screen, 5, "MenuChoice_DetailDistance", "|&Detail Distance", "How far away detail textures stay visible. Normal matches the original game.", "DetailDistance", { "Normal", "Far", "Very Far" }, 0);
            int next = 6;
            if (!browser) {
                addEnumChoice(screen, 6, "MenuChoice_VSync", "|&Vertical Sync", "Waits for the display refresh. Adaptive skips the wait when a frame is late.", "VSync", { "Off", "On", "Adaptive" }, 1);
                addEnumChoice(screen, 7, "MenuChoice_FrameRateLimit", "Frame Rate L|&imit", "Caps the frame rate to save power and reduce heat.", "FrameRateLimit", { "Off", "30", "60", "75", "120", "144", "165", "240" }, 0);
                next = 8;
            }
            addSwitchChoice(screen, next, "MenuChoice_LimitTextureAnimation", "Limit Texture A|&nimation", "Keeps water and fire textures at 60 updates per second. Off matches the original game.", "LimitTextureAnimation", true);
            addEnumChoice(screen, next + 1, "MenuChoice_AmbientOcclusion", "Ambient |&Occlusion", "Adds soft contact shadows under characters and objects. Higher levels are smoother and cost more performance. Off matches the original game.", "AmbientOcclusion", { "Off", "Low", "Medium", "High" }, 0);
            addEnumChoice(screen, next + 2, "MenuChoice_UIScale", "|&UI Scale", "Size of menus and the HUD. Auto keeps the whole interface on screen. Larger scales can push long conversations and some menus off the screen.", "UIScale", { "Auto", "1x", "1.25x", "1.5x", "1.75x", "2x", "2.25x", "2.5x", "2.75x", "3x", "3.5x", "4x" }, 0);
            addEnumChoice(screen, next + 3, "MenuChoice_PerformanceHUD", "Performance |&HUD", "Shows an on-screen FPS counter, frame time graph, and real-time CPU/GPU render stats.", "PerformanceHUD", { "Off", "Compact", "Full Debug" }, 0);
            dx::UObject* graphicsDefaults = screen->defaultObject;
            int gap = *reinterpret_cast<int32_t*>(graphicsDefaults->propertyAddress(dx::Name("choiceVerticalGap")));
            int sliderRow = *reinterpret_cast<int32_t*>(graphicsDefaults->propertyAddress(dx::Name("choiceStartY"))) + 8 * gap;
            int32_t& clientHeight = *reinterpret_cast<int32_t*>(graphicsDefaults->propertyAddress(dx::Name("ClientHeight")));
            int extraRows = browser ? 1 : 3;
            dx::deusex::spliceMenuBackground(graphicsDefaults, clientHeight, *reinterpret_cast<int32_t*>(graphicsDefaults->propertyAddress(dx::Name("textureRows"))) * 256);
            dx::deusex::spliceMenuBackground(graphicsDefaults, sliderRow, sliderRow + gap);
            dx::deusex::spliceMenuBackground(graphicsDefaults, sliderRow, sliderRow - gap);
            for (int i = 0; i < extraRows; i++) {
                dx::deusex::spliceMenuBackground(graphicsDefaults, sliderRow + gap, sliderRow);
            }
            clientHeight += extraRows * gap;
            *reinterpret_cast<int32_t*>(graphicsDefaults->propertyAddress(dx::Name("helpPosY"))) += extraRows * gap;

            languageChoiceClass = addChoice(
                gameplay, 0, "DeusEx.MenuUIChoiceEnum", "MenuChoice_Language", "|&Language",
                "Game text, subtitles, menus, and controller interface language. Changes apply immediately.",
                "", { "English", "Português (BR)" }, 0, "0");
            if (languageChoiceClass && languageChoiceClass->defaultObject) {
                *reinterpret_cast<int32_t*>(languageChoiceClass->defaultObject->propertyAddress(dx::Name("defaultInfoWidth"))) = 140;
            }
            dx::deusex::configureLanguageChoice(languageChoiceClass, [this](std::string_view lang) {
                applyLiveLanguageChange(lang);
            });
            addPlayerSpeedChoice(gameplay, 1);
#ifdef __ANDROID__
            constexpr bool defaultTouchOverlays = true;
#else
            constexpr bool defaultTouchOverlays = false;
#endif
            addEnumChoice(gameplay, 2, "MenuChoice_TouchOverlaysEnabled", "|&Touch Overlays",
                "Shows configurable on-screen touch controls when you tap the screen, and hides them immediately when using a controller or keyboard. Off keeps gameplay touch disabled.",
                "TouchOverlaysEnabled", { "Off", "On" }, defaultTouchOverlays ? 1 : 0);
            addEnumChoice(gameplay, 3, "MenuChoice_TouchOverlayOpacity", "Touch |&Opacity",
                "Transparency of the on-screen touch controls during gameplay.",
                "TouchOverlayOpacity", { "25%", "40%", "55%", "75%", "95%" }, 2);
            addEnumChoice(gameplay, 4, "MenuChoice_TouchOverlayScale", "|&Touch Button Size",
                "Global size multiplier applied to all on-screen touch buttons.",
                "TouchOverlayScale", { "75%", "100%", "125%", "150%" }, 1);
            addEnumChoice(gameplay, 5, "MenuChoice_TouchLookSensitivity", "Touch |&Look Speed",
                "Camera rotation speed when dragging the right side of the screen.",
                "TouchLookSensitivity", { "0.5x", "0.75x", "1.0x", "1.5x", "2.0x" }, 2);
            touchEditorChoiceClass = addChoice(
                gameplay, 6, "DeusEx.MenuUIChoiceEnum", "MenuChoice_TouchLayout", "Customize Touch |&Layout",
                "Opens the visual Touch Layout Editor to move, resize, show, or hide each on-screen control.",
                "", { "Open Editor", "Open Editor" }, 0, "0");
            if (touchEditorChoiceClass && touchEditorChoiceClass->defaultObject) {
                *reinterpret_cast<int32_t*>(touchEditorChoiceClass->defaultObject->propertyAddress(dx::Name("defaultInfoWidth"))) = 140;
            }
            dx::deusex::configureTouchEditorChoice(touchEditorChoiceClass, [this]() {
                touchOverlay.openEditor();
            });
            addFixVanillaBugsChoice(gameplay, 7);

            dx::UObject* gameplayDefaults = gameplay->defaultObject;
            int gameplayGap = *reinterpret_cast<int32_t*>(gameplayDefaults->propertyAddress(dx::Name("choiceVerticalGap")));
            int gameplayCut = 288 - 7 * gameplayGap;
            if (gameplayCut > 0) {
                dx::deusex::spliceMenuBackground(gameplayDefaults, 55, 55 + gameplayCut);
                *reinterpret_cast<int32_t*>(gameplayDefaults->propertyAddress(dx::Name("ClientHeight"))) -= gameplayCut;
                *reinterpret_cast<int32_t*>(gameplayDefaults->propertyAddress(dx::Name("helpPosY"))) -= gameplayCut;
            }

            syncExtraMenusLocalization();
            if (hasDxControllerMenu) {
                dx::UObject* settings = dx::objects::findClass("DeusEx.MenuSettings")->defaultObject;
                dx::deusex::configureControllerSettingsMenu(settings, gameplay, screen);
            } else {
                const bool pt = dx::iequals(dx::config::language(), "ptb");
                dx::UClass* settingsClass = dx::objects::findClass("DeusEx.MenuSettings");
                std::vector<dx::UClass*> menus;
                for (dx::UObject* object : dx::objects::all()) {
                    dx::UClass* cls = dx::cast<dx::UClass>(object);
                    if (cls && cls->defaultObject && cls->isChildOf(settingsClass)) {
                        menus.push_back(cls);
                    }
                }
                for (dx::UClass* menu : menus) {
                    dx::deusex::insertMenuButton(menu->defaultObject, 3, dx::deusex::utf8ToCp1252(pt ? "Jogabilidade Extra" : "Extra Gameplay"), gameplay);
                    dx::deusex::insertMenuButton(menu->defaultObject, 5, dx::deusex::utf8ToCp1252(pt ? "Gráficos Extras" : "Extra Graphics"), screen);
                }
            }
        }

        void addPlayerSpeedChoice(dx::UClass* screen, int index)
        {
            dx::UClass* choice = dx::objects::createSubclass(dx::objects::findClass("DeusEx.MenuUIChoiceSlider"), dx::Name("MenuChoice_PlayerSpeed"));
            *reinterpret_cast<dx::UObject**>(screen->defaultObject->propertyAddress(dx::Name("choices"), index)) = choice;
            dx::UObject* defaults = choice->defaultObject;
            *reinterpret_cast<std::string*>(defaults->propertyAddress(dx::Name("actionText"))) = "Player Sp|&eed";
            *reinterpret_cast<std::string*>(defaults->propertyAddress(dx::Name("helpText"))) = "Player movement speed in percent of normal speed.";
            *reinterpret_cast<std::string*>(defaults->propertyAddress(dx::Name("configSetting"))) = "ini:Engine.Engine.GameEngine PlayerSpeed";
            *reinterpret_cast<int32_t*>(defaults->propertyAddress(dx::Name("numTicks"))) = 16;
            *reinterpret_cast<float*>(defaults->propertyAddress(dx::Name("startValue"))) = 50.0f;
            *reinterpret_cast<float*>(defaults->propertyAddress(dx::Name("endValue"))) = 200.0f;
            *reinterpret_cast<float*>(defaults->propertyAddress(dx::Name("defaultValue"))) = 100.0f;
            std::string section = dx::config::system().get("Engine.Engine", "GameEngine").value_or("DeusEx.DeusExGameEngine");
            if (!dx::config::system().get(section, "PlayerSpeed")) {
                dx::config::system().set(section, "PlayerSpeed", "100");
            }
        }

        void addFixVanillaBugsChoice(dx::UClass* screen, int index)
        {
            dx::UClass* choice = dx::objects::createSubclass(dx::objects::findClass("DeusEx.MenuChoice_DetailTextures"), dx::Name("MenuChoice_FixVanillaBugs"));
            *reinterpret_cast<dx::UObject**>(screen->defaultObject->propertyAddress(dx::Name("choices"), index)) = choice;
            dx::UObject* defaults = choice->defaultObject;
            *reinterpret_cast<std::string*>(defaults->propertyAddress(dx::Name("actionText"))) = "Fix |&Vanilla Bugs";
            *reinterpret_cast<std::string*>(defaults->propertyAddress(dx::Name("helpText"))) = "Fixes bugs of the original game, such as stunned or gassed enemies attacking as soon as they are hit. Off matches the original game.";
            *reinterpret_cast<std::string*>(defaults->propertyAddress(dx::Name("configSetting"))) = "ini:Engine.Engine.GameEngine FixVanillaBugs";
            *reinterpret_cast<int32_t*>(defaults->propertyAddress(dx::Name("defaultValue"))) = 1;
            std::string section = dx::config::system().get("Engine.Engine", "GameEngine").value_or("DeusEx.DeusExGameEngine");
            if (!dx::config::system().get(section, "FixVanillaBugs")) {
                dx::config::system().set(section, "FixVanillaBugs", "True");
            }
        }

        void applyPlayerSpeed()
        {
            std::string section = dx::config::system().get("Engine.Engine", "GameEngine").value_or("DeusEx.DeusExGameEngine");
            float speed = static_cast<float>(std::atof(dx::config::system().get(section, "PlayerSpeed").value_or("100").c_str())) / 100.0f;
            if (speed == appliedPlayerSpeed) {
                return;
            }
            appliedPlayerSpeed = speed;
            dx::UClass* playerPawn = dx::objects::findClass("Engine.PlayerPawn");
            for (dx::UObject* object : dx::objects::all()) {
                dx::UClass* cls = dx::cast<dx::UClass>(object);
                if (!cls || !cls->defaultObject || !cls->isChildOf(playerPawn)) {
                    continue;
                }
                float* ground = reinterpret_cast<float*>(cls->defaultObject->propertyAddress(dx::Name("GroundSpeed")));
                float* water = reinterpret_cast<float*>(cls->defaultObject->propertyAddress(dx::Name("WaterSpeed")));
                const std::pair<float, float>& base = basePlayerSpeeds.try_emplace(cls, *ground, *water).first->second;
                *ground = base.first * speed;
                *water = base.second * speed;
            }
        }

        void applyRenderOptions()
        {
            dx::RenderOptions render;
            render.anisotropy = static_cast<float>(1 << std::clamp(videoSetting("Anisotropy", 0), 0, 4));
            render.trilinear = videoSetting("UseTrilinear", 0) != 0;
            int multisample = std::clamp(videoSetting("Multisampling", 0), 0, 3);
            render.multisample = multisample > 0 ? 1 << multisample : 0;
            render.fxaa = videoSetting("FXAA", 0) != 0;
            render.smoothLightmaps = videoSetting("SmoothLightmaps", 0) != 0;
            const float detailDistances[] = { 380.0f, 1024.0f, 2048.0f };
            render.detailDistance = detailDistances[std::clamp(videoSetting("DetailDistance", 0), 0, 2)];
            render.textureRate = videoSetting("LimitTextureAnimation", 1) != 0 ? 60.0f : 0.0f;
            render.ambientOcclusion = static_cast<dx::OcclusionQuality>(std::clamp(videoSetting("AmbientOcclusion", 0), 0, 3));
            renderer->setOptions(render);
            constexpr float kUIScaleValues[] = { 0.0f, 1.0f, 1.25f, 1.5f, 1.75f, 2.0f, 2.25f, 2.5f, 2.75f, 3.0f, 3.5f, 4.0f };
            dx::extension::setUIScale(kUIScaleValues[std::clamp(videoSetting("UIScale", 0), 0, 11)]);
            perfOverlay.setMode(static_cast<game::PerfOverlayMode>(std::clamp(videoSetting("PerformanceHUD", 0), 0, 2)));
            applyTouchOptions();
            if (browser || options.frames >= 0) {
                return;
            }
            int vsync = std::clamp(videoSetting("VSync", 1), 0, 2);
            if (vsync != appliedVSync) {
                appliedVSync = vsync;
                if (vsync != 2 || SDL_GL_SetSwapInterval(-1) != 0) {
                    SDL_GL_SetSwapInterval(std::min(vsync, 1));
                }
            }
        }

        void limitFrameRate()
        {
            const int limits[] = { 0, 30, 60, 75, 120, 144, 165, 240 };
            int limit = limits[std::clamp(videoSetting("FrameRateLimit", 0), 0, 7)];
            if (browser || limit == 0 || options.frames >= 0) {
                return;
            }
            uint64_t frequency = SDL_GetPerformanceFrequency();
            uint64_t deadline = frameDeadline + frequency / static_cast<uint64_t>(limit);
            uint64_t now = SDL_GetPerformanceCounter();
            if (now >= deadline) {
                frameDeadline = now;
                return;
            }
            while (now < deadline) {
                uint64_t remaining = (deadline - now) * 1000 / frequency;
                if (remaining > 1) {
                    SDL_Delay(static_cast<uint32_t>(remaining - 1));
                }
                now = SDL_GetPerformanceCounter();
            }
            frameDeadline = deadline;
        }

        void addFieldOfViewChoice()
        {
            dx::UClass* screen = dx::objects::findClass("DeusEx.MenuScreenDisplay");
            dx::UClass* choice = dx::objects::createSubclass(dx::objects::findClass("DeusEx.MenuUIChoiceEnum"), dx::Name("MenuChoice_FieldOfView"));
            *reinterpret_cast<dx::UObject**>(screen->defaultObject->propertyAddress(dx::Name("choices"), 2)) = choice;
            dx::UObject* defaults = choice->defaultObject;
            *reinterpret_cast<std::string*>(defaults->propertyAddress(dx::Name("actionText"))) = "Field of |&View";
            *reinterpret_cast<std::string*>(defaults->propertyAddress(dx::Name("helpText"))) = "Horizontal field of view on a 4:3 screen. Wider screens show more at the sides.";
            *reinterpret_cast<std::string*>(defaults->propertyAddress(dx::Name("configSetting"))) = "ini:Engine.Engine.ViewportManager FieldOfViewIndex";
            *reinterpret_cast<int32_t*>(defaults->propertyAddress(dx::Name("defaultValue"))) = 3;
            *reinterpret_cast<int32_t*>(defaults->propertyAddress(dx::Name("defaultInfoWidth"))) = 98;
            for (int i = 0; i <= 12; i++) {
                *reinterpret_cast<std::string*>(defaults->propertyAddress(dx::Name("enumText"), i)) = std::to_string(60 + 5 * i);
            }
            if (!dx::config::system().get(videoSection(), "FieldOfViewIndex")) {
                dx::config::system().set(videoSection(), "FieldOfViewIndex", "3");
            }
        }

        bool start()
        {
            std::filesystem::path root = dx::config::findGameRoot(DEUSEX_GAME_DATA);
            if (!options.mod.empty()) {
                std::filesystem::path mod = (root / "..").lexically_normal() / "mods" / options.mod;
                if (!std::filesystem::is_directory(mod)) {
                    std::fprintf(stderr, "mod %s not found in %s\n", options.mod.c_str(), mod.parent_path().string().c_str());
                    return false;
                }
                dx::config::useMod(mod);
            }
            dx::engine::initialize(root.string(), options.ini, options.userIni);
            dx::audio::initialize();
            dx::audio::setLightRandom(dx::lightRandomBase);
            renderer = std::make_unique<dx::SceneRenderer>();
            renderer->iteratorHook = [this](dx::UObject* actor, const std::function<void(dx::UObject*)>& emit) {
                return world && dx::deusex::renderIterate(*world, actor, emit);
            };
            dx::console::openMapHook = [this](const std::string& url, int32_t travelType) {
                pendingMap = url;
                pendingTravelType = travelType;
            };
            dx::console::quitHook = [this]() {
                running = false;
            };
            dx::console::currentResHook = [this]() {
                int width = 0;
                int height = 0;
                SDL_GetWindowSize(window, &width, &height);
                return std::to_string(width) + "x" + std::to_string(height);
            };
            dx::console::resolutionsHook = [this]() {
                return availableResolutions();
            };
            dx::console::setResHook = [this](int width, int height) {
                bool fullscreen = (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0;
                applyVideoMode(fullscreen, width, height);
                saveVideoMode(fullscreen, width, height);
            };
            dx::console::toggleFullscreenHook = [this]() {
                bool fullscreen = (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) == 0;
                int width = videoSetting(fullscreen ? "FullscreenViewportX" : "WindowedViewportX", 640);
                int height = videoSetting(fullscreen ? "FullscreenViewportY" : "WindowedViewportY", 480);
                applyVideoMode(fullscreen, width, height);
                saveVideoMode(fullscreen, width, height);
            };
            dx::console::screenshotHook = [this]() {
                std::filesystem::path directory = dx::config::system().path.parent_path() / "Screenshots";
                std::filesystem::create_directories(directory);
                int index = 0;
                std::filesystem::path file;
                do {
                    char name[32];
                    std::snprintf(name, sizeof(name), "Shot%04d.bmp", index++);
                    file = directory / name;
                } while (std::filesystem::exists(file));
                pendingShot = file.string();
            };
            dx::console::applyAudioSettings();
            if (options.frames < 0 && !options.sizeGiven) {
                bool fullscreen = videoSetting("StartupFullscreen", 0) != 0;
                applyVideoMode(fullscreen, videoSetting(fullscreen ? "FullscreenViewportX" : "WindowedViewportX", options.width), videoSetting(fullscreen ? "FullscreenViewportY" : "WindowedViewportY", options.height));
            }
            dx::CanvasHooks& hooks = dx::canvasHooks();
            hooks.target = &renderer->canvas();
            hooks.drawActor = [this](dx::UObject* actor, bool) {
                // Keep first-person viewmodels at a natural FOV so wide or narrow
                // world FOV settings never shrink the weapon or clip it into the camera.
                float vmFov = std::clamp(viewFov, 85.0f, 95.0f);
                renderer->drawActorOverlay(actor, viewLocation, viewRotation, vmFov, true);
            };
            hooks.clipboardCopy = [](const std::string& text) {
                SDL_SetClipboardText(text.c_str());
            };
            hooks.clipboardPaste = []() {
                char* text = SDL_GetClipboardText();
                std::string result = text ? text : "";
                SDL_free(text);
                return result;
            };
            dx::extension::setFrameCanvas(&renderer->canvas());
            dx::extension::setProjector([this](const dx::Vector& location, float& pixelX, float& pixelY) {
                return project(location, pixelX, pixelY);
            });
            dx::extension::setActorDrawer([this](dx::Canvas2D&, const dx::extension::ActorDrawRequest& request) {
                if (request.actor) {
                    renderer->drawActorOverlay(request.actor, viewLocation, viewRotation, viewFov, request.clearZ);
                }
            });
            dx::extension::setViewportDrawer([this](dx::Canvas2D&, const dx::extension::ViewportRequest& request) {
                if (!world || !world->level) {
                    return;
                }
                dx::UObject* hidden = request.showActor ? nullptr : request.originActor;
                int x = static_cast<int>(std::lround(request.x));
                int y = static_cast<int>(std::lround(request.y));
                int width = static_cast<int>(std::lround(request.width));
                int height = static_cast<int>(std::lround(request.height));
                renderer->drawSceneRect(world->level, world->actors, request.location, request.rotation, request.fov, hidden, x, y, width, height);
            });
            loadPackageClasses("DeusEx");
            std::string rootClass = dx::config::system().get("Engine.Engine", "Root").value_or("DeusEx.DeusExRootWindow");
            loadPackageClasses(rootClass.substr(0, rootClass.find('.')));
            dx::deusex::registerUiTextTranslator();
            addFieldOfViewChoice();
            addExtraOptions();
            initControllers();
            touchOverlay.load(videoSection());
            applyTouchOptions();
            std::string map = options.map;
            std::filesystem::path mapFile = map;
            if (mapFile.has_parent_path() && std::filesystem::is_regular_file(mapFile)) {
                map = mapFile.stem().string();
                dx::objects::loadPackageFile(dx::Name(map), mapFile, false);
            }
            if (map.empty()) {
                auto local = dx::config::system().get("URL", "LocalMap");
                map = local ? *local : "DX.dx";
            }
            return openMap(map);
        }

        bool project(const dx::Vector& location, float& pixelX, float& pixelY)
        {
            dx::Coords axes = dx::rotationAxes(viewRotation);
            dx::Vector delta = location - viewLocation;
            float forward = delta.dot(axes.xAxis);
            if (forward <= 1.0f) {
                return false;
            }
            float right = delta.dot(axes.yAxis);
            float up = delta.dot(axes.zAxis);
            float halfWidth = static_cast<float>(frameWidth) * 0.5f;
            float halfHeight = static_cast<float>(frameHeight) * 0.5f;
            float scale = halfWidth / std::tan(viewFov * dx::Pi / 360.0f);
            pixelX = halfWidth + right / forward * scale;
            pixelY = halfHeight - up / forward * scale;
            return true;
        }

        bool openMap(const std::string& url, bool carryItems = false)
        {
            std::vector<dx::TravelActor> travel;
            std::vector<std::pair<std::string, std::string>> travelInfo;
            if (carryItems && world) {
                travel = world->collectTravel();
                dx::UObject* player = world->player();
                if (player) {
                    dx::UObject* replication = dx::pawn::PlayerReplicationInfo(player);
                    std::string key = replication ? dx::playerreplicationinfo::PlayerName(replication) : std::string("Player");
                    travelInfo.emplace_back(key, world->travelString(player));
                }
            }
            std::string target = url;
            if (world && !target.empty() && target[0] == '?') {
                target = world->url.substr(0, world->url.find('?')) + target;
            }
            int32_t loadIndex = 0;
            bool loading = dx::savegame::loadRequested(target, loadIndex);
            if (loading) {
                std::string map;
                if (!dx::savegame::prepareLoad(loadIndex, map)) {
                    std::fprintf(stderr, "cannot load saved game %d\n", loadIndex);
                    dx::World::releaseTravel(travel);
                    return false;
                }
                target = map + "?loadgame=" + std::to_string(loadIndex);
            } else if (world) {
                dx::savegame::storeDepartingLevel(*world, target);
            }
            if (loading || world) {
                dx::savegame::selectLevel(target);
            }
            dx::extension::reset();
            renderer->flushLevel();
            std::vector<std::pair<std::string, std::string>> previousTravelInfo;
            if (world && world->level) {
                previousTravelInfo = world->level->travelInfo;
            }
            if (world && world->level) {
                dx::objects::unloadPackage(world->level->outermost()->name);
            }
            world.reset();
            canvasObject = nullptr;
            world = std::make_unique<dx::World>();
            world->incomingTravel = std::move(travel);
            if (!loading) {
                bool restart = target.find("?restart") != std::string::npos || target.find("?RESTART") != std::string::npos;
                if (restart) {
                    travelInfo = previousTravelInfo;
                }
                world->arrivalTravelInfo = std::move(travelInfo);
                world->replaceTravelInfo = true;
            }
            world->inputHook = [](dx::UObject* player, float delta) {
                dx::console::readInput(player, delta);
            };
            if (!world->loadMap(target)) {
                return false;
            }
            if (loading) {
                world->url = world->level->url;
            }
            dx::console::resetInput(world->player());
            return true;
        }

        dx::UObject* canvas(int width, int height)
        {
            if (!canvasObject) {
                canvasObject = dx::objects::newObject(dx::engineClasses().canvas, dx::objects::packageObject(dx::Name("Transient")), dx::Name("Canvas0"), 0);
                dx::canvas::Viewport(canvasObject) = world->viewport;
            }
            dx::vm::event(canvasObject, dx::Name("Reset"));
            dx::canvas::SizeX(canvasObject) = width;
            dx::canvas::SizeY(canvasObject) = height;
            dx::canvas::ClipX(canvasObject) = static_cast<float>(width);
            dx::canvas::ClipY(canvasObject) = static_cast<float>(height);
            return canvasObject;
        }

        dx::UObject* scriptConsole()
        {
            if (!world || !world->viewport || !world->player()) {
                return nullptr;
            }
            return dx::player::Console(world->viewport);
        }

        bool consoleKeyEvent(uint8_t key, uint8_t action, float delta)
        {
            dx::UObject* console = scriptConsole();
            if (!console) {
                return false;
            }
            dx::vm::Call call(console, dx::Name("KeyEvent"));
            if (!call.valid()) {
                return false;
            }
            call.byte(key).byte(action).f(delta).run();
            return call.returnValue() && *reinterpret_cast<uint32_t*>(call.returnValue());
        }

        void charEvent(char c)
        {
            dx::UObject* console = scriptConsole();
            if (console) {
                dx::vm::Call call(console, dx::Name("KeyType"));
                if (call.valid()) {
                    call.byte(static_cast<uint8_t>(c)).run();
                    if (call.returnValue() && *reinterpret_cast<uint32_t*>(call.returnValue())) {
                        return;
                    }
                }
            }
            dx::extension::charEvent(c);
        }

        void handleKey(uint8_t key, bool pressed)
        {
            if (key == dx::IK_None) {
                return;
            }
            uint8_t action = pressed ? dx::IST_Press : dx::IST_Release;
            if (consoleKeyEvent(key, action, 0)) {
                if (!pressed) {
                    dx::extension::keyEvent(key, action, 0);
                    dx::console::key(key, action, 0);
                }
                return;
            }
            if (dx::extension::keyEvent(key, action, 0)) {
                if (!pressed) {
                    dx::console::key(key, action, 0);
                }
                return;
            }
            dx::console::key(key, action, 0);
        }

        void handleAxis(uint8_t key, float delta)
        {
            if (consoleKeyEvent(key, dx::IST_Axis, delta)) {
                return;
            }
            if (dx::extension::keyEvent(key, dx::IST_Axis, delta)) {
                return;
            }
            dx::console::key(key, dx::IST_Axis, delta);
        }

        static constexpr std::array<std::pair<SDL_GameControllerButton, uint8_t>, 20> kControllerButtons {{
            { SDL_CONTROLLER_BUTTON_A, dx::IK_Joy1 },
            { SDL_CONTROLLER_BUTTON_B, dx::IK_Joy2 },
            { SDL_CONTROLLER_BUTTON_X, dx::IK_Joy3 },
            { SDL_CONTROLLER_BUTTON_Y, dx::IK_Joy4 },
            { SDL_CONTROLLER_BUTTON_LEFTSHOULDER, dx::IK_Joy5 },
            { SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, dx::IK_Joy6 },
            { SDL_CONTROLLER_BUTTON_BACK, dx::IK_Joy7 },
            { SDL_CONTROLLER_BUTTON_START, dx::IK_Joy8 },
            { SDL_CONTROLLER_BUTTON_LEFTSTICK, dx::IK_Joy9 },
            { SDL_CONTROLLER_BUTTON_RIGHTSTICK, dx::IK_Joy10 },
            { SDL_CONTROLLER_BUTTON_PADDLE1, dx::IK_Joy11 },
            { SDL_CONTROLLER_BUTTON_PADDLE2, dx::IK_Joy12 },
            { SDL_CONTROLLER_BUTTON_PADDLE3, dx::IK_Joy13 },
            { SDL_CONTROLLER_BUTTON_PADDLE4, dx::IK_Joy14 },
            { SDL_CONTROLLER_BUTTON_MISC1, dx::IK_Joy15 },
            { SDL_CONTROLLER_BUTTON_TOUCHPAD, dx::IK_Joy16 },
            { SDL_CONTROLLER_BUTTON_DPAD_UP, dx::IK_JoyPovUp },
            { SDL_CONTROLLER_BUTTON_DPAD_DOWN, dx::IK_JoyPovDown },
            { SDL_CONTROLLER_BUTTON_DPAD_LEFT, dx::IK_JoyPovLeft },
            { SDL_CONTROLLER_BUTTON_DPAD_RIGHT, dx::IK_JoyPovRight },
        }};

        void updateGamepadInfo()
        {
            if (!controller) {
                dx::console::setGamepadInfo("None");
                return;
            }
            SDL_GameControllerType padType = SDL_GameControllerGetType(controller);
            if (padType == SDL_CONTROLLER_TYPE_XBOXONE) {
                dx::console::setGamepadInfo("XboxOne");
            } else if (padType == SDL_CONTROLLER_TYPE_XBOX360) {
                dx::console::setGamepadInfo("Xbox360");
            } else if (padType == SDL_CONTROLLER_TYPE_PS4) {
                dx::console::setGamepadInfo("PS4");
            } else if (padType == SDL_CONTROLLER_TYPE_PS5) {
                dx::console::setGamepadInfo("PS5");
            } else if (padType == SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_PRO) {
                dx::console::setGamepadInfo("SwitchPro");
            } else {
                dx::console::setGamepadInfo("Unknown");
            }
        }

        void closeController()
        {
            if (controller) {
                SDL_GameControllerClose(controller);
                controller = nullptr;
                controllerId = -1;
            }
        }

        void emitAxisWithZeroEdge(uint8_t key, float value, float& prevValue)
        {
            if (value != 0.0f || prevValue != 0.0f) {
                handleAxis(key, value);
                prevValue = value;
            }
        }

        void releaseGamepadInputs()
        {
            for (size_t i = 0; i < kControllerButtons.size(); i++) {
                uint32_t bit = 1u << static_cast<uint32_t>(i);
                if (prevControllerButtons & bit) {
                    handleKey(kControllerButtons[i].second, false);
                }
            }
            prevControllerButtons = 0;
            if (prevStickRepeatKey != dx::IK_None) {
                handleKey(prevStickRepeatKey, false);
                prevStickRepeatKey = dx::IK_None;
            }
            repeatKey = dx::IK_None;
            repeatTimer = 0.0f;
            emitAxisWithZeroEdge(dx::IK_JoyX, 0.0f, prevLeftStickX);
            emitAxisWithZeroEdge(dx::IK_JoyY, 0.0f, prevLeftStickY);
            emitAxisWithZeroEdge(dx::IK_JoyU, 0.0f, prevRightStickX);
            emitAxisWithZeroEdge(dx::IK_JoyV, 0.0f, prevRightStickY);
            emitAxisWithZeroEdge(dx::IK_JoyZ, 0.0f, prevLeftTrigger);
            emitAxisWithZeroEdge(dx::IK_JoyR, 0.0f, prevRightTrigger);
            dx::console::setGamepadRawMags(0.0f, 0.0f);
        }

        void openControllerByIndex(int index)
        {
            if (!SDL_IsGameController(index)) {
                return;
            }
            SDL_GameController* opened = SDL_GameControllerOpen(index);
            if (!opened) {
                return;
            }
            SDL_Joystick* joy = SDL_GameControllerGetJoystick(opened);
            SDL_JoystickID jid = joy ? SDL_JoystickInstanceID(joy) : -1;
            if (controller && jid == controllerId) {
                SDL_GameControllerClose(opened);
                return;
            }
            releaseGamepadInputs();
            closeController();
            controller = opened;
            controllerId = jid;
            updateGamepadInfo();
        }

        void openControllerById(SDL_JoystickID jid)
        {
            if (controller && jid == controllerId) {
                return;
            }
            int count = SDL_NumJoysticks();
            for (int i = 0; i < count; i++) {
                if (SDL_IsGameController(i) && SDL_JoystickGetDeviceInstanceID(i) == jid) {
                    openControllerByIndex(i);
                    return;
                }
            }
        }

        void initControllers()
        {
            int count = SDL_NumJoysticks();
            for (int i = 0; i < count; i++) {
                if (SDL_IsGameController(i)) {
                    openControllerByIndex(i);
                    break;
                }
            }
            if (!controller) {
                updateGamepadInfo();
            }
        }

        game::TouchInputCallbacks touchCallbacks()
        {
            return {
                [this](uint8_t key, bool pressed) {
                    handleKey(key, pressed);
                },
                [](uint8_t key, float value) {
                    dx::console::key(key, dx::IST_Axis, value);
                },
                [](float dxVal, float dyVal) {
                    dx::console::key(dx::IK_MouseX, dx::IST_Axis, dxVal);
                    dx::console::key(dx::IK_MouseY, dx::IST_Axis, dyVal);
                },
                [this]() {
                    handleKey(dx::IK_RightMouse, true);
                    handleKey(dx::IK_RightMouse, false);
                },
                [](float sx, float sy) {
                    return dx::extension::hitTestHudBeltKey(sx, sy);
                },
            };
        }

        void applyTouchOptions()
        {
            static constexpr float opacities[] = { 0.25f, 0.40f, 0.55f, 0.75f, 0.95f };
            static constexpr float scales[] = { 0.75f, 1.00f, 1.25f, 1.50f };
            static constexpr float lookSens[] = { 0.50f, 0.75f, 1.00f, 1.50f, 2.00f };
#ifdef __ANDROID__
            constexpr int defaultEnabled = 1;
#else
            constexpr int defaultEnabled = 0;
#endif
            game::TouchOverlaySettings ts;
            bool nextEnabled = videoSetting("TouchOverlaysEnabled", defaultEnabled) != 0;
            if (!nextEnabled && touchOverlay.settings().enabled) {
                touchOverlay.releaseGameplayInputs(touchCallbacks());
            }
            ts.enabled = nextEnabled;
            ts.opacity = opacities[std::clamp(videoSetting("TouchOverlayOpacity", 2), 0, 4)];
            ts.scale = scales[std::clamp(videoSetting("TouchOverlayScale", 1), 0, 3)];
            ts.lookSensitivity = lookSens[std::clamp(videoSetting("TouchLookSensitivity", 2), 0, 4)];
            touchOverlay.setSettings(ts);
        }

        void resetTouchControls()
        {
            if (hasMenuFinger) {
                dx::extension::mouseButton(dx::IK_LeftMouse, false);
                hasMenuFinger = false;
            }
            touchOverlay.releaseGameplayInputs(touchCallbacks());
        }

        void handleTouchEvent(const SDL_Event& event)
        {
            int drawWidth = 0;
            int drawHeight = 0;
            SDL_GL_GetDrawableSize(window, &drawWidth, &drawHeight);
            if (drawWidth <= 0 || drawHeight <= 0) {
                return;
            }
            lastInputWasGamepad = false;
            if (touchOverlay.isEditorOpen()) {
                touchOverlay.handleTouchEvent(event, drawWidth, drawHeight, touchCallbacks());
                return;
            }
            SDL_FingerID fid = event.tfinger.fingerId;
            if (dx::extension::wantsMouse() || dx::extension::isModalOpen()) {
                dx::extension::setTouchInputActive(true);
                float px = event.tfinger.x * static_cast<float>(drawWidth);
                float py = event.tfinger.y * static_cast<float>(drawHeight);
                if (event.type == SDL_FINGERDOWN) {
                    if (hasMenuFinger && fid != menuFinger) {
                        dx::extension::mouseButton(dx::IK_LeftMouse, false);
                    }
                    hasMenuFinger = true;
                    menuFinger = fid;
                    dx::extension::mouseMove(px, py);
                    dx::extension::mouseButton(dx::IK_LeftMouse, true);
#ifdef __ANDROID__
                    if (dx::extension::isEditFocused()) {
                        SDL_StartTextInput();
                        textInputRequested = true;
                    }
#endif
                } else if (event.type == SDL_FINGERMOTION && hasMenuFinger && fid == menuFinger) {
                    dx::extension::mouseMove(px, py);
                } else if (event.type == SDL_FINGERUP && hasMenuFinger && fid == menuFinger) {
                    dx::extension::mouseMove(px, py);
                    dx::extension::mouseButton(dx::IK_LeftMouse, false);
                    hasMenuFinger = false;
                }
                return;
            }

            touchOverlay.handleTouchEvent(event, drawWidth, drawHeight, touchCallbacks());
        }

        void pollGamepad(float delta)
        {
            if (!controller) {
                return;
            }
            uint32_t buttonsNow = 0;
            bool padActivity = false;
            for (size_t i = 0; i < kControllerButtons.size(); i++) {
                bool nowDown = SDL_GameControllerGetButton(controller, kControllerButtons[i].first) != 0;
                uint32_t bit = 1u << static_cast<uint32_t>(i);
                if (nowDown) {
                    buttonsNow |= bit;
                    padActivity = true;
                }
                bool wasDown = (prevControllerButtons & bit) != 0;
                if (nowDown != wasDown) {
                    if (touchOverlay.isEditorOpen()) {
                        touchOverlay.handleEditorControllerButton(kControllerButtons[i].first, nowDown);
                    } else {
                        handleKey(kControllerButtons[i].second, nowDown);
                    }
                }
            }
            prevControllerButtons = buttonsNow;
            if (touchOverlay.isEditorOpen()) {
                return;
            }

            auto clampX = [](Sint16 raw) {
                return std::clamp(static_cast<int>(raw), -32767, 32767);
            };
            auto negateY = [](Sint16 raw) {
                return std::clamp(-static_cast<int>(raw), -32767, 32767);
            };

            int lx = clampX(SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTX));
            int ly = negateY(SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTY));
            int rx = clampX(SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_RIGHTX));
            int ry = negateY(SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_RIGHTY));
            int trigL = std::clamp(static_cast<int>(SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_TRIGGERLEFT)), 0, 32767);
            int trigR = std::clamp(static_cast<int>(SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_TRIGGERRIGHT)), 0, 32767);

            float rawLMag = std::min(1.0f, std::hypot(static_cast<float>(lx), static_cast<float>(ly)) / 32767.0f);
            float rawRMag = std::min(1.0f, std::hypot(static_cast<float>(rx), static_cast<float>(ry)) / 32767.0f);
            dx::console::setGamepadRawMags(rawLMag, rawRMag);

            const dx::console::GamepadSettings& gs = dx::console::gamepadSettings();
            float outLX = 0.0f;
            float outLY = 0.0f;
            dx::console::shapeStick(lx, ly, gs.leftDeadzone, gs.leftCurve, 1.0f, outLX, outLY);

            int ryForEmit = gs.invertLookY ? -ry : ry;
            float outRX = 0.0f;
            float outRY = 0.0f;
            dx::console::shapeStick(rx, ryForEmit, gs.rightDeadzone, gs.rightCurve, gs.rightScale, outRX, outRY);

            float outLT = dx::console::shapeTrigger(trigL, gs.triggerThreshold);
            float outRT = dx::console::shapeTrigger(trigR, gs.triggerThreshold);

            if (padActivity || outLX != 0.0f || outLY != 0.0f || outRX != 0.0f || outRY != 0.0f || outLT != 0.0f || outRT != 0.0f) {
                lastInputWasGamepad = true;
                touchOverlay.onPhysicalInput(touchCallbacks());
            }

            emitAxisWithZeroEdge(dx::IK_JoyX, outLX, prevLeftStickX);
            emitAxisWithZeroEdge(dx::IK_JoyY, outLY, prevLeftStickY);
            emitAxisWithZeroEdge(dx::IK_JoyU, outRX, prevRightStickX);
            emitAxisWithZeroEdge(dx::IK_JoyV, outRY, prevRightStickY);
            emitAxisWithZeroEdge(dx::IK_JoyZ, outLT, prevLeftTrigger);
            emitAxisWithZeroEdge(dx::IK_JoyR, outRT, prevRightTrigger);

            if (dx::extension::isModalOpen()) {
                uint8_t dpadDir = dx::IK_None;
                if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_UP)) {
                    dpadDir = dx::IK_JoyPovUp;
                } else if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_DOWN)) {
                    dpadDir = dx::IK_JoyPovDown;
                } else if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_LEFT)) {
                    dpadDir = dx::IK_JoyPovLeft;
                } else if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) {
                    dpadDir = dx::IK_JoyPovRight;
                }
                uint8_t stickDir = dx::IK_None;
                if (dpadDir == dx::IK_None && (outLX * outLX + outLY * outLY) > (500.0f * 500.0f)) {
                    if (std::fabs(outLY) >= std::fabs(outLX)) {
                        stickDir = outLY > 0.0f ? dx::IK_JoyPovUp : dx::IK_JoyPovDown;
                    } else {
                        stickDir = outLX > 0.0f ? dx::IK_JoyPovRight : dx::IK_JoyPovLeft;
                    }
                }
                if (stickDir != prevStickRepeatKey) {
                    if (prevStickRepeatKey != dx::IK_None) {
                        handleKey(prevStickRepeatKey, false);
                    }
                    if (stickDir != dx::IK_None) {
                        handleKey(stickDir, true);
                    }
                    prevStickRepeatKey = stickDir;
                }
                uint8_t activeDir = dpadDir != dx::IK_None ? dpadDir : stickDir;
                if (activeDir != dx::IK_None) {
                    if (activeDir != repeatKey) {
                        repeatKey = activeDir;
                        repeatTimer = 0.40f;
                    } else {
                        repeatTimer -= delta;
                        if (repeatTimer <= 0.0f) {
                            repeatTimer = 0.09f;
                            if (!consoleKeyEvent(repeatKey, dx::IST_Hold, 0)) {
                                dx::extension::keyEvent(repeatKey, dx::IST_Hold, 0);
                            }
                        }
                    }
                } else {
                    repeatKey = dx::IK_None;
                    repeatTimer = 0.0f;
                }
            } else {
                if (prevStickRepeatKey != dx::IK_None) {
                    handleKey(prevStickRepeatKey, false);
                    prevStickRepeatKey = dx::IK_None;
                }
                repeatKey = dx::IK_None;
                repeatTimer = 0.0f;
            }
        }

        void processEvents()
        {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                switch (event.type) {
                case SDL_QUIT:
                    running = false;
                    break;
                case SDL_APP_WILLENTERBACKGROUND:
                    dx::audio::suspend();
                    dx::config::system().save();
                    dx::config::user().save();
                    break;
                case SDL_APP_DIDENTERFOREGROUND:
                    dx::audio::resume();
                    break;
                case SDL_CONTROLLERDEVICEADDED:
                    if (!controller) {
                        openControllerByIndex(event.cdevice.which);
                    }
                    break;
                case SDL_CONTROLLERDEVICEREMOVED:
                    if (controller && event.cdevice.which == controllerId) {
                        releaseGamepadInputs();
                        closeController();
                        initControllers();
                    }
                    break;
                case SDL_CONTROLLERBUTTONDOWN:
                    openControllerById(event.cbutton.which);
                    break;
                case SDL_FINGERDOWN:
                case SDL_FINGERMOTION:
                case SDL_FINGERUP:
                    handleTouchEvent(event);
                    break;
                case SDL_KEYDOWN:
                    if (touchOverlay.handleEditorPhysicalKey(event.key.keysym.sym, true)) {
                        break;
                    }
                    if (event.key.keysym.sym != SDLK_VOLUMEUP && event.key.keysym.sym != SDLK_VOLUMEDOWN) {
                        touchOverlay.onPhysicalInput(touchCallbacks());
                    }
                    if (!event.key.repeat) {
                        handleKey(translateKey(event.key.keysym.sym), true);
                    } else if (!consoleKeyEvent(translateKey(event.key.keysym.sym), dx::IST_Hold, 0)) {
                        dx::extension::keyEvent(translateKey(event.key.keysym.sym), dx::IST_Hold, 0);
                    }
                    break;
                case SDL_KEYUP:
                    if (touchOverlay.handleEditorPhysicalKey(event.key.keysym.sym, false)) {
                        break;
                    }
                    handleKey(translateKey(event.key.keysym.sym), false);
                    break;
                case SDL_TEXTINPUT:
                    if (touchOverlay.isEditorOpen()) {
                        break;
                    }
                    for (const char* c = event.text.text; *c; c++) {
                        charEvent(*c);
                    }
                    break;
                case SDL_MOUSEMOTION:
                    if (event.motion.which == SDL_TOUCH_MOUSEID || touchOverlay.isEditorOpen()) {
                        break;
                    }
                    touchOverlay.onPhysicalInput(touchCallbacks());
                    dx::extension::setTouchInputActive(false);
                    if (dx::extension::wantsMouse() || dx::extension::isModalOpen()) {
                        int width = 0;
                        int height = 0;
                        SDL_GetWindowSize(window, &width, &height);
                        int drawWidth = 0;
                        int drawHeight = 0;
                        SDL_GL_GetDrawableSize(window, &drawWidth, &drawHeight);
                        float scaleX = width > 0 ? static_cast<float>(drawWidth) / static_cast<float>(width) : 1.0f;
                        float scaleY = height > 0 ? static_cast<float>(drawHeight) / static_cast<float>(height) : 1.0f;
                        dx::extension::mouseMove(static_cast<float>(event.motion.x) * scaleX, static_cast<float>(event.motion.y) * scaleY);
                    } else {
                        dx::console::key(dx::IK_MouseX, dx::IST_Axis, static_cast<float>(event.motion.xrel));
                        dx::console::key(dx::IK_MouseY, dx::IST_Axis, static_cast<float>(-event.motion.yrel));
                    }
                    break;
                case SDL_MOUSEBUTTONDOWN:
                case SDL_MOUSEBUTTONUP: {
                    if (event.button.which == SDL_TOUCH_MOUSEID || touchOverlay.isEditorOpen()) {
                        break;
                    }
                    if (event.type == SDL_MOUSEBUTTONDOWN) {
                        touchOverlay.onPhysicalInput(touchCallbacks());
                    }
                    dx::extension::setTouchInputActive(false);
                    uint8_t key = translateButton(event.button.button);
                    bool pressed = event.type == SDL_MOUSEBUTTONDOWN;
                    if ((dx::extension::wantsMouse() || dx::extension::isModalOpen()) && dx::extension::mouseButton(key, pressed)) {
                        break;
                    }
                    handleKey(key, pressed);
                    break;
                }
                case SDL_MOUSEWHEEL: {
                    if (event.wheel.which == SDL_TOUCH_MOUSEID || touchOverlay.isEditorOpen()) {
                        break;
                    }
                    touchOverlay.onPhysicalInput(touchCallbacks());
                    uint8_t key = event.wheel.y > 0 ? dx::IK_MouseWheelUp : dx::IK_MouseWheelDown;
                    if (event.wheel.y != 0) {
                        dx::console::key(dx::IK_MouseW, dx::IST_Axis, static_cast<float>(event.wheel.y * 120));
                        handleKey(key, true);
                        handleKey(key, false);
                    }
                    break;
                }
                default:
                    break;
                }
            }
        }

        void handleTravel()
        {
            if (!pendingMap.empty()) {
                std::string url = dx::travelUrl(world ? world->url : std::string(), pendingMap, pendingTravelType);
                pendingMap.clear();
                openMap(url);
                return;
            }
            if (world && world->travelPending) {
                std::string url = dx::travelUrl(world->url, world->travelUrl, world->travelType);
                bool items = world->travelItems;
                world->travelPending = false;
                openMap(url, items);
            }
        }

        void render(int width, int height)
        {
            frameWidth = width;
            frameHeight = height;
            dx::extension::setViewportSize(static_cast<float>(width), static_cast<float>(height));
            if (world) {
                renderer->setTime(world->timeSeconds);
            }
            renderer->setViewportTime(viewportTime);
            renderer->beginFrame(width, height);
            viewLocation = {};
            viewRotation = {};
            viewFov = 90;
            if (world && world->calcView(viewLocation, viewRotation, viewFov)) {
                viewFov = displayFov(viewFov, width, height);
                dx::UObject* player = world->player();
                dx::UObject* viewActor = nullptr;
                if (player && !dx::pawn::bBehindView(player)) {
                    viewActor = dx::playerpawn::ViewTarget(player) ? dx::playerpawn::ViewTarget(player) : player;
                }
                renderer->drawScene(world->level, world->actors, viewLocation, viewRotation, viewFov, viewActor);
                dx::UObject* canvasActor = canvas(width, height);
                if (player && !player->destroyed) {
                    dx::vm::Call(player, dx::Name("PreRender")).obj(canvasActor).run();
                    if (viewActor) {
                        dx::vm::Call(viewActor, dx::Name("RenderOverlays")).obj(canvasActor).run();
                    }
                    dx::vm::Call(player, dx::Name("PostRenderFlash")).obj(canvasActor).run();
                    dx::vm::Call(player, dx::Name("PostRender")).obj(canvasActor).run();
                    dx::UObject* console = scriptConsole();
                    if (console) {
                        dx::console::FrameX(console) = static_cast<float>(width);
                        dx::console::FrameY(console) = static_cast<float>(height);
                        dx::vm::Call(console, dx::Name("PostRender")).obj(canvasActor).run();
                    }
                }
            }
            if (touchOverlay.isVisible() && touchOverlay.isAugDrawerOpen() && world && world->player()) {
                std::vector<game::TouchAugEntry> augs;
                dx::UObject* player = world->player();
                static const dx::Name nameAugSystem("AugmentationSystem");
                dx::UProperty* augSysProp = (player && player->cls) ? player->cls->findProperty(nameAugSystem) : nullptr;
                dx::UObject* augMgr = (augSysProp && augSysProp->type == dx::PropType::Object)
                    ? *reinterpret_cast<dx::UObject* const*>(player->data + augSysProp->offset)
                    : nullptr;
                if (augMgr && !augMgr->destroyed && augMgr->cls) {
                    static const dx::Name nameFirstAug("FirstAug");
                    static const dx::Name nameNext("next");
                    static const dx::Name nameHasIt("bHasIt");
                    static const dx::Name nameAlwaysActive("bAlwaysActive");
                    static const dx::Name nameIsActive("bIsActive");
                    static const dx::Name nameHotKeyNum("HotKeyNum");
                    static const dx::Name nameAugName("AugmentationName");
                    dx::UProperty* firstProp = augMgr->cls->findProperty(nameFirstAug);
                    dx::UObject* aug = (firstProp && firstProp->type == dx::PropType::Object)
                        ? *reinterpret_cast<dx::UObject* const*>(augMgr->data + firstProp->offset)
                        : nullptr;
                    for (int guard = 0; aug && !aug->destroyed && aug->cls && guard < 32; ++guard) {
                        auto readBool = [aug](dx::Name propName, bool fallback) {
                            dx::UProperty* p = aug->cls->findProperty(propName);
                            return (p && p->type == dx::PropType::Bool)
                                ? (*reinterpret_cast<const uint32_t*>(aug->data + p->offset) != 0)
                                : fallback;
                        };
                        bool hasIt = readBool(nameHasIt, false);
                        bool alwaysActive = readBool(nameAlwaysActive, false);
                        bool isActive = readBool(nameIsActive, false);
                        dx::UProperty* hkProp = aug->cls->findProperty(nameHotKeyNum);
                        int32_t hotKey = (hkProp && hkProp->type == dx::PropType::Int)
                            ? *reinterpret_cast<const int32_t*>(aug->data + hkProp->offset)
                            : 0;
                        if (hasIt && !alwaysActive && hotKey >= 1 && hotKey <= 12) {
                            dx::UProperty* nProp = aug->cls->findProperty(nameAugName);
                            std::string label;
                            if (nProp && (nProp->type == dx::PropType::Str || nProp->type == dx::PropType::String)) {
                                label = *reinterpret_cast<const std::string*>(aug->data + nProp->offset);
                            }
                            if (label.empty()) {
                                label = aug->name.str();
                            }
                            if (label.size() > 10) {
                                label.resize(10);
                            }
                            augs.push_back(game::TouchAugEntry {
                                std::move(label),
                                static_cast<uint8_t>(dx::IK_F1 + (hotKey - 1)),
                                hotKey,
                                isActive,
                            });
                        }
                        dx::UProperty* nextProp = aug->cls->findProperty(nameNext);
                        aug = (nextProp && nextProp->type == dx::PropType::Object)
                            ? *reinterpret_cast<dx::UObject* const*>(aug->data + nextProp->offset)
                            : nullptr;
                    }
                    std::sort(augs.begin(), augs.end(), [](const game::TouchAugEntry& a, const game::TouchAugEntry& b) {
                        return a.hotKeyNum < b.hotKeyNum;
                    });
                }
                touchOverlay.setAugEntries(std::move(augs));
            }
            touchOverlay.render(
                renderer->canvas(),
                dx::iequals(dx::config::language(), "ptb"),
                dx::extension::wantsMouse() || dx::extension::isModalOpen());
            perfOverlay.render(renderer->canvas(), width, height);
            renderer->endFrame();
        }

        void scriptedAction(const std::string& action)
        {
            size_t space = action.find(' ');
            std::string verb = action.substr(0, space);
            std::string rest = space == std::string::npos ? std::string() : action.substr(space + 1);
            if (verb == "press" || verb == "release") {
                uint8_t key = dx::console::keyFromName(rest);
                if (dx::extension::wantsMouse() && (key == dx::IK_LeftMouse || key == dx::IK_RightMouse)) {
                    dx::extension::mouseButton(key, verb == "press");
                } else {
                    handleKey(key, verb == "press");
                }
            } else if (verb == "cursor") {
                float x = 0;
                float y = 0;
                std::sscanf(rest.c_str(), "%f %f", &x, &y);
                dx::extension::mouseMove(x, y);
            } else if (verb == "type") {
                for (char c : rest) {
                    charEvent(c);
                }
            } else if (verb == "ccursor") {
                float x = 0;
                float y = 0;
                std::sscanf(rest.c_str(), "%f %f", &x, &y);
                x += std::floor(static_cast<float>(frameWidth - options.width) * 0.5f);
                y += std::floor(static_cast<float>(frameHeight - options.height) * 0.5f);
                dx::extension::mouseMove(x, y);
            } else if (verb == "mouse") {
                float dx = 0;
                float dy = 0;
                std::sscanf(rest.c_str(), "%f %f", &dx, &dy);
                dx::console::key(dx::IK_MouseX, dx::IST_Axis, dx);
                dx::console::key(dx::IK_MouseY, dx::IST_Axis, -dy);
            } else if (verb == "exec") {
                std::string output = dx::console::execute(world ? world->player() : nullptr, rest);
                if (!output.empty()) {
                    std::printf("%s\n", output.c_str());
                }
            } else if (verb == "shot") {
                pendingShot = rest;
            } else if (verb == "bench") {
                int warm = 300;
                int count = 900;
                char label[64] = {};
                std::sscanf(rest.c_str(), "%d %d %63s", &warm, &count, label);
                benchWarmup = std::max(warm, 0);
                benchFrames = std::max(count, 1);
                benchLabel = label[0] ? label : "run";
                benchSamples.clear();
                benchSamples.reserve(static_cast<size_t>(benchFrames));
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "OpenDE_Bench: start label=%s warmup=%d frames=%d", benchLabel.c_str(), benchWarmup, benchFrames);
            } else if (verb == "opt") {
                char key[32] = {};
                int value = 1;
                std::sscanf(rest.c_str(), "%31s %d", key, &value);
                if (std::strcmp(key, "noop") == 0) {
                    dx::vm::skipNoopCalls = value != 0;
                }
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "OpenDE_Bench: opt %s=%d", key, value);
            } else if (verb == "profdetail") {
                if (world) {
                    world->profDetail = rest == "1";
                }
            } else if (verb == "perfhud") {
                int modeVal = std::clamp(std::atoi(rest.c_str()), 0, 2);
                dx::config::system().set(videoSection(), "PerformanceHUD", std::to_string(modeVal));
                perfOverlay.setMode(static_cast<game::PerfOverlayMode>(modeVal));
            } else if (verb == "state") {
                dx::UObject* player = world ? world->player() : nullptr;
                if (player) {
                    dx::Vector location = dx::actor::Location(player);
                    std::printf("player at %.1f %.1f %.1f yaw %d state %s health %d\n", static_cast<double>(location.x), static_cast<double>(location.y), static_cast<double>(location.z), dx::actor::Rotation(player).yaw & 65535, dx::vm::stateName(player).c_str(), dx::pawn::Health(player));
                }
            }
        }

#ifdef __ANDROID__
        void pollAndroidDebugBridge(float delta)
        {
            if ((frame % 10) != 0) {
                return;
            }
            const char* extPath = SDL_AndroidGetExternalStoragePath();
            if (!extPath || !*extPath) {
                return;
            }
            std::string cmdPath = "/storage/emulated/0/OpenDE/debug_cmd.txt";
            struct stat cmdStat {};
            if (::stat(cmdPath.c_str(), &cmdStat) != 0) {
                cmdPath = std::string(extPath) + "/debug_cmd.txt";
                if (::stat(cmdPath.c_str(), &cmdStat) != 0) {
                    return;
                }
            }
            const long long cmdStamp = static_cast<long long>(cmdStat.st_mtim.tv_sec) * 1000000000LL + static_cast<long long>(cmdStat.st_mtim.tv_nsec) + static_cast<long long>(cmdStat.st_size);
            if (cmdStamp == lastDebugCmdStamp) {
                return;
            }
            std::FILE* f = std::fopen(cmdPath.c_str(), "rb");
            if (!f) {
                return;
            }
            std::string contents;
            char buf[1024];
            while (std::size_t n = std::fread(buf, 1, sizeof(buf), f)) {
                contents.append(buf, n);
            }
            std::fclose(f);
            if (std::remove(cmdPath.c_str()) != 0) {
                lastDebugCmdStamp = cmdStamp;
            }

            std::string lastExecOut;
            std::size_t start = 0;
            while (start < contents.size()) {
                std::size_t end = contents.find('\n', start);
                std::string line = (end == std::string::npos)
                    ? contents.substr(start)
                    : contents.substr(start, end - start);
                while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) {
                    line.pop_back();
                }
                if (!line.empty()) {
                    if (line.rfind("exec ", 0) == 0) {
                        lastExecOut = dx::console::execute(world ? world->player() : nullptr, line.substr(5));
                    } else if (line != "state") {
                        scriptedAction(line);
                    }
                }
                if (end == std::string::npos) {
                    break;
                }
                start = end + 1;
            }

            dx::UObject* player = world ? world->player() : nullptr;
            dx::Vector loc = player ? dx::actor::Location(player) : dx::Vector{0.0f, 0.0f, 0.0f};
            int yaw = player ? (dx::actor::Rotation(player).yaw & 65535) : 0;
            int health = player ? dx::pawn::Health(player) : 0;
            std::string pState = player ? dx::vm::stateName(player).c_str() : "";
            std::string lang = dx::config::system().get("Engine.Engine", "Language").value_or("int");
            float fps = delta > 0.0001f ? (1.0f / delta) : 0.0f;

            char jsonBuf[2048];
            std::snprintf(
                jsonBuf,
                sizeof(jsonBuf),
                "{\"frame\":%d,\"fps\":%.1f,\"tickMs\":%.2f,\"renderMs\":%.2f,\"swapMs\":%.2f,\"res\":[%d,%d],\"wantsMouse\":%s,\"touchEditor\":%s,\"lang\":\"%s\",\"hasPlayer\":%s,\"pos\":[%.1f,%.1f,%.1f],\"yaw\":%d,\"health\":%d,\"playerState\":\"%s\",\"execOut\":\"%s\"}\n",
                frame,
                static_cast<double>(fps),
                static_cast<double>(profTickMs),
                static_cast<double>(profRenderMs),
                static_cast<double>(profSwapMs),
                frameWidth,
                frameHeight,
                dx::extension::wantsMouse() ? "true" : "false",
                touchOverlay.isEditorOpen() ? "true" : "false",
                lang.c_str(),
                player ? "true" : "false",
                static_cast<double>(loc.x),
                static_cast<double>(loc.y),
                static_cast<double>(loc.z),
                yaw,
                health,
                pState.c_str(),
                lastExecOut.c_str()
            );

            std::string outPath = std::string(extPath) + "/debug_state.json";
            if (std::FILE* out = std::fopen(outPath.c_str(), "wb")) {
                std::fwrite(jsonBuf, 1, std::strlen(jsonBuf), out);
                std::fclose(out);
            }
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "OpenDE_Debug: %s", jsonBuf);
        }
#endif

        void begin()
        {
            for (const std::string& command : options.commands) {
                dx::console::execute(world ? world->player() : nullptr, command);
            }
            last = SDL_GetPerformanceCounter();
        }

        int run()
        {
            begin();
            while (running) {
                step();
            }
            return 0;
        }

        void recordBench(double perfFreqMs, uint64_t tTick0, uint64_t tTick1, uint64_t tRender0, uint64_t tRender1, uint64_t tSwap0, uint64_t tSwap1)
        {
            uint64_t prevSwap = benchLastSwap;
            benchLastSwap = tSwap1;
            if (benchWarmup < 0 || prevSwap == 0) {
                return;
            }
            if (!world || !world->player() || !pendingMap.empty()) {
                return;
            }
            if (benchWarmup > 0) {
                benchWarmup--;
                return;
            }
            auto ms = [perfFreqMs](uint64_t a, uint64_t b) { return static_cast<float>(static_cast<double>(b - a) / perfFreqMs); };
            const auto& st = renderer->stats();
            BenchSample s {};
            s.frame = ms(prevSwap, tSwap1);
            s.tick = ms(tTick0, tTick1);
            s.render = ms(tRender0, tRender1);
            s.swap = ms(tSwap0, tSwap1);
            s.sync = world->profSyncMs;
            s.actor = world->profActorMs;
            s.ai = world->profAiMs;
            s.lgt = st.lightsMs;
            s.occ = st.occludeMs;
            s.sky = st.skyMs;
            s.bsp = st.bspMs;
            s.act = st.actorsMs;
            s.actC = st.actCandMs;
            s.actE = st.actEmitMs;
            s.actF = st.actFlushMs;
            s.ssao = st.ssaoMs;
            s.fxaa = st.fxaaMs;
            s.ticked = world->profTickedCount;
            s.vmCalls = world->profTickVmCount;
            s.actN = st.actActors;
            s.actB = st.actBatches;
            s.dc = st.drawCalls;
            s.tri = st.triangles;
            s.lmUp = st.lightmapUploads;
            benchSamples.push_back(s);
            if (static_cast<int>(benchSamples.size()) < benchFrames) {
                return;
            }
            benchWarmup = -1;
            const size_t n = benchSamples.size();
            std::vector<float> frames;
            frames.reserve(n);
            BenchSample sum {};
            double ticked = 0, vmCalls = 0, actN = 0, actB = 0, dc = 0, tri = 0, lmUp = 0;
            for (const BenchSample& b : benchSamples) {
                frames.push_back(b.frame);
                sum.frame += b.frame; sum.tick += b.tick; sum.render += b.render; sum.swap += b.swap;
                sum.sync += b.sync; sum.actor += b.actor; sum.ai += b.ai;
                sum.lgt += b.lgt; sum.occ += b.occ; sum.sky += b.sky; sum.bsp += b.bsp;
                sum.act += b.act; sum.actC += b.actC; sum.actE += b.actE; sum.actF += b.actF;
                sum.ssao += b.ssao; sum.fxaa += b.fxaa;
                ticked += b.ticked; vmCalls += b.vmCalls; actN += b.actN; actB += b.actB;
                dc += b.dc; tri += b.tri; lmUp += b.lmUp;
            }
            std::sort(frames.begin(), frames.end());
            auto pct = [&frames, n](double p) { return static_cast<double>(frames[std::min(n - 1, static_cast<size_t>(p * static_cast<double>(n - 1) + 0.5))]); };
            const double inv = 1.0 / static_cast<double>(n);
            const double mean = static_cast<double>(sum.frame) * inv;
            char line[1024];
            std::snprintf(line, sizeof(line),
                "label=%s n=%zu fps=%.2f frame[mean=%.2f p50=%.2f p95=%.2f p99=%.2f max=%.2f] tick=%.2f[sync=%.2f actor=%.2f ai=%.2f ticked=%.0f vm=%.0f] render=%.2f[lgt=%.2f occ=%.2f sky=%.2f bsp=%.2f act=%.2f c=%.2f e=%.2f f=%.2f ssao=%.2f fxaa=%.2f actors=%.1f batches=%.1f dc=%.0f tri=%.0f lmUp=%.1f] swap=%.2f",
                benchLabel.c_str(), n, 1000.0 / mean, mean, pct(0.50), pct(0.95), pct(0.99), static_cast<double>(frames.back()),
                static_cast<double>(sum.tick) * inv, static_cast<double>(sum.sync) * inv, static_cast<double>(sum.actor) * inv, static_cast<double>(sum.ai) * inv, ticked * inv, vmCalls * inv,
                static_cast<double>(sum.render) * inv, static_cast<double>(sum.lgt) * inv, static_cast<double>(sum.occ) * inv, static_cast<double>(sum.sky) * inv, static_cast<double>(sum.bsp) * inv,
                static_cast<double>(sum.act) * inv, static_cast<double>(sum.actC) * inv, static_cast<double>(sum.actE) * inv, static_cast<double>(sum.actF) * inv,
                static_cast<double>(sum.ssao) * inv, static_cast<double>(sum.fxaa) * inv,
                actN * inv, actB * inv, dc * inv, tri * inv, lmUp * inv,
                static_cast<double>(sum.swap) * inv);
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "OpenDE_Bench: %s", line);
#ifdef __ANDROID__
            if (std::FILE* out = std::fopen("/storage/emulated/0/OpenDE/bench_results.txt", "ab")) {
                std::fprintf(out, "%s\n", line);
                std::fclose(out);
            } else if (const char* extPath = SDL_AndroidGetExternalStoragePath()) {
                std::string outPath = std::string(extPath) + "/bench_results.txt";
                if (std::FILE* out2 = std::fopen(outPath.c_str(), "ab")) {
                    std::fprintf(out2, "%s\n", line);
                    std::fclose(out2);
                }
            }
#endif
            benchSamples.clear();
        }

        void step()
        {
            processEvents();
            uint64_t now = SDL_GetPerformanceCounter();
            float delta = static_cast<float>(now - last) / static_cast<float>(SDL_GetPerformanceFrequency());
            last = now;
            if (options.frames >= 0) {
                delta = 1.0f / 30.0f;
            }
            pollGamepad(delta);
            touchOverlay.emitAxes(touchCallbacks());
            bool modal = dx::extension::isModalOpen();
            if ((modal && !wasModalOpen) || touchOverlay.isEditorOpen()) {
                dx::console::releaseAllKeys();
                resetTouchControls();
            }
            wasModalOpen = modal;
            bool gui = dx::extension::wantsMouse() || modal || touchOverlay.isEditorOpen();
            if (gui != guiMode) {
                guiMode = gui;
                SDL_SetRelativeMouseMode(gui ? SDL_FALSE : SDL_TRUE);
#ifndef __ANDROID__
                float cursorX = 0;
                float cursorY = 0;
                if (gui && dx::extension::cursorPosition(cursorX, cursorY)) {
                    int windowWidth = 0;
                    int windowHeight = 0;
                    SDL_GetWindowSize(window, &windowWidth, &windowHeight);
                    int drawWidth = 0;
                    int drawHeight = 0;
                    SDL_GL_GetDrawableSize(window, &drawWidth, &drawHeight);
                    if (drawWidth > 0 && drawHeight > 0) {
                        SDL_WarpMouseInWindow(window, static_cast<int>(cursorX * static_cast<float>(windowWidth) / static_cast<float>(drawWidth)), static_cast<int>(cursorY * static_cast<float>(windowHeight) / static_cast<float>(drawHeight)));
                    }
                }
#endif
            }
            viewportTime += static_cast<double>(delta);
            static float cachedBrightness = 0.5f;
            if (gui || frame == 0 || (frame % 30) == 0) {
                applyPlayerSpeed();
                dx::deusex::applyVanillaFixes();
                applyRenderOptions();
                cachedBrightness = static_cast<float>(std::atof(dx::config::system().get(videoSection(), "Brightness").value_or("0.5").c_str()));
            }
            const double perfFreqMs = static_cast<double>(SDL_GetPerformanceFrequency()) / 1000.0;
            uint64_t tTick0 = SDL_GetPerformanceCounter();
            {
                DX_TRACE_SCOPE("DX_Tick");
                if (world && !touchOverlay.isEditorOpen()) {
                    world->tick(delta);
                }
                dx::deusex::flushPendingLocalization(world.get());
                dx::extension::tick(delta);
#ifdef __ANDROID__
                {
                    bool wantTextInput = !lastInputWasGamepad && dx::extension::isEditFocused();
                    if (wantTextInput != textInputRequested) {
                        textInputRequested = wantTextInput;
                        if (wantTextInput) {
                            SDL_StartTextInput();
                        } else {
                            SDL_StopTextInput();
                        }
                    }
                }
#endif
                dx::UObject* console = scriptConsole();
                if (console) {
                    dx::vm::Call(console, dx::Name("Tick")).f(delta).run();
                    int consoleHeight = 0;
                    SDL_GL_GetDrawableSize(window, nullptr, &consoleHeight);
                    dx::console::ConsoleLines(console) = static_cast<int32_t>(std::min(dx::console::ConsolePos(console) * static_cast<float>(consoleHeight), static_cast<float>(consoleHeight)));
                }
                handleTravel();
            }
            uint64_t tTick1 = SDL_GetPerformanceCounter();
            int width = 0;
            int height = 0;
            SDL_GL_GetDrawableSize(window, &width, &height);
            uint64_t tRender0 = SDL_GetPerformanceCounter();
            {
                DX_TRACE_SCOPE("DX_Render");
                render(width, height);
                renderer->applyBrightness(cachedBrightness);
            }
            uint64_t tRender1 = SDL_GetPerformanceCounter();
            dx::audio::update(viewLocation, viewRotation, delta);
            if (!pendingShot.empty()) {
                std::vector<uint8_t> pixels;
                int shotWidth = 0;
                int shotHeight = 0;
                renderer->readPixels(pixels, shotWidth, shotHeight);
                saveBmp(pendingShot, pixels, shotWidth, shotHeight);
                pendingShot.clear();
            }
            frame++;
#ifdef __ANDROID__
            pollAndroidDebugBridge(delta);
#endif
            for (const auto& [actionFrame, action] : options.actions) {
                if (actionFrame == frame) {
                    scriptedAction(action);
                }
            }
            if (options.frames >= 0 && frame >= options.frames) {
                if (!options.screenshot.empty()) {
                    std::vector<uint8_t> pixels;
                    int shotWidth = 0;
                    int shotHeight = 0;
                    renderer->readPixels(pixels, shotWidth, shotHeight);
                    saveBmp(options.screenshot, pixels, shotWidth, shotHeight);
                }
                running = false;
            }
            uint64_t tSwap0 = SDL_GetPerformanceCounter();
            {
                DX_TRACE_SCOPE("DX_Swap");
                SDL_GL_SwapWindow(window);
            }
            uint64_t tSwap1 = SDL_GetPerformanceCounter();
            auto ema = [](float prev, float sample) { return prev <= 0.001f ? sample : (prev * 0.9f + sample * 0.1f); };
            profTickMs = ema(profTickMs, static_cast<float>(static_cast<double>(tTick1 - tTick0) / perfFreqMs));
            profRenderMs = ema(profRenderMs, static_cast<float>(static_cast<double>(tRender1 - tRender0) / perfFreqMs));
            profSwapMs = ema(profSwapMs, static_cast<float>(static_cast<double>(tSwap1 - tSwap0) / perfFreqMs));
            if (perfOverlay.mode() != game::PerfOverlayMode::Off && perfLastSwap != 0) {
                const auto& st = renderer->stats();
                game::PerfOverlaySample ps {};
                ps.frameMs = static_cast<float>(static_cast<double>(tSwap1 - perfLastSwap) / perfFreqMs);
                ps.tickMs = profTickMs;
                ps.renderMs = profRenderMs;
                ps.swapMs = profSwapMs;
                ps.tickActorMs = world ? world->profActorMs : 0.0f;
                ps.tickAiMs = world ? world->profAiMs : 0.0f;
                ps.tickedActors = world ? world->profTickedCount : 0;
                ps.lightsMs = st.lightsMs;
                ps.occludeMs = st.occludeMs;
                ps.skyMs = st.skyMs;
                ps.bspMs = st.bspMs;
                ps.actorsMs = st.actorsMs;
                ps.actEmitMs = st.actEmitMs;
                ps.actFlushMs = st.actFlushMs;
                ps.ssaoMs = st.ssaoMs;
                ps.fxaaMs = st.fxaaMs;
                ps.actActors = st.actActors;
                ps.actBatches = st.actBatches;
                ps.drawCalls = st.drawCalls;
                ps.triangles = st.triangles;
                ps.meshFast = st.meshFast;
                ps.meshFull = st.meshFull;
                ps.meshRebuild = st.meshRebuild;
                ps.width = width;
                ps.height = height;
                perfOverlay.pushSample(ps);
            }
            perfLastSwap = tSwap1;
            recordBench(perfFreqMs, tTick0, tTick1, tRender0, tRender1, tSwap0, tSwap1);
#ifdef __ANDROID__
            if ((frame % 120) == 0) {
                float fps = delta > 0.0001f ? (1.0f / delta) : 0.0f;
                const auto& st = renderer->stats();
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "OpenDE_Perf: frame=%d fps=%.1f tick=%.2f[s=%.2f a=%.2f ai=%.2f n=%d] render=%.2f (lgt=%.2f occ=%.2f sky=%.2f bsp=%.2f act=%.2f[c=%.2f e=%.2f f=%.2f n=%d b=%d m=%d/%d/%d(g=%d u=%d l=%d)] cor=%.2f ssao=%.2f fxaa=%.2f dc=%d tri=%d lmUp=%d) swap=%.2f res=%dx%d",
                    frame, static_cast<double>(fps),
                    static_cast<double>(profTickMs),
                    static_cast<double>(world ? world->profSyncMs : 0.0f),
                    static_cast<double>(world ? world->profActorMs : 0.0f),
                    static_cast<double>(world ? world->profAiMs : 0.0f),
                    world ? world->profTickedCount : 0,
                    static_cast<double>(profRenderMs),
                    static_cast<double>(st.lightsMs), static_cast<double>(st.occludeMs),
                    static_cast<double>(st.skyMs), static_cast<double>(st.bspMs),
                    static_cast<double>(st.actorsMs),
                    static_cast<double>(st.actCandMs), static_cast<double>(st.actEmitMs), static_cast<double>(st.actFlushMs),
                    st.actActors, st.actBatches,
                    st.meshFast, st.meshFull, st.meshRebuild, st.meshRebGeom, st.meshRebUncache, st.meshRebLight,
                    static_cast<double>(st.coronasMs),
                    static_cast<double>(st.ssaoMs), static_cast<double>(st.fxaaMs),
                    st.drawCalls, st.triangles, st.lightmapUploads,
                    static_cast<double>(profSwapMs), width, height);
                if (world && world->profDetail) {
                    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "OpenDE_Tick: pVm=%.2f aVm=%.2f cnt=%d top=%s(%.2f) phys=%.2f",
                        static_cast<double>(world->profTickPlayerVmMs),
                        static_cast<double>(world->profTickActorVmMs),
                        world->profTickVmCount,
                        world->profTickTopClass ? world->profTickTopClass : "",
                        static_cast<double>(world->profTickTopMs),
                        static_cast<double>(world->profTickPhysMs));
                    if (!world->profClassStats.empty()) {
                        std::vector<std::pair<dx::UClass*, std::pair<float, int>>> ranked(world->profClassStats.begin(), world->profClassStats.end());
                        std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) { return a.second.first > b.second.first; });
                        std::string summary;
                        for (size_t i = 0; i < std::min<size_t>(5, ranked.size()); i++) {
                            char buf[96];
                            std::snprintf(buf, sizeof(buf), "%s(%.2f/%d) ", ranked[i].first->name.c_str(), static_cast<double>(ranked[i].second.first), ranked[i].second.second);
                            summary += buf;
                        }
                        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "OpenDE_TickTop: %s", summary.c_str());
                    }
                }
            }
#endif
            limitFrameRate();
        }

        bool isRunning() const
        {
            return running;
        }

    private:
        SDL_Window* window;
        Options options;
        std::unique_ptr<dx::SceneRenderer> renderer;
        std::unique_ptr<dx::World> world;
        dx::UObject* canvasObject = nullptr;
        struct BenchSample {
            float frame, tick, render, swap, sync, actor, ai, lgt, occ, sky, bsp, act, actC, actE, actF, ssao, fxaa;
            int ticked, vmCalls, actN, actB, dc, tri, lmUp;
        };
        int benchWarmup = -1;
        long long lastDebugCmdStamp = 0;
        uint64_t benchLastSwap = 0;
        int benchFrames = 0;
        std::string benchLabel;
        std::vector<BenchSample> benchSamples;
        std::string pendingMap;
        int32_t pendingTravelType = dx::TRAVEL_Partial;
        std::string pendingShot;
        dx::Vector viewLocation;
        dx::Rotator viewRotation;
        float viewFov = 90;
        int frameWidth = 1;
        double viewportTime = 0;
        int frameHeight = 1;
        bool running = true;
        uint64_t last = 0;
        int frame = 0;
        bool guiMode = true;
        bool wasModalOpen = false;
        SDL_GameController* controller = nullptr;
        SDL_JoystickID controllerId = -1;
        uint32_t prevControllerButtons = 0;
        float prevLeftStickX = 0.0f;
        float prevLeftStickY = 0.0f;
        float prevRightStickX = 0.0f;
        float prevRightStickY = 0.0f;
        float prevLeftTrigger = 0.0f;
        float prevRightTrigger = 0.0f;
        uint8_t repeatKey = dx::IK_None;
        uint8_t prevStickRepeatKey = dx::IK_None;
        float repeatTimer = 0.0f;
        bool hasMenuFinger = false;
        SDL_FingerID menuFinger = 0;
        bool textInputRequested = false;
        bool lastInputWasGamepad = false;
        game::TouchOverlayManager touchOverlay;
        game::PerfOverlay perfOverlay;
        uint64_t perfLastSwap = 0;
        dx::UClass* touchEditorChoiceClass = nullptr;
        float appliedPlayerSpeed = 1.0f;
        int appliedVSync = -1;
        uint64_t frameDeadline = 0;
        float profTickMs = 0.0f;
        float profRenderMs = 0.0f;
        float profSwapMs = 0.0f;
        std::unordered_map<dx::UClass*, std::pair<float, float>> basePlayerSpeeds;
    };
}

int main(int argc, char** argv)
{
    Options options = parseOptions(argc, argv);
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK) != 0) {
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
            std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
            return 1;
        }
    }
#if defined(__EMSCRIPTEN__) || defined(__ANDROID__)
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
#endif
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_Window* window = SDL_CreateWindow("Deus Ex", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, options.width, options.height, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!window) {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GLContext context = SDL_GL_CreateContext(window);
    if (!context) {
        std::fprintf(stderr, "SDL_GL_CreateContext failed: %s\n", SDL_GetError());
        return 1;
    }
    if (!gladLoadGL(reinterpret_cast<GLADloadfunc>(SDL_GL_GetProcAddress))) {
        std::fprintf(stderr, "failed to load OpenGL\n");
        return 1;
    }
    SDL_GL_SetSwapInterval(options.frames >= 0 ? 0 : 1);
#ifndef __ANDROID__
    SDL_StartTextInput();
#endif
    SDL_ShowCursor(SDL_DISABLE);
    dx::vm::setCriticalHandler([window](const std::string& message) {
        std::fprintf(stderr, "Critical Error: %s\n", message.c_str());
        std::fflush(stderr);
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Critical Error", message.c_str(), window);
        std::_Exit(1);
    });
#ifdef __EMSCRIPTEN__
    try {
        Game* game = new Game(window, options);
        if (!game->start()) {
            std::fprintf(stderr, "failed to start game\n");
            return 1;
        }
        game->begin();
        emscripten_set_click_callback("#fullscreen", nullptr, true, [](int, const EmscriptenMouseEvent*, void*) -> EM_BOOL {
            dx::console::toggleFullscreenHook();
            return true;
        });
        emscripten_set_main_loop_arg(
            [](void* argument) {
                Game* running = static_cast<Game*>(argument);
                try {
                    running->step();
                } catch (const std::exception& error) {
                    std::fprintf(stderr, "error: %s\n", error.what());
                    emscripten_cancel_main_loop();
                    return;
                }
                if (!running->isRunning()) {
                    emscripten_cancel_main_loop();
                }
            },
            game, 0, true);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "error: %s\n", error.what());
        return 1;
    }
    return 0;
#else
    int result = 0;
    try {
        Game game(window, options);
        if (!game.start()) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "failed to start game");
            std::fprintf(stderr, "failed to start game\n");
            result = 1;
        } else {
            result = game.run();
        }
    } catch (const std::exception& error) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "error: %s", error.what());
        std::fprintf(stderr, "error: %s\n", error.what());
        result = 1;
    }
    dx::audio::shutdown();
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return result;
#endif
}
