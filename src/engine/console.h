#pragma once

#include "core/object.h"

#include <cstdint>
#include <functional>
#include <string>

namespace dx {
    namespace console {
        std::string execute(UObject* context, const std::string& command);
        void loadBindings();
        bool key(uint8_t key, uint8_t action, float delta);
        void readInput(UObject* player, float deltaSeconds);
        void resetInput(UObject* player);
        std::string binding(uint8_t key);
        std::string keyName(uint8_t key);
        uint8_t keyFromName(const std::string& name);
        void applyAudioSettings();

        enum class StickCurveType : uint8_t {
            Linear = 0,
            Power = 1,
            Expo = 2,
            Sigmoid = 3,
        };

        struct StickCurve {
            StickCurveType type = StickCurveType::Linear;
            float power = 2.0f;
            float expo = 0.6f;
            float sigSteepness = 6.0f;
            float sigMidpoint = 0.6f;
            float sigStrength = 0.6f;
        };

        struct GamepadSettings {
            int leftDeadzone = 3500;
            int rightDeadzone = 3500;
            int triggerThreshold = 30;
            StickCurve leftCurve { StickCurveType::Power, 2.0f, 0.60f, 6.0f, 0.60f, 0.60f };
            StickCurve rightCurve { StickCurveType::Sigmoid, 2.0f, 0.60f, 8.0f, 0.70f, 0.90f };
            float rightScale = 0.4f;
            bool invertLookY = false;
        };

        void reloadGamepadSettings();
        const GamepadSettings& gamepadSettings();
        void shapeStick(int rawX, int rawY, int deadzone, const StickCurve& curve, float scale, float& outX, float& outY);
        float shapeTrigger(int raw, int threshold255);
        void setGamepadRawMags(float leftMag, float rightMag);
        void setGamepadInfo(const std::string& info);
        void releaseAllKeys();

        extern std::function<void(const std::string&, int32_t)> openMapHook;
        extern std::function<void()> quitHook;
        extern std::function<std::string()> currentResHook;
        extern std::function<std::string()> resolutionsHook;
        extern std::function<void(int, int)> setResHook;
        extern std::function<void()> toggleFullscreenHook;
        extern std::function<void()> screenshotHook;
    }
}
