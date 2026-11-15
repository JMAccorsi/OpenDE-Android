#pragma once

#include <SDL.h>

#include <array>
#include <cstdint>
#include <functional>
#include <string>

namespace dx {
    class Canvas2D;
}

namespace game {

    enum class TouchControlId : uint8_t {
        MoveStick = 0,
        Fire,
        Use,
        Jump,
        Crouch,
        Reload,
        Scope,
        LeanLeft,
        LeanRight,
        Inventory,
        Goals,
        Menu,
        BeltPrev,
        BeltNext,
        SwitchAmmo,
        Laser,
        AugLight,
        QuickSave,
        Count,
    };

    inline constexpr size_t kTouchControlCount = static_cast<size_t>(TouchControlId::Count);

    struct TouchControlLayout {
        bool enabled = true;
        float cx = 0.5f;
        float cy = 0.5f;
        float scale = 1.0f;
    };

    struct TouchOverlaySettings {
        bool enabled = true;
        float opacity = 0.55f;
        float scale = 1.0f;
        float lookSensitivity = 1.0f;
    };

    struct TouchInputCallbacks {
        std::function<void(uint8_t key, bool pressed)> onKey;
        std::function<void(uint8_t key, float value)> onAxis;
        std::function<void(float dx, float dy)> onLookDelta;
        std::function<void()> onQuickRightClick;
    };

    class TouchOverlayManager {
    public:
        TouchOverlayManager();

        void load(const std::string& iniSection);
        void saveLayout() const;
        void resetToDefault();
        void applyPreset(int presetIndex);

        void setSettings(const TouchOverlaySettings& newSettings);
        const TouchOverlaySettings& settings() const { return settings_; }

        bool isVisible() const { return visible_; }
        bool isEditorOpen() const { return editorOpen_; }
        void openEditor();
        void closeEditor(bool saveChanges);

        // Immediately hides overlays on any physical controller, keyboard, or mouse input.
        void onPhysicalInput(const TouchInputCallbacks& cb);
        void releaseGameplayInputs(const TouchInputCallbacks& cb);
        void emitAxes(const TouchInputCallbacks& cb);

        // Handles touch events either in the layout editor or in active gameplay.
        // Returns true if the event was consumed by the overlay system.
        bool handleTouchEvent(const SDL_Event& event, int drawWidth, int drawHeight, const TouchInputCallbacks& cb);
        bool handleEditorPhysicalKey(SDL_Keycode sym, bool pressed);
        bool handleEditorControllerButton(SDL_GameControllerButton button, bool pressed);

        void render(dx::Canvas2D& canvas, bool ptBr, bool gameplayBlocked);

    private:
        struct ButtonState {
            bool active = false;
            bool fingerDown = false;
            SDL_FingerID finger = 0;
            bool dragAimMoved = false;
            float prevX = 0.0f;
            float prevY = 0.0f;
        };

        void buildDefaultLayouts();
        float controlRadiusPx(TouchControlId id, int drawHeight) const;
        bool isActionToggle(TouchControlId id) const;
        int hitTestControl(float xNorm, float yNorm, int drawWidth, int drawHeight, bool includeDisabled) const;
        void releaseAllControls(const TouchInputCallbacks& cb);
        void releaseTouchStickOnly(const TouchInputCallbacks& cb);
        void setButtonPressed(TouchControlId id, bool pressed, const TouchInputCallbacks& cb);
        void renderGameplayOverlays(dx::Canvas2D& canvas, bool ptBr);
        void renderEditor(dx::Canvas2D& canvas, bool ptBr);
        void getToolbarRect(int width, int height, float& outX, float& outY, float& outW, float& outH) const;
        bool handleEditorTouch(const SDL_Event& event, int drawWidth, int drawHeight);

        TouchOverlaySettings settings_;
        std::array<TouchControlLayout, kTouchControlCount> layouts_ {};
        std::array<TouchControlLayout, kTouchControlCount> editorBackup_ {};
        std::array<ButtonState, kTouchControlCount> buttons_ {};

        bool visible_ = false;
        bool editorOpen_ = false;
        bool toolbarAtBottom_ = false;
        int selectedControl_ = 0;
        int activePreset_ = 0;

        bool editorDragging_ = false;
        SDL_FingerID editorFinger_ = 0;
        float editorDragOffsetX_ = 0.0f;
        float editorDragOffsetY_ = 0.0f;
        int editorPendingTool_ = -1;

        bool hasStickFinger_ = false;
        SDL_FingerID stickFinger_ = 0;
        float stickOriginX_ = 0.0f;
        float stickOriginY_ = 0.0f;
        float stickCurX_ = 0.0f;
        float stickCurY_ = 0.0f;
        float prevEmitStickX_ = 0.0f;
        float prevEmitStickY_ = 0.0f;

        bool hasLookFinger_ = false;
        bool lookCanQuickClick_ = false;
        SDL_FingerID lookFinger_ = 0;
        float lookStartX_ = 0.0f;
        float lookStartY_ = 0.0f;
        float lookMaxDistSq_ = 0.0f;
        uint64_t lookDownTicks_ = 0;

        bool crouchToggled_ = false;
    };

} // namespace game
