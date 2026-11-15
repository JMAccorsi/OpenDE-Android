#include "core/config.h"
#include "core/objects.h"
#include "core/vm.h"
#include "deusex/deusex.h"
#include "engine/input.h"
#include "engine/props_extension.h"
#include "engine/resources.h"
#include "extension/extension.h"
#include "extension/windows.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace dx::deusex {
    void spliceMenuBackground(UObject* menu, int cut, int resume)
    {
        int cols = *reinterpret_cast<int32_t*>(menu->propertyAddress(Name("textureCols")));
        int rows = *reinterpret_cast<int32_t*>(menu->propertyAddress(Name("textureRows")));
        std::vector<UTexture*> tiles;
        int height = 0;
        for (int i = 0; i < cols * rows; i++) {
            UObject* tile = *reinterpret_cast<UObject**>(menu->propertyAddress(Name("clientTextures"), i));
            objects::ensureLoaded(tile);
            UTexture* utex = cast<UTexture>(tile);
            if (!utex) {
                return;
            }
            tiles.push_back(utex);
            height = std::max(height, (i / cols) * 256 + static_cast<int>(utex->height()));
        }
        int width = cols * 256;
        std::vector<uint8_t> pixels(static_cast<size_t>(width * height) * 4);
        for (int i = 0; i < cols * rows; i++) {
            UTexture* tile = tiles[static_cast<size_t>(i)];
            bool raw = tile->format() == 5;
            std::vector<uint8_t> rgba = raw ? std::vector<uint8_t>() : tile->rgba(0);
            int tileWidth = static_cast<int>(tile->width());
            for (int y = 0; y < static_cast<int>(tile->height()); y++) {
                for (int x = 0; x < tileWidth; x++) {
                    size_t from = static_cast<size_t>(y * tileWidth + x);
                    uint8_t* to = &pixels[(static_cast<size_t>((i / cols) * 256 + y) * static_cast<size_t>(width) + static_cast<size_t>((i % cols) * 256 + x)) * 4];
                    if (raw) {
                        std::copy_n(tile->mips[0].data.begin() + static_cast<std::ptrdiff_t>(from * 4), 4, to);
                    } else if (tile->mips[0].data[from] != 0) {
                        to[0] = rgba[from * 4 + 2];
                        to[1] = rgba[from * 4 + 1];
                        to[2] = rgba[from * 4 + 0];
                        to[3] = 255;
                    }
                }
            }
        }
        std::vector<int> sourceRows;
        for (int y = 0; y < cut; y++) {
            sourceRows.push_back(y);
        }
        for (int y = resume; y < height; y++) {
            sourceRows.push_back(y);
        }
        int maxTiles = menu->cls->findProperty(Name("clientTextures"))->arrayDim;
        int maxRows = std::max(1, maxTiles / cols);
        int newHeight = std::min(static_cast<int>(sourceRows.size()), maxRows * 256);
        int newRows = std::min((newHeight + 255) / 256, maxRows);
        UClass* textureClass = objects::findClass("Engine.Texture");
        for (int i = 0; i < maxTiles; i++) {
            *reinterpret_cast<UObject**>(menu->propertyAddress(Name("clientTextures"), i)) = nullptr;
        }
        for (int row = 0; row < newRows; row++) {
            for (int col = 0; col < cols; col++) {
                UTexture* source = tiles[static_cast<size_t>(col)];
                UTexture* texture = cast<UTexture>(objects::newObject(textureClass, source->outer, Name(), RF_Public | RF_Transient));
                *texture->propertyAddress(Name("Format")) = 5;
                *reinterpret_cast<uint32_t*>(texture->propertyAddress(Name("bNoSmooth"))) = *reinterpret_cast<uint32_t*>(source->propertyAddress(Name("bNoSmooth")));
                TextureMip mip;
                mip.width = source->width();
                mip.height = static_cast<uint32_t>(std::min(256, newHeight - row * 256));
                for (uint32_t y = 0; y < mip.height; y++) {
                    size_t from = (static_cast<size_t>(sourceRows[static_cast<size_t>(row * 256) + y]) * static_cast<size_t>(width) + static_cast<size_t>(col * 256)) * 4;
                    mip.data.insert(mip.data.end(), pixels.begin() + static_cast<std::ptrdiff_t>(from), pixels.begin() + static_cast<std::ptrdiff_t>(from + mip.width * 4));
                }
                texture->mips.push_back(std::move(mip));
                if (UObject** slot = reinterpret_cast<UObject**>(menu->propertyAddress(Name("clientTextures"), row * cols + col))) {
                    *slot = texture;
                }
            }
        }
        *reinterpret_cast<int32_t*>(menu->propertyAddress(Name("textureRows"))) = newRows;
    }

    void insertMenuButton(UObject* menu, int index, const std::string& label, UClass* invoke)
    {
        UStruct* button = menu->cls->findProperty(Name("buttonDefaults"))->structType;
        size_t yOffset = button->findProperty(Name("Y"))->offset;
        size_t invokeOffset = button->findProperty(Name("Invoke"))->offset;
        bool names = menu->cls->findProperty(Name("ButtonNames")) != nullptr;
        for (int i = 9; i > index; i--) {
            button->copyValue(menu->propertyAddress(Name("buttonDefaults"), i), menu->propertyAddress(Name("buttonDefaults"), i - 1));
            if (names) {
                *reinterpret_cast<std::string*>(menu->propertyAddress(Name("ButtonNames"), i)) = *reinterpret_cast<std::string*>(menu->propertyAddress(Name("ButtonNames"), i - 1));
            }
        }
        button->copyValue(menu->propertyAddress(Name("buttonDefaults"), index), menu->propertyAddress(Name("buttonDefaults"), index - 1));
        if (names) {
            *reinterpret_cast<std::string*>(menu->propertyAddress(Name("ButtonNames"), index)) = label;
        } else {
            *reinterpret_cast<std::string*>(menu->propertyAddress(Name("buttonDefaults"), index) + button->findProperty(Name("Text"))->offset) = label;
        }
        *reinterpret_cast<UObject**>(menu->propertyAddress(Name("buttonDefaults"), index) + invokeOffset) = invoke;
        int gap = *reinterpret_cast<int32_t*>(menu->propertyAddress(Name("buttonDefaults"), 1) + yOffset) - *reinterpret_cast<int32_t*>(menu->propertyAddress(Name("buttonDefaults"), 0) + yOffset);
        for (int i = index; i < 10; i++) {
            *reinterpret_cast<int32_t*>(menu->propertyAddress(Name("buttonDefaults"), i) + yOffset) += gap;
        }
        int slot = *reinterpret_cast<int32_t*>(menu->propertyAddress(Name("buttonDefaults"), index) + yOffset);
        spliceMenuBackground(menu, slot, slot - gap);
        *reinterpret_cast<int32_t*>(menu->propertyAddress(Name("ClientHeight"))) += gap;
    }

    namespace {
        constexpr int32_t kMenuButtonStartY = 13;
        constexpr int32_t kMenuButtonStride = 36;
        // Bottom "Previous Menu" slot after inserting 4 upper rows (144 px) before Y=193
        // preserves the divider notch at Y=367..409 and bottom panel at Y=410..451.
        constexpr int32_t kControllerMenuExtraHeight = 144;
        constexpr int32_t kControllerMenuPrevButtonY = 266 + kControllerMenuExtraHeight;
        constexpr int32_t kControllerMenuClientHeight = 308 + kControllerMenuExtraHeight;
        constexpr uint8_t kMenuActionScreen = 1;
        constexpr uint8_t kMenuActionPrevious = 2;

        // Temporarily unsets FUNC_Native on a hooked UnrealScript function so we can
        // invoke its original bytecode cleanly for classes/screens we do not override.
        struct NativeFlagGuard {
            UFunction* function;
            uint32_t savedFlags;

            explicit NativeFlagGuard(UFunction* fn)
                : function(fn)
                , savedFlags(fn->functionFlags)
            {
                function->functionFlags &= ~FUNC_Native;
            }

            ~NativeFlagGuard()
            {
                function->functionFlags = savedFlags;
            }
        };

        struct ControllerMenuState {
            UClass* settingsClass = nullptr;
            UClass* menuButtonClass = nullptr;
            UClass* langChoiceClass = nullptr;
            UClass* touchEditorChoiceClass = nullptr;
            UClass* controllerRootClass = nullptr;
            UClass* infoButtonClass = nullptr;
            UClass* choiceClass = nullptr;
            UClass* customizeKeysClass = nullptr;
            UClass* actionButtonClass = nullptr;
            UClass* screenClass = nullptr;
            UClass* optionsNavClass = nullptr;
            UClass* extraGameplayClass = nullptr;
            UClass* extraGraphicsClass = nullptr;
            UFunction* superInitWindow = nullptr;
            UFunction* superButtonActivated = nullptr;
            UFunction* menuButtonActivated = nullptr;
            UFunction* handleDPad = nullptr;
            UFunction* optionsHandleDPad = nullptr;
            UFunction* optionsIsEnabled = nullptr;
            UFunction* optionsEnterActionBar = nullptr;
            UFunction* optionsHandleActivate = nullptr;
            UFunction* rootVirtualKeyPressed = nullptr;
            UFunction* registerNavControllers = nullptr;
            UFunction* screenVirtualKeyPressed = nullptr;
            UFunction* enumLoadSetting = nullptr;
            UFunction* enumSaveSetting = nullptr;
            UFunction* enumSetValue = nullptr;
            std::function<void(std::string_view)> onLanguageChanged;
            std::function<void()> onOpenTouchEditor;
            bool refreshingLanguage = false;
            std::string previousLabel = "Previous Menu";
        };

        ControllerMenuState& controllerMenuState()
        {
            static ControllerMenuState state;
            return state;
        }

        bool runBoolCall(vm::Call& call)
        {
            call.run();
            uint8_t* rv = call.returnValue();
            return rv && (*reinterpret_cast<uint32_t*>(rv) != 0);
        }

        UObject* readObjectProperty(UObject* object, Name name, int index = 0)
        {
            uint8_t* addr = object ? object->propertyAddress(name, index) : nullptr;
            return addr ? *reinterpret_cast<UObject**>(addr) : nullptr;
        }

        extension::UWindow* findExtraPrevButton(UObject* menu)
        {
            const ControllerMenuState& state = controllerMenuState();
            if (!menu || !state.menuButtonClass) {
                return nullptr;
            }
            auto* winClient = cast<extension::UWindow>(readObjectProperty(menu, Name("winClient")));
            if (!winClient || winClient->destroyed) {
                return nullptr;
            }
            std::array<UObject*, 10> known {};
            for (int i = 0; i < 10; ++i) {
                known[static_cast<size_t>(i)] = readObjectProperty(menu, Name("winButtons"), i);
            }
            for (extension::UWindow* child = winClient->firstChild(); child; child = child->nextSibling()) {
                if (!child->destroyed && child->isA(state.menuButtonClass)) {
                    if (std::find(known.begin(), known.end(), static_cast<UObject*>(child)) == known.end()) {
                        return child;
                    }
                }
            }
            return nullptr;
        }

        void collectSettingsButtons(UObject* screen, std::array<extension::UWindow*, 11>& out)
        {
            for (int i = 0; i < 10; ++i) {
                out[static_cast<size_t>(i)] = cast<extension::UWindow>(readObjectProperty(screen, Name("winButtons"), i));
            }
            out[10] = findExtraPrevButton(screen);
        }

        void nativeMenuSettingsInitWindow(UObject* object, Frame& stack, void*)
        {
            vm::finish(stack);
            const ControllerMenuState& state = controllerMenuState();
            if (!object || !state.superInitWindow) {
                return;
            }
            vm::Call(object, state.superInitWindow).run();
            auto* winClient = cast<extension::UWindow>(readObjectProperty(object, Name("winClient")));
            uint8_t* xAddr = object->propertyAddress(Name("buttonXPos"));
            uint8_t* wAddr = object->propertyAddress(Name("buttonWidth"));
            if (!state.menuButtonClass || !winClient || winClient->destroyed || !xAddr || !wAddr) {
                return;
            }
            extension::UWindow* prevButton = winClient->newChild(state.menuButtonClass, true);
            if (!prevButton) {
                return;
            }
            vm::Call(prevButton, Name("SetButtonText")).s(state.previousLabel).run();
            prevButton->x() = static_cast<float>(*reinterpret_cast<int32_t*>(xAddr));
            prevButton->y() = static_cast<float>(kControllerMenuPrevButtonY);
            prevButton->setWidth(static_cast<float>(*reinterpret_cast<int32_t*>(wAddr)));
        }

        void nativeMenuUIMenuButtonActivated(UObject* object, Frame& stack, void* result)
        {
            UObject* buttonPressed = vm::get<UObject*>(stack);
            vm::finish(stack);
            const ControllerMenuState& state = controllerMenuState();
            if (object && buttonPressed && state.settingsClass && object->isA(state.settingsClass)) {
                extension::UWindow* extraPrev = findExtraPrevButton(object);
                if (extraPrev && buttonPressed == static_cast<UObject*>(extraPrev)) {
                    if (state.superButtonActivated) {
                        vm::Call(object, state.superButtonActivated).obj(buttonPressed).run();
                    }
                    vm::Call(object, Name("ProcessMenuAction")).byte(kMenuActionPrevious).obj(nullptr).s("").run();
                    vm::retBool(result, true);
                    return;
                }
            }
            if (!object || !state.menuButtonActivated) {
                vm::retBool(result, false);
                return;
            }
            NativeFlagGuard guard(state.menuButtonActivated);
            vm::Call call(object, state.menuButtonActivated);
            call.obj(buttonPressed);
            vm::retBool(result, runBoolCall(call));
        }

        bool readBoolProp(UObject* object, Name name)
        {
            uint8_t* addr = object ? object->propertyAddress(name) : nullptr;
            return addr && (*reinterpret_cast<uint32_t*>(addr) != 0);
        }

        void writeBoolProp(UObject* object, Name name, bool value)
        {
            if (uint8_t* addr = object ? object->propertyAddress(name) : nullptr) {
                *reinterpret_cast<uint32_t*>(addr) = value ? 1u : 0u;
            }
        }

        void collectScreenActionButtons(UObject* screen, std::vector<extension::UWindow*>& out, extension::UWindow** outPrimary = nullptr)
        {
            out.clear();
            if (outPrimary) {
                *outPrimary = nullptr;
            }
            UProperty* abProp = (screen && screen->cls) ? screen->cls->findProperty(Name("actionButtons")) : nullptr;
            if (!abProp || !abProp->structType) {
                return;
            }
            UProperty* btnProp = abProp->structType->findProperty(Name("btn"));
            UProperty* alignProp = abProp->structType->findProperty(Name("align"));
            UProperty* actionProp = abProp->structType->findProperty(Name("Action"));
            if (!btnProp || !alignProp) {
                return;
            }
            int32_t count = std::min<int32_t>(abProp->arrayDim, 5);
            auto slotBtn = [&](int32_t i) -> extension::UWindow* {
                uint8_t* entry = screen->propertyAddress(Name("actionButtons"), i);
                UObject* obj = entry ? *reinterpret_cast<UObject**>(entry + btnProp->offset) : nullptr;
                auto* win = cast<extension::UWindow>(obj);
                return (win && !win->destroyed) ? win : nullptr;
            };
            auto slotAlign = [&](int32_t i) -> uint8_t {
                uint8_t* entry = screen->propertyAddress(Name("actionButtons"), i);
                return entry ? *(entry + alignProp->offset) : 0;
            };
            auto slotAction = [&](int32_t i) -> Name {
                if (!actionProp || !actionProp->enumType) {
                    return Name();
                }
                uint8_t* entry = screen->propertyAddress(Name("actionButtons"), i);
                if (!entry) {
                    return Name();
                }
                uint8_t val = *(entry + actionProp->offset);
                return val < actionProp->enumType->names.size() ? actionProp->enumType->names[val] : Name();
            };
            auto isLiveBtn = [](extension::UWindow* btn) -> bool {
                return btn && !btn->destroyed && btn->visible() && btn->sensitive();
            };
            for (int32_t i = 0; i < count; ++i) {
                if (slotAlign(i) != extension::HALIGN_Right) {
                    if (extension::UWindow* win = slotBtn(i)) {
                        out.push_back(win);
                    }
                }
            }
            for (int32_t i = count - 1; i >= 0; --i) {
                if (slotAlign(i) == extension::HALIGN_Right) {
                    if (extension::UWindow* win = slotBtn(i)) {
                        out.push_back(win);
                    }
                }
            }
            if (outPrimary) {
                for (int32_t i = 0; i < count; ++i) {
                    if (slotAction(i) == Name("AB_OK")) {
                        if (extension::UWindow* okBtn = slotBtn(i); isLiveBtn(okBtn)) {
                            *outPrimary = okBtn;
                            return;
                        }
                    }
                }
                extension::UWindow* fallbackSlot = (count > 1) ? slotBtn(1) : nullptr;
                if (isLiveBtn(fallbackSlot)) {
                    *outPrimary = fallbackSlot;
                } else {
                    for (extension::UWindow* btn : out) {
                        if (isLiveBtn(btn)) {
                            *outPrimary = btn;
                            break;
                        }
                    }
                }
            }
        }

        void nativeMenuUIWindowButtonActivated(UObject* object, Frame& stack, void* result)
        {
            UObject* buttonPressed = vm::get<UObject*>(stack);
            vm::finish(stack);
            const ControllerMenuState& state = controllerMenuState();
            if (object && buttonPressed && state.infoButtonClass && state.choiceClass && buttonPressed->isA(state.infoButtonClass)) {
                auto* btnWin = cast<extension::UWindow>(buttonPressed);
                for (extension::UWindow* p = btnWin ? btnWin->parent() : nullptr; p; p = p->parent()) {
                    if (p->isA(state.choiceClass)) {
                        if (buttonPressed == readObjectProperty(p, Name("btnInfo")) && !readObjectProperty(p, Name("winScaleManager"))) {
                            auto* btnAction = cast<extension::UWindow>(readObjectProperty(p, Name("btnAction")));
                            if (btnAction && !btnAction->destroyed && btnAction->visible() && btnAction->sensitive()) {
                                btnAction->setFocusWindow(btnAction);
                                vm::Call(btnAction, Name("PressButton")).run();
                                vm::retBool(result, true);
                                return;
                            }
                        }
                        if (state.touchEditorChoiceClass && p->isA(state.touchEditorChoiceClass) && state.onOpenTouchEditor) {
                            state.onOpenTouchEditor();
                            vm::retBool(result, true);
                            return;
                        }
                        break;
                    }
                }
            }
            if (!object || !state.superButtonActivated) {
                vm::retBool(result, false);
                return;
            }
            NativeFlagGuard guard(state.superButtonActivated);
            vm::Call call(object, state.superButtonActivated);
            call.obj(buttonPressed);
            vm::retBool(result, runBoolCall(call));
        }

        void nativeOptionsIsEnabled(UObject*, Frame& stack, void* result)
        {
            UObject* choice = vm::get<UObject*>(stack);
            vm::finish(stack);
            auto* win = cast<extension::UWindow>(choice);
            vm::retBool(result, win && !win->destroyed && win->sensitive() && win->effectivelyVisible());
        }

        void setOptionsNavActionBarState(UObject* nav, const std::vector<extension::UWindow*>& btns, int32_t idx)
        {
            int32_t count = std::min<int32_t>(static_cast<int32_t>(btns.size()), 5);
            if (uint8_t* abCountAddr = nav->propertyAddress(Name("actionBtnCount"))) {
                *reinterpret_cast<int32_t*>(abCountAddr) = count;
            }
            for (int32_t i = 0; i < 5; ++i) {
                if (uint8_t* arrAddr = nav->propertyAddress(Name("actionBtns"), i)) {
                    *reinterpret_cast<UObject**>(arrAddr) = (i < count) ? btns[static_cast<size_t>(i)] : nullptr;
                }
            }
            writeBoolProp(nav, Name("bInActionBar"), true);
            if (uint8_t* idxAddr = nav->propertyAddress(Name("actionBtnIdx"))) {
                *reinterpret_cast<int32_t*>(idxAddr) = idx;
            }
            if (uint8_t* focusedAddr = nav->propertyAddress(Name("focused"))) {
                *reinterpret_cast<UObject**>(focusedAddr) = (idx >= 0 && idx < count) ? btns[static_cast<size_t>(idx)] : nullptr;
            }
        }

        void syncOptionsNavWithEngineFocus(UObject* nav)
        {
            const ControllerMenuState& state = controllerMenuState();
            UObject* screen = readObjectProperty(nav, Name("screen"));
            auto* screenWin = cast<extension::UWindow>(screen);
            extension::URootWindow* rootWin = screenWin ? screenWin->root() : nullptr;
            extension::UWindow* engineFocus = rootWin ? rootWin->focusWindow() : nullptr;
            if (!nav || !screen || !engineFocus || engineFocus->destroyed) {
                return;
            }

            if (state.actionButtonClass && engineFocus->isA(state.actionButtonClass)) {
                std::vector<extension::UWindow*> btns;
                collectScreenActionButtons(screen, btns);
                int32_t matchedIdx = -1;
                for (size_t i = 0; i < btns.size(); ++i) {
                    if (btns[i] == engineFocus) {
                        matchedIdx = static_cast<int32_t>(i);
                        break;
                    }
                }
                if (matchedIdx >= 0) {
                    setOptionsNavActionBarState(nav, btns, matchedIdx);
                    return;
                }
            }

            int32_t choiceCount = 0;
            if (uint8_t* countAddr = nav->propertyAddress(Name("choiceCount"))) {
                choiceCount = std::clamp<int32_t>(*reinterpret_cast<int32_t*>(countAddr), 0, 32);
            }
            for (int32_t i = 0; i < choiceCount; ++i) {
                UObject* ch = readObjectProperty(nav, Name("choices"), i);
                if (!ch) {
                    continue;
                }
                bool matched = false;
                for (extension::UWindow* w = engineFocus; w && !w->destroyed; w = w->parent()) {
                    if (w == static_cast<UObject*>(ch)) {
                        matched = true;
                        break;
                    }
                    if (w == screenWin) {
                        break;
                    }
                }
                if (matched) {
                    writeBoolProp(nav, Name("bInActionBar"), false);
                    if (uint8_t* idxAddr = nav->propertyAddress(Name("focusIndex"))) {
                        *reinterpret_cast<int32_t*>(idxAddr) = i;
                    }
                    if (uint8_t* focusedAddr = nav->propertyAddress(Name("focused"))) {
                        *reinterpret_cast<UObject**>(focusedAddr) = ch;
                    }
                    return;
                }
            }
        }

        void nativeOptionsHandleDPad(UObject* object, Frame& stack, void* result)
        {
            int32_t dx = vm::get<int32_t>(stack);
            int32_t dy = vm::get<int32_t>(stack);
            vm::finish(stack);
            const ControllerMenuState& state = controllerMenuState();
            if (!object || !state.optionsHandleDPad) {
                vm::retBool(result, false);
                return;
            }
            syncOptionsNavWithEngineFocus(object);
            NativeFlagGuard guard(state.optionsHandleDPad);
            vm::Call call(object, state.optionsHandleDPad);
            call.i(dx).i(dy);
            vm::retBool(result, runBoolCall(call));
        }

        void nativeOptionsEnterActionBar(UObject* object, Frame& stack, void* result)
        {
            vm::finish(stack);
            if (!object) {
                vm::retBool(result, false);
                return;
            }
            UObject* screen = readObjectProperty(object, Name("screen"));
            std::vector<extension::UWindow*> btns;
            extension::UWindow* primaryBtn = nullptr;
            collectScreenActionButtons(screen, btns, &primaryBtn);
            int32_t count = std::min<int32_t>(static_cast<int32_t>(btns.size()), 5);
            int32_t primaryIdx = -1;
            for (int32_t i = 0; i < count; ++i) {
                if (btns[static_cast<size_t>(i)] == primaryBtn) {
                    primaryIdx = i;
                    break;
                }
            }
            if (count == 0 || primaryIdx < 0) {
                vm::retBool(result, false);
                return;
            }
            setOptionsNavActionBarState(object, btns, primaryIdx);
            vm::Call(object, Name("SetFocus")).obj(btns[static_cast<size_t>(primaryIdx)]).run();
            vm::retBool(result, true);
        }

        void nativeOptionsHandleActivate(UObject* object, Frame& stack, void* result)
        {
            uint8_t button = vm::get<uint8_t>(stack);
            vm::finish(stack);
            const ControllerMenuState& state = controllerMenuState();
            if (!object || !state.optionsHandleActivate) {
                vm::retBool(result, false);
                return;
            }
            syncOptionsNavWithEngineFocus(object);
            NativeFlagGuard guard(state.optionsHandleActivate);
            vm::Call call(object, state.optionsHandleActivate);
            call.byte(button);
            vm::retBool(result, runBoolCall(call));
        }

        void nativeMenuUIScreenVirtualKeyPressed(UObject* object, Frame& stack, void* result)
        {
            uint8_t key = vm::get<uint8_t>(stack);
            bool repeat = vm::getBool(stack);
            vm::finish(stack);
            const ControllerMenuState& state = controllerMenuState();
            if (!object || !state.screenVirtualKeyPressed) {
                vm::retBool(result, false);
                return;
            }
            // Vanilla MenuUIScreenWindow leaves bHandled=True when IK_Left/IK_Right runs
            // with currentChoice==None, swallowing horizontal D-pad input in the action bar.
            if (key == IK_Left || key == IK_Right) {
                UObject* curChoice = readObjectProperty(object, Name("currentChoice"));
                auto* screenWin = cast<extension::UWindow>(object);
                extension::URootWindow* rootWin = screenWin ? screenWin->root() : nullptr;
                extension::UWindow* focus = rootWin ? rootWin->focusWindow() : nullptr;
                bool focusInActionButton = focus && state.actionButtonClass && focus->isA(state.actionButtonClass);
                if (!curChoice || focusInActionButton) {
                    vm::retBool(result, false);
                    return;
                }
            }
            NativeFlagGuard guard(state.screenVirtualKeyPressed);
            vm::Call call(object, state.screenVirtualKeyPressed);
            call.byte(key).b(repeat);
            vm::retBool(result, runBoolCall(call));
        }

        void registerExtraScreenNav(UObject* controllerRoot)
        {
            const ControllerMenuState& state = controllerMenuState();
            if (!controllerRoot || !state.controllerRootClass || !controllerRoot->isA(state.controllerRootClass) || !state.optionsNavClass) {
                return;
            }
            UFunction* findFn = state.controllerRootClass->findFunction(Name("FindNavIndex"));
            UFunction* regFn = state.controllerRootClass->findFunction(Name("RegisterNav"));
            if (!findFn || !regFn) {
                return;
            }
            for (UClass* extraCls : { state.extraGameplayClass, state.extraGraphicsClass }) {
                if (!extraCls) {
                    continue;
                }
                vm::Call findCall(controllerRoot, findFn);
                findCall.obj(extraCls);
                findCall.run();
                uint8_t* rv = findCall.returnValue();
                int32_t idx = rv ? *reinterpret_cast<int32_t*>(rv) : -1;
                if (idx < 0) {
                    vm::Call(controllerRoot, regFn).obj(extraCls).obj(state.optionsNavClass).run();
                }
            }
        }

        void nativeControllerRootRegisterNavControllers(UObject* object, Frame& stack, void*)
        {
            vm::finish(stack);
            const ControllerMenuState& state = controllerMenuState();
            if (object && state.registerNavControllers) {
                NativeFlagGuard guard(state.registerNavControllers);
                vm::Call(object, state.registerNavControllers).run();
            }
            registerExtraScreenNav(object);
        }

        bool handleCustomizeKeysNav(extension::URootWindow* rootWin, extension::UModalWindow* screen, uint8_t key, bool repeat)
        {
            if (!rootWin || !screen || readBoolProp(screen, Name("bWaitingForInput"))) {
                return false;
            }
            std::vector<extension::UWindow*> btns;
            extension::UWindow* primaryBtn = nullptr;
            collectScreenActionButtons(screen, btns, &primaryBtn);
            std::vector<extension::UWindow*> sensitiveBtns;
            for (extension::UWindow* btn : btns) {
                if (btn->visible() && btn->sensitive()) {
                    sensitiveBtns.push_back(btn);
                }
            }
            extension::UWindow* focus = rootWin->focusWindow();
            int abIdx = -1;
            for (size_t i = 0; i < sensitiveBtns.size(); ++i) {
                if (sensitiveBtns[i] == focus) {
                    abIdx = static_cast<int>(i);
                    break;
                }
            }
            auto* lstKeys = cast<extension::UListWindow>(readObjectProperty(screen, Name("lstKeys")));

            const bool isUp = (key == IK_JoyPovUp || key == IK_Up);
            const bool isDown = (key == IK_JoyPovDown || key == IK_Down);
            const bool isLeft = (key == IK_JoyPovLeft || key == IK_Left);
            const bool isRight = (key == IK_JoyPovRight || key == IK_Right);
            const bool isActivate = (key == IK_Joy1 || key == IK_Enter);
            const bool isBack = (key == IK_Joy2);

            if (isBack && !repeat) {
                vm::Call(screen, Name("CancelScreen")).run();
                return true;
            }

            if (isUp || isDown) {
                if (abIdx >= 0) {
                    if (lstKeys && !lstKeys->destroyed && lstKeys->visible() && lstKeys->sensitive()) {
                        rootWin->setRootFocusWindow(lstKeys);
                    }
                    return true;
                }
                if (lstKeys && !lstKeys->destroyed) {
                    int32_t count = static_cast<int32_t>(lstKeys->rows.size());
                    int32_t cur = listwindow::focusLine(lstKeys);
                    if (count <= 0 || (isDown && cur >= count - 1) || (isUp && cur <= 0)) {
                        if (primaryBtn) {
                            rootWin->setRootFocusWindow(primaryBtn);
                            return true;
                        }
                    }
                    if (focus != lstKeys && lstKeys->visible() && lstKeys->sensitive()) {
                        rootWin->setRootFocusWindow(lstKeys);
                    }
                    return lstKeys->virtualKeyPressed(isUp ? IK_Up : IK_Down, repeat);
                }
                if (primaryBtn) {
                    rootWin->setRootFocusWindow(primaryBtn);
                }
                return true;
            }

            if (isLeft || isRight) {
                if (abIdx >= 0 && !sensitiveBtns.empty()) {
                    size_t n = sensitiveBtns.size();
                    size_t nextIdx = isLeft ? ((static_cast<size_t>(abIdx) + n - 1) % n) : ((static_cast<size_t>(abIdx) + 1) % n);
                    rootWin->setRootFocusWindow(sensitiveBtns[nextIdx]);
                    return true;
                }
                if (primaryBtn) {
                    rootWin->setRootFocusWindow(primaryBtn);
                    return true;
                }
                return false;
            }

            if (isActivate && !repeat) {
                if (abIdx >= 0) {
                    vm::Call(sensitiveBtns[static_cast<size_t>(abIdx)], Name("PressButton")).run();
                    return true;
                }
                if (lstKeys && !lstKeys->destroyed) {
                    return lstKeys->virtualKeyPressed(IK_Enter, false);
                }
            }
            return false;
        }

        void nativeControllerRootVirtualKeyPressed(UObject* object, Frame& stack, void* result)
        {
            uint8_t key = vm::get<uint8_t>(stack);
            bool repeat = vm::getBool(stack);
            vm::finish(stack);
            const ControllerMenuState& state = controllerMenuState();
            if (!object || !state.rootVirtualKeyPressed) {
                vm::retBool(result, false);
                return;
            }

            UObject* keyboard = readObjectProperty(object, Name("keyboard"));
            bool oskOpen = readBoolProp(keyboard, Name("bOpen"));
            UObject* activeNav = readObjectProperty(object, Name("activeNav"));
            auto* rootWin = cast<extension::URootWindow>(object);
            extension::UWindow* focus = rootWin ? rootWin->focusWindow() : nullptr;
            bool editingText = (cast<extension::UEditWindow>(focus) != nullptr);

            uint8_t effectiveKey = key;
            if (!oskOpen && activeNav && !editingText) {
                switch (key) {
                case IK_Up:
                    effectiveKey = IK_JoyPovUp;
                    break;
                case IK_Down:
                    effectiveKey = IK_JoyPovDown;
                    break;
                case IK_Left:
                    effectiveKey = IK_JoyPovLeft;
                    break;
                case IK_Right:
                    effectiveKey = IK_JoyPovRight;
                    break;
                case IK_Enter:
                    effectiveKey = IK_Joy1;
                    break;
                default:
                    break;
                }
                if (effectiveKey == IK_JoyPovUp || effectiveKey == IK_JoyPovDown || effectiveKey == IK_JoyPovLeft || effectiveKey == IK_JoyPovRight || effectiveKey == IK_Joy1) {
                    vm::Call(object, Name("NoticeGamepadActivity")).run();
                }
            } else if (!oskOpen && !activeNav && !editingText && rootWin && state.customizeKeysClass) {
                for (extension::UWindow* child = rootWin->lastChild(); child; child = child->prevSibling()) {
                    if (child->visible() && cast<extension::UModalWindow>(child)) {
                        if (child->isA(state.customizeKeysClass)) {
                            if (key == IK_Up || key == IK_Down || key == IK_Left || key == IK_Right || key == IK_Enter ||
                                key == IK_JoyPovUp || key == IK_JoyPovDown || key == IK_JoyPovLeft || key == IK_JoyPovRight ||
                                key == IK_Joy1 || key == IK_Joy2) {
                                vm::Call(object, Name("NoticeGamepadActivity")).run();
                                if (handleCustomizeKeysNav(rootWin, cast<extension::UModalWindow>(child), key, repeat)) {
                                    vm::retBool(result, true);
                                    return;
                                }
                            }
                        }
                        break;
                    }
                }
            }

            NativeFlagGuard guard(state.rootVirtualKeyPressed);
            vm::Call call(object, state.rootVirtualKeyPressed);
            call.byte(effectiveKey).b(repeat);
            vm::retBool(result, runBoolCall(call));
        }

        void nativeMenuMainHandleDPad(UObject* object, Frame& stack, void* result)
        {
            int32_t dx = vm::get<int32_t>(stack);
            int32_t dy = vm::get<int32_t>(stack);
            vm::finish(stack);
            const ControllerMenuState& state = controllerMenuState();
            UObject* screen = readObjectProperty(object, Name("screen"));
            if (!object || !screen || !state.settingsClass || !screen->isA(state.settingsClass)) {
                if (!object || !state.handleDPad) {
                    vm::retBool(result, false);
                    return;
                }
                NativeFlagGuard guard(state.handleDPad);
                vm::Call call(object, state.handleDPad);
                call.i(dx).i(dy);
                vm::retBool(result, runBoolCall(call));
                return;
            }
            if (dy == 0) {
                vm::retBool(result, true);
                return;
            }
            std::array<extension::UWindow*, 11> buttons {};
            collectSettingsButtons(screen, buttons);

            int32_t* focusIdxPtr = reinterpret_cast<int32_t*>(object->propertyAddress(Name("focusIndex")));
            int32_t curIdx = focusIdxPtr ? *focusIdxPtr : -1;
            UObject* focusedObj = readObjectProperty(object, Name("focused"));
            auto* screenWin = cast<extension::UWindow>(screen);
            extension::URootWindow* rootWindow = screenWin ? screenWin->root() : nullptr;
            extension::UWindow* engineFocus = rootWindow ? rootWindow->focusWindow() : nullptr;
            for (int i = 0; i < 11; ++i) {
                extension::UWindow* btn = buttons[static_cast<size_t>(i)];
                if (btn && !btn->destroyed && (btn == engineFocus || (!engineFocus && btn == focusedObj))) {
                    curIdx = i;
                    break;
                }
            }

            constexpr int count = 11;
            const int step = (dy > 0) ? 1 : -1;
            int idx = (curIdx >= 0 && curIdx < count) ? curIdx : ((step > 0) ? -1 : 0);
            for (int attempt = 0; attempt < count; ++attempt) {
                idx = (idx + step + count) % count;
                extension::UWindow* candidate = buttons[static_cast<size_t>(idx)];
                if (candidate && !candidate->destroyed && candidate->visible() && candidate->sensitive()) {
                    if (focusIdxPtr) {
                        *focusIdxPtr = idx;
                    }
                    vm::Call(object, Name("SetFocus")).obj(candidate).run();
                    vm::retBool(result, true);
                    return;
                }
            }
            vm::retBool(result, true);
        }

        void hookNative(UFunction* fn, NativeFunction handler)
        {
            fn->functionFlags |= FUNC_Native;
            fn->native = handler;
        }

        void nativeEnumLoadSetting(UObject* object, Frame& stack, void*)
        {
            vm::finish(stack);
            const ControllerMenuState& state = controllerMenuState();
            if (object && state.langChoiceClass && object->isA(state.langChoiceClass) && state.enumSetValue) {
                int32_t value = iequals(config::language(), "ptb") ? 1 : 0;
                vm::Call(object, state.enumSetValue).i(value).run();
                return;
            }
            if (object && state.touchEditorChoiceClass && object->isA(state.touchEditorChoiceClass) && state.enumSetValue) {
                NativeFlagGuard guard(state.enumSetValue);
                vm::Call(object, state.enumSetValue).i(0).run();
                return;
            }
            if (object && state.enumLoadSetting) {
                NativeFlagGuard guard(state.enumLoadSetting);
                vm::Call(object, state.enumLoadSetting).run();
            }
        }

        void nativeEnumSaveSetting(UObject* object, Frame& stack, void*)
        {
            vm::finish(stack);
            ControllerMenuState& state = controllerMenuState();
            if (object && state.langChoiceClass && object->isA(state.langChoiceClass)) {
                uint8_t* enumAddr = object->propertyAddress(Name("currentValue"));
                int32_t val = enumAddr ? *reinterpret_cast<int32_t*>(enumAddr) : 0;
                const char* desired = (val == 1) ? "ptb" : "int";
                if (!iequals(desired, config::language()) && state.onLanguageChanged && !state.refreshingLanguage) {
                    state.refreshingLanguage = true;
                    state.onLanguageChanged(desired);
                    state.refreshingLanguage = false;
                }
                return;
            }
            if (object && state.touchEditorChoiceClass && object->isA(state.touchEditorChoiceClass)) {
                return;
            }
            if (object && state.enumSaveSetting) {
                NativeFlagGuard guard(state.enumSaveSetting);
                vm::Call(object, state.enumSaveSetting).run();
            }
        }

        void nativeEnumSetValue(UObject* object, Frame& stack, void*)
        {
            int32_t newValue = vm::get<int32_t>(stack);
            vm::finish(stack);
            ControllerMenuState& state = controllerMenuState();
            if (!object || !state.enumSetValue) {
                return;
            }
            {
                NativeFlagGuard guard(state.enumSetValue);
                vm::Call(object, state.enumSetValue).i(newValue).run();
            }
            if (state.langChoiceClass && object->isA(state.langChoiceClass) && !state.refreshingLanguage) {
                const char* desired = (newValue == 1) ? "ptb" : "int";
                if (!iequals(desired, config::language()) && state.onLanguageChanged) {
                    state.refreshingLanguage = true;
                    state.onLanguageChanged(desired);
                    state.refreshingLanguage = false;
                }
            } else if (state.touchEditorChoiceClass && object->isA(state.touchEditorChoiceClass) && state.onOpenTouchEditor) {
                if (newValue != 0) {
                    NativeFlagGuard guard(state.enumSetValue);
                    vm::Call(object, state.enumSetValue).i(0).run();
                    state.onOpenTouchEditor();
                }
            }
        }

        std::string localizeOrFallback(std::string_view section, std::string_view key, std::string_view package, const char* fallback)
        {
            std::string value = config::localize(section, key, package);
            if (value.rfind("<?int?", 0) == 0 || value.empty()) {
                return std::string(fallback);
            }
            return value;
        }

        void applyControllerSettingsLabels(UObject* menu, UClass* extraGameplay, UClass* extraGraphics)
        {
            ControllerMenuState& state = controllerMenuState();
            const bool pt = iequals(config::language(), "ptb");
            state.previousLabel = localizeOrFallback("MenuSettings", "ButtonNames[6]", "DeusEx", pt ? "Menu anterior" : "Previous Menu");

            const std::array<std::string, 10> labels {{
                localizeOrFallback("MenuSettings", "ButtonNames[0]", "DeusEx", pt ? "Teclado/Mouse" : "Keyboard/Mouse"),
                localizeOrFallback("MenuSettings", "ButtonNames[1]", "DeusEx", pt ? "Controles" : "Controls"),
                localizeOrFallback("MenuSettings", "ButtonNames[2]", "DeusEx", pt ? "Op\xe7\xf5" "es de jogo" : "Game Options"),
                pt ? utf8ToCp1252("Jogabilidade Extra") : "Extra Gameplay",
                localizeOrFallback("MenuSettings", "ButtonNames[3]", "DeusEx", pt ? "V\xed" "deo" : "Display"),
                pt ? utf8ToCp1252("Gráficos Extras") : "Extra Graphics",
                localizeOrFallback("MenuSettings", "ButtonNames[4]", "DeusEx", pt ? "Cores" : "Colors"),
                localizeOrFallback("MenuSettings", "ButtonNames[5]", "DeusEx", pt ? "Som" : "Sound"),
                localizeOrFallback("MenuScreenController", "Title", "DXController", pt ? "Controle" : "Controller"),
                pt ? "Salv. Autom\xe1" "tico" : localizeOrFallback("MenuScreenAutoSave", "Title", "DXController", "Autosave"),
            }};
            for (int i = 0; i < 10; ++i) {
                if (uint8_t* addr = menu->propertyAddress(Name("ButtonNames"), i)) {
                    *reinterpret_cast<std::string*>(addr) = labels[static_cast<size_t>(i)];
                }
            }
            (void)extraGameplay;
            (void)extraGraphics;
        }

        void copyStringProp(UObject* dst, UObject* src, Name prop, int index = 0)
        {
            uint8_t* dstAddr = dst ? dst->propertyAddress(prop, index) : nullptr;
            uint8_t* srcAddr = src ? src->propertyAddress(prop, index) : nullptr;
            if (dstAddr && srcAddr) {
                *reinterpret_cast<std::string*>(dstAddr) = *reinterpret_cast<const std::string*>(srcAddr);
            }
        }

        void refreshMenuWindowRecursive(
            extension::UWindow* win,
            UClass* menuUiWindowClass,
            UClass* menuUiMenuClass,
            UClass* menuUiScreenClass,
            UClass* menuUiChoiceClass,
            UClass* menuUiChoiceEnumClass)
        {
            if (!win || win->destroyed) {
                return;
            }
            const ControllerMenuState& state = controllerMenuState();
            UObject* def = win->cls ? win->cls->defaultObject : nullptr;

            if (def && menuUiWindowClass && win->isA(menuUiWindowClass)) {
                for (const char* propName : { "Title", "btnLabelOK", "btnLabelCancel", "btnLabelPrevious", "btnLabelResetToDefaults",
                         "MessageBoxTitle", "ExitMessage", "TrainingWarningMessage", "IntroWarningMessage", "AskToTrainTitle", "AskToTrainMessage" }) {
                    copyStringProp(win, def, Name(propName));
                }
                if (uint8_t* titleAddr = win->propertyAddress(Name("Title"))) {
                    vm::Call(win, Name("SetTitle")).s(*reinterpret_cast<const std::string*>(titleAddr)).run();
                }
                UObject* bar = readObjectProperty(win, Name("winButtonBar"));
                UProperty* screenAbProp = win->cls->findProperty(Name("actionButtons"));
                UProperty* barAbProp = bar && bar->cls ? bar->cls->findProperty(Name("actionButtons")) : nullptr;
                if (bar && screenAbProp && screenAbProp->structType && barAbProp && barAbProp->structType) {
                    UProperty* actProp = screenAbProp->structType->findProperty(Name("Action"));
                    UProperty* btnProp = barAbProp->structType->findProperty(Name("btn"));
                    uint8_t* countAddr = bar->propertyAddress(Name("buttonCount"));
                    int32_t barCount = countAddr ? *reinterpret_cast<int32_t*>(countAddr) : 0;
                    if (actProp && actProp->enumType && btnProp) {
                        int barIdx = 0;
                        for (int32_t i = 0; i < screenAbProp->arrayDim && barIdx < barCount; ++i) {
                            uint8_t* entry = win->propertyAddress(Name("actionButtons"), i);
                            if (!entry) {
                                continue;
                            }
                            uint8_t actVal = *(entry + actProp->offset);
                            Name actName = actVal < actProp->enumType->names.size() ? actProp->enumType->names[actVal] : Name();
                            if (actName.none() || actName == Name("AB_None")) {
                                continue;
                            }
                            const char* labelProp = nullptr;
                            if (actName == Name("AB_Cancel")) {
                                labelProp = "btnLabelCancel";
                            } else if (actName == Name("AB_OK")) {
                                labelProp = "btnLabelOK";
                            } else if (actName == Name("AB_Reset")) {
                                labelProp = "btnLabelResetToDefaults";
                            } else if (actName == Name("AB_Previous")) {
                                labelProp = "btnLabelPrevious";
                            }
                            uint8_t* barEntry = bar->propertyAddress(Name("actionButtons"), barIdx);
                            UObject* btn = barEntry ? *reinterpret_cast<UObject**>(barEntry + btnProp->offset) : nullptr;
                            if (labelProp && btn) {
                                if (uint8_t* labelAddr = win->propertyAddress(Name(labelProp))) {
                                    vm::Call(btn, Name("SetButtonText")).s(*reinterpret_cast<const std::string*>(labelAddr)).run();
                                }
                            }
                            barIdx++;
                        }
                        vm::Call(bar, Name("ConfigurationChanged")).run();
                    }
                }
            }

            if (def && menuUiMenuClass && win->isA(menuUiMenuClass)) {
                UProperty* namesProp = win->cls->findProperty(Name("ButtonNames"));
                if (namesProp) {
                    for (int32_t i = 0; i < namesProp->arrayDim; ++i) {
                        copyStringProp(win, def, Name("ButtonNames"), i);
                        UObject* btn = readObjectProperty(win, Name("winButtons"), i);
                        uint8_t* textAddr = win->propertyAddress(Name("ButtonNames"), i);
                        if (btn && textAddr) {
                            vm::Call(btn, Name("SetButtonText")).s(*reinterpret_cast<const std::string*>(textAddr)).run();
                        }
                    }
                }
                if (state.settingsClass && win->isA(state.settingsClass)) {
                    if (extension::UWindow* prev = findExtraPrevButton(win)) {
                        vm::Call(prev, Name("SetButtonText")).s(state.previousLabel).run();
                    }
                }
            }

            if (def && menuUiChoiceClass && win->isA(menuUiChoiceClass)) {
                copyStringProp(win, def, Name("actionText"));
                copyStringProp(win, def, Name("HelpText"));
                if (UObject* btnAction = readObjectProperty(win, Name("btnAction"))) {
                    if (uint8_t* actText = win->propertyAddress(Name("actionText"))) {
                        vm::Call(btnAction, Name("SetButtonText")).s(*reinterpret_cast<const std::string*>(actText)).run();
                    }
                }
                if (menuUiChoiceEnumClass && win->isA(menuUiChoiceEnumClass)) {
                    if (UProperty* enumProp = win->cls->findProperty(Name("enumText"))) {
                        for (int32_t i = 0; i < enumProp->arrayDim; ++i) {
                            copyStringProp(win, def, Name("enumText"), i);
                        }
                    }
                    vm::Call(win, Name("UpdateInfoButton")).run();
                }
            }

            for (extension::UWindow* child = win->firstChild(); child; child = child->nextSibling()) {
                refreshMenuWindowRecursive(child, menuUiWindowClass, menuUiMenuClass, menuUiScreenClass, menuUiChoiceClass, menuUiChoiceEnumClass);
            }

            if (menuUiScreenClass && win->isA(menuUiScreenClass)) {
                if (UObject* current = readObjectProperty(win, Name("currentChoice"))) {
                    if (uint8_t* helpAddr = current->propertyAddress(Name("HelpText"))) {
                        vm::Call(win, Name("ShowHelp")).s(*reinterpret_cast<const std::string*>(helpAddr)).run();
                    }
                }
            }
        }
    }

    std::string utf8ToCp1252(std::string_view utf8)
    {
        std::string out;
        out.reserve(utf8.size());
        for (size_t i = 0; i < utf8.size();) {
            uint8_t b0 = static_cast<uint8_t>(utf8[i]);
            if (b0 < 0x80) {
                out.push_back(static_cast<char>(b0));
                i++;
                continue;
            }
            uint32_t cp = '?';
            size_t adv = 1;
            if ((b0 & 0xe0) == 0xc0 && i + 1 < utf8.size()) {
                cp = (static_cast<uint32_t>(b0 & 0x1f) << 6) | static_cast<uint32_t>(static_cast<uint8_t>(utf8[i + 1]) & 0x3f);
                adv = 2;
            } else if ((b0 & 0xf0) == 0xe0 && i + 2 < utf8.size()) {
                cp = (static_cast<uint32_t>(b0 & 0x0f) << 12) | (static_cast<uint32_t>(static_cast<uint8_t>(utf8[i + 1]) & 0x3f) << 6) | static_cast<uint32_t>(static_cast<uint8_t>(utf8[i + 2]) & 0x3f);
                adv = 3;
            }
            i += adv;
            out.push_back(cp < 256 ? static_cast<char>(cp) : '?');
        }
        return out;
    }

    void configureLanguageChoice(UClass* choiceClass, const std::function<void(std::string_view newLanguage)>& onChanged)
    {
        ControllerMenuState& state = controllerMenuState();
        state.langChoiceClass = choiceClass;
        state.onLanguageChanged = onChanged;
        if (!state.enumLoadSetting) {
            UClass* enumChoiceClass = objects::findClass("DeusEx.MenuUIChoiceEnum");
            if (!enumChoiceClass) {
                return;
            }
            state.enumLoadSetting = enumChoiceClass->findFunction(Name("LoadSetting"));
            state.enumSaveSetting = enumChoiceClass->findFunction(Name("SaveSetting"));
            state.enumSetValue = enumChoiceClass->findFunction(Name("SetValue"));
            if (state.enumLoadSetting) {
                hookNative(state.enumLoadSetting, &nativeEnumLoadSetting);
            }
            if (state.enumSaveSetting) {
                hookNative(state.enumSaveSetting, &nativeEnumSaveSetting);
            }
            if (state.enumSetValue) {
                hookNative(state.enumSetValue, &nativeEnumSetValue);
            }
        }
    }

    void configureTouchEditorChoice(UClass* choiceClass, const std::function<void()>& onOpen)
    {
        ControllerMenuState& state = controllerMenuState();
        state.touchEditorChoiceClass = choiceClass;
        state.onOpenTouchEditor = onOpen;
    }

    void refreshControllerSettingsMenu(UObject* menu, UClass* extraGameplay, UClass* extraGraphics)
    {
        if (menu) {
            applyControllerSettingsLabels(menu, extraGameplay, extraGraphics);
        }
    }

    void refreshOpenMenus()
    {
        auto* root = cast<extension::UWindow>(extension::rootWindow());
        if (!root) {
            return;
        }
        refreshMenuWindowRecursive(
            root,
            objects::findClass("DeusEx.MenuUIWindow"),
            objects::findClass("DeusEx.MenuUIMenuWindow"),
            objects::findClass("DeusEx.MenuUIScreenWindow"),
            objects::findClass("DeusEx.MenuUIChoice"),
            objects::findClass("DeusEx.MenuUIChoiceEnum"));
    }

    void registerUiTextTranslator()
    {
        static const std::unordered_map<std::string, std::string> ptMap = [] {
            struct Entry {
                const char* en;
                const char* pt;
            };
            constexpr Entry entries[] = {
                { "Select", "Selecionar" },
                { "Back", "Voltar" },
                { "Close", "Fechar" },
                { "Cancel", "Cancelar" },
                { "Skip", "Pular" },
                { "Save", "Salvar" },
                { "Load", "Carregar" },
                { "Delete", "Excluir" },
                { "Save theme", "Salvar tema" },
                { "Apply theme", "Aplicar tema" },
                { "Upgrade", "Melhorar" },
                { "Upgrade / Select", "Melhorar / Selecionar" },
                { "Downgrade", "Rebaixar" },
                { "Start Game", "Iniciar Jogo" },
                { "Install", "Instalar" },
                { "Toggle", "Alternar" },
                { "Use Cell", "Usar Célula" },
                { "Scroll", "Rolar" },
                { "Scroll info", "Rolar info" },
                { "Scroll text", "Rolar texto" },
                { "Prev tab", "Aba ant." },
                { "Next tab", "Próx. aba" },
                { "Prev pane", "Painel ant." },
                { "Next pane", "Próx. painel" },
                { "Special", "Especial" },
                { "Pan", "Mover câmera" },
                { "Zoom out", "Afastar" },
                { "Zoom in", "Aproximar" },
                { "Logout", "Sair" },
                { "Browse", "Navegar" },
                { "Switch area", "Trocar área" },
                { "Heal", "Curar" },
                { "Heal All", "Curar tudo" },
                { "Recharge", "Recarregar" },
                { "Press", "Pressionar" },
                { "Backspace", "Apagar" },
                { "Apply mod", "Aplicar mod" },
                { "Place", "Colocar" },
                { "Assign", "Atribuir" },
                { "Use", "Usar" },
                { "Equip", "Equipar" },
                { "Unequip", "Desequipar" },
                { "Assign slot", "Atribuir atalho" },
                { "Move", "Mover" },
                { "Change ammo", "Trocar munição" },
                { "Drop/Scroll info", "Largar/Rolar info" },
                { "SPACE", "ESPAÇO" },
                { "BACKSPACE", "APAGAR" },
                { "ENTER ACCOUNT", "DIGITE A CONTA" },
                { "ENTER PIN", "DIGITE O PIN" },
                { "ENTER USERNAME", "DIGITE O USUÁRIO" },
                { "ENTER PASSWORD", "DIGITE A SENHA" },
                { "ENTER NAME", "DIGITE O NOME" },
                { "WITHDRAW AMOUNT", "VALOR DO SAQUE" },
                { "(empty)", "(vazio)" },
                { "(unassigned)", "(não atribuído)" },
            };
            std::unordered_map<std::string, std::string> m;
            for (const Entry& e : entries) {
                m.emplace(e.en, utf8ToCp1252(e.pt));
            }
            return m;
        }();

        static const std::string ptStickLeft = utf8ToCp1252("Analógico esq.: ");
        static const std::string ptStickRight = utf8ToCp1252("Analógico dir.: ");
        static const std::string ptPowerPrefix = utf8ToCp1252("Potência (");
        static const std::string ptExpoPrefix = "Exponencial (";
        static const std::string ptSigmoidPrefix = "Sigmoide (";
        static const std::string ptCurrently = "atual: ";
        static const std::string ptSlot = " -> atalho ";
        static const std::string ptActive = "/min  ATIVO";
        static const std::string ptOff = "/min  DESL.";

        extension::setTextTranslator([](std::string_view text) -> std::string {
            if (!iequals(config::language(), "ptb")) {
                return std::string(text);
            }
            if (auto it = ptMap.find(std::string(text)); it != ptMap.end()) {
                return it->second;
            }
            auto translateCurveSuffix = [](std::string_view s) -> std::string {
                if (s.rfind("Power (", 0) == 0) {
                    return ptPowerPrefix + std::string(s.substr(7));
                }
                if (s.rfind("Expo (", 0) == 0) {
                    return ptExpoPrefix + std::string(s.substr(6));
                }
                if (s.rfind("Sigmoid (", 0) == 0) {
                    return ptSigmoidPrefix + std::string(s.substr(9));
                }
                return std::string(s);
            };
            if (text.rfind("Left stick: ", 0) == 0) {
                return ptStickLeft + translateCurveSuffix(text.substr(12));
            }
            if (text.rfind("Right stick: ", 0) == 0) {
                return ptStickRight + translateCurveSuffix(text.substr(13));
            }
            if (text.rfind("currently: ", 0) == 0) {
                return ptCurrently + std::string(text.substr(11));
            }
            if (size_t pos = text.find(" -> slot "); pos != std::string_view::npos) {
                return std::string(text.substr(0, pos)) + ptSlot + std::string(text.substr(pos + 9));
            }
            if (text.size() > 12 && text.substr(text.size() - 12) == "/min  ACTIVE") {
                return std::string(text.substr(0, text.size() - 12)) + ptActive;
            }
            if (text.size() > 9 && text.substr(text.size() - 9) == "/min  OFF") {
                return std::string(text.substr(0, text.size() - 9)) + ptOff;
            }
            return std::string(text);
        });
    }

    void configureControllerSettingsMenu(UObject* menu, UClass* extraGameplay, UClass* extraGraphics)
    {
        if (!menu || !menu->cls) {
            return;
        }
        UProperty* defaultsProp = menu->cls->findProperty(Name("buttonDefaults"));
        UProperty* namesProp = menu->cls->findProperty(Name("ButtonNames"));
        if (!defaultsProp || !defaultsProp->structType || !namesProp) {
            return;
        }
        UStruct* buttonStruct = defaultsProp->structType;
        UProperty* yProp = buttonStruct->findProperty(Name("y"));
        UProperty* actionProp = buttonStruct->findProperty(Name("action"));
        UProperty* invokeProp = buttonStruct->findProperty(Name("invoke"));
        UProperty* keyProp = buttonStruct->findProperty(Name("key"));
        if (!yProp || !actionProp || !invokeProp || !keyProp) {
            return;
        }

        UClass* customizeKeys = objects::findClass("DeusEx.MenuScreenCustomizeKeys");
        UClass* controls = objects::findClass("DeusEx.MenuScreenControls");
        UClass* gameOptions = objects::findClass("DeusEx.MenuScreenOptions");
        UClass* display = objects::findClass("DeusEx.MenuScreenDisplay");
        UClass* colors = objects::findClass("DeusEx.MenuScreenAdjustColors");
        UClass* sound = objects::findClass("DeusEx.MenuScreenSound");
        UClass* controller = objects::findClass("DXController.MenuScreenController");
        UClass* autosave = objects::findClass("DXController.MenuScreenAutoSave");

        ControllerMenuState& state = controllerMenuState();
        state.settingsClass = menu->cls;
        state.menuButtonClass = objects::findClass("DeusEx.MenuUIMenuButtonWindow");
        state.controllerRootClass = objects::findClass("DXController.ControllerRootWindow");
        state.infoButtonClass = objects::findClass("DeusEx.MenuUIInfoButtonWindow");
        state.choiceClass = objects::findClass("DeusEx.MenuUIChoice");
        state.customizeKeysClass = customizeKeys;
        state.actionButtonClass = objects::findClass("DeusEx.MenuUIActionButtonWindow");
        state.screenClass = objects::findClass("DeusEx.MenuUIScreenWindow");
        state.extraGameplayClass = extraGameplay;
        state.extraGraphicsClass = extraGraphics;
        UClass* menuUiMenuClass = menu->cls->superClass();
        UClass* menuUiClass = menuUiMenuClass ? menuUiMenuClass->superClass() : nullptr;
        UClass* navClass = objects::findClass("DXController.MenuMainNavController");
        UClass* optionsNavClass = objects::findClass("DXController.OptionsNavController");
        state.optionsNavClass = optionsNavClass;
        objects::flushLoads();

        const std::array<UClass*, 10> invokes {{
            customizeKeys,
            controls,
            gameOptions,
            extraGameplay,
            display,
            extraGraphics,
            colors,
            sound,
            controller,
            autosave,
        }};

        for (int i = 0; i < 10; ++i) {
            uint8_t* entry = menu->propertyAddress(Name("buttonDefaults"), i);
            *reinterpret_cast<int32_t*>(entry + yProp->offset) = kMenuButtonStartY + i * kMenuButtonStride;
            *(entry + actionProp->offset) = kMenuActionScreen;
            *reinterpret_cast<UObject**>(entry + invokeProp->offset) = invokes[static_cast<size_t>(i)];
            *reinterpret_cast<std::string*>(entry + keyProp->offset) = "";
        }
        applyControllerSettingsLabels(menu, extraGameplay, extraGraphics);

        // MenuOptionsBackground starts with 6 upper slots (0..5); insert 4 slots (144 px)
        // before Y=193 so the divider and Previous Menu slot shift down cleanly to Y=410.
        spliceMenuBackground(menu, 193, 49);
        if (uint8_t* heightAddr = menu->propertyAddress(Name("ClientHeight"))) {
            *reinterpret_cast<int32_t*>(heightAddr) = kControllerMenuClientHeight;
        }

        UFunction* settingsInit = state.settingsClass->findFunction(Name("InitWindow"));
        state.superInitWindow = menuUiMenuClass ? menuUiMenuClass->findFunction(Name("InitWindow")) : nullptr;
        state.superButtonActivated = menuUiClass ? menuUiClass->findFunction(Name("ButtonActivated")) : nullptr;
        state.menuButtonActivated = menuUiMenuClass ? menuUiMenuClass->findFunction(Name("ButtonActivated")) : nullptr;
        state.handleDPad = navClass ? navClass->findFunction(Name("HandleDPad")) : nullptr;
        state.optionsHandleDPad = optionsNavClass ? optionsNavClass->findFunction(Name("HandleDPad")) : nullptr;
        state.optionsIsEnabled = optionsNavClass ? optionsNavClass->findFunction(Name("IsEnabled")) : nullptr;
        state.optionsEnterActionBar = optionsNavClass ? optionsNavClass->findFunction(Name("EnterActionBar")) : nullptr;
        state.optionsHandleActivate = optionsNavClass ? optionsNavClass->findFunction(Name("HandleActivate")) : nullptr;
        state.rootVirtualKeyPressed = state.controllerRootClass ? state.controllerRootClass->findFunction(Name("VirtualKeyPressed")) : nullptr;
        state.registerNavControllers = state.controllerRootClass ? state.controllerRootClass->findFunction(Name("RegisterNavControllers")) : nullptr;
        state.screenVirtualKeyPressed = state.screenClass ? state.screenClass->findFunction(Name("VirtualKeyPressed")) : nullptr;

        if (settingsInit && settingsInit->outer == state.settingsClass && state.superInitWindow) {
            hookNative(settingsInit, &nativeMenuSettingsInitWindow);
        }
        if (state.superButtonActivated) {
            hookNative(state.superButtonActivated, &nativeMenuUIWindowButtonActivated);
        }
        if (state.menuButtonActivated) {
            hookNative(state.menuButtonActivated, &nativeMenuUIMenuButtonActivated);
        }
        if (state.handleDPad) {
            hookNative(state.handleDPad, &nativeMenuMainHandleDPad);
        }
        if (state.optionsIsEnabled) {
            hookNative(state.optionsIsEnabled, &nativeOptionsIsEnabled);
        }
        if (state.optionsHandleDPad) {
            hookNative(state.optionsHandleDPad, &nativeOptionsHandleDPad);
        }
        if (state.optionsEnterActionBar) {
            hookNative(state.optionsEnterActionBar, &nativeOptionsEnterActionBar);
        }
        if (state.optionsHandleActivate) {
            hookNative(state.optionsHandleActivate, &nativeOptionsHandleActivate);
        }
        if (state.screenVirtualKeyPressed) {
            hookNative(state.screenVirtualKeyPressed, &nativeMenuUIScreenVirtualKeyPressed);
        }
        if (state.rootVirtualKeyPressed) {
            hookNative(state.rootVirtualKeyPressed, &nativeControllerRootVirtualKeyPressed);
        }
        if (state.registerNavControllers) {
            hookNative(state.registerNavControllers, &nativeControllerRootRegisterNavControllers);
            registerExtraScreenNav(cast<UObject>(extension::rootWindow()));
        }
    }
}
