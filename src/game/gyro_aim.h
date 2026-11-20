#pragma once

#include <SDL.h>

#include <cstdint>
#include <string>

#ifdef __ANDROID__
struct ASensor;
struct ASensorEventQueue;
struct ASensorManager;
#endif

namespace game {

    enum class GyroAimMode : int {
        Off = 0,
        AlwaysOn = 1,
        WhileAiming = 2,
    };

    enum class GyroAxisMode : int {
        AutoGravity = 0,
        Yaw = 1,
        Roll = 2,
    };

    struct GyroSettings {
        GyroAimMode mode = GyroAimMode::Off;
        float sensitivity = 1.0f;
        GyroAxisMode axis = GyroAxisMode::AutoGravity;
        bool invertY = false;
    };

    struct GyroDelta {
        float yawRad = 0.0f;
        float pitchRad = 0.0f;
    };

    struct GyroDebugInfo {
        std::string backend = "None";
        std::string gyroSensorName;
        std::string gravitySensorName;
        int displayRotation = 1;
        bool enabled = false;
        bool usingGravityFallbackAccel = false;
        bool hasGravitySample = false;
        uint64_t gyroSamples = 0;
        uint64_t gravitySamples = 0;
        float lastScreenGyro[3] {};
        float lastBias[3] {};
        float upVec[3] { 0.0f, 1.0f, 0.0f };
        float lastOutputYawRad = 0.0f;
        float lastOutputPitchRad = 0.0f;
    };

    class GyroAim {
    public:
        GyroAim() = default;
        ~GyroAim();

        GyroAim(const GyroAim&) = delete;
        GyroAim& operator=(const GyroAim&) = delete;

        void initialize();
        void shutdown();

        void setSettings(const GyroSettings& settings);
        const GyroSettings& settings() const { return settings_; }

        void setSuspended(bool suspended);
        void setController(SDL_GameController* controller);
        void refreshDisplayRotation();
        void handleControllerSensorEvent(const SDL_ControllerSensorEvent& ev);

        GyroDelta poll(bool allowOutput);
        GyroDebugInfo debugInfo() const;

    private:
        void updateActiveState();
        void resetMotionState();
        void processGravitySample(float sx, float sy, float sz, float alpha);
        void processGyroSample(float nx, float ny, float nz, int rotation, float dt, bool allowOutput, float& accumYaw, float& accumPitch);

        GyroSettings settings_;
        bool initialized_ = false;
        bool suspended_ = false;

        SDL_GameController* controller_ = nullptr;
        bool controllerHasGyro_ = false;
        bool controllerHasAccel_ = false;
        bool controllerSensorsEnabled_ = false;
        uint64_t prevCtrlTsUs_ = 0;
        uint64_t prevCtrlPerfCounter_ = 0;
        float pendingCtrlYawRad_ = 0.0f;
        float pendingCtrlPitchRad_ = 0.0f;

#ifdef __ANDROID__
        ASensorManager* sensorManager_ = nullptr;
        const ASensor* androidGyro_ = nullptr;
        const ASensor* androidGravity_ = nullptr;
        bool androidGravityIsAccel_ = false;
        ASensorEventQueue* androidQueue_ = nullptr;
        bool androidSensorsEnabled_ = false;
        int64_t prevAndroidGyroTsNs_ = 0;
#endif

        int displayRotation_ = 1;
        bool hasGravitySample_ = false;
        float up_[3] { 0.0f, 1.0f, 0.0f };

        bool hasPrevRawGyro_ = false;
        float prevRawGyro_[3] {};
        float bias_[3] {};
        float calmSeconds_ = 0.0f;

        uint64_t gyroSamples_ = 0;
        uint64_t gravitySamples_ = 0;
        float lastScreenGyro_[3] {};
        GyroDelta lastOutput_;
    };

} // namespace game
