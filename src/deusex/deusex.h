#pragma once

#include "core/object.h"

#include <functional>
#include <string>
#include <string_view>

namespace dx {
    class World;

    namespace deusex {
        void levelLoaded(World& world);
        void applyVanillaFixes();
        void reloadWorldLocalization(World* world);
        void flushPendingLocalization(World* world);
        bool scriptedPawnTick(World& world, UObject* pawn, float deltaSeconds);
        bool renderIterate(World& world, UObject* owner, const std::function<void(UObject* item)>& emit);
        void spliceMenuBackground(UObject* menu, int cut, int resume);
        void insertMenuButton(UObject* menu, int index, const std::string& label, UClass* invoke);
        void configureControllerSettingsMenu(UObject* menu, UClass* extraGameplay, UClass* extraGraphics);
        void refreshControllerSettingsMenu(UObject* menu, UClass* extraGameplay, UClass* extraGraphics);
        void configureLanguageChoice(UClass* choiceClass, const std::function<void(std::string_view newLanguage)>& onChanged);
        void configureTouchEditorChoice(UClass* choiceClass, const std::function<void()>& onOpen);
        void refreshOpenMenus();
        void registerUiTextTranslator();
        std::string utf8ToCp1252(std::string_view utf8);
    }
}
