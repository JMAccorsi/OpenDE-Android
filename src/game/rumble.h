#pragma once

#include <SDL3/SDL.h>

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace dx {
    class UObject;
}

namespace game {

    enum class RumbleStrength : int {
        Off = 0,
        Low = 1,
        Medium = 2,
        High = 3,
    };

    enum class RumbleTarget : int {
        Auto = 0,
        GamepadOnly = 1,
        DeviceOnly = 2,
        Both = 3,
    };

    struct RumbleSettings {
        RumbleStrength strength = RumbleStrength::Medium;
        RumbleTarget target = RumbleTarget::Auto;
        bool touchButtonHaptics = false;
    };

    struct RumbleDebugInfo {
        bool controllerConnected = false;
        bool controllerHasRumble = false;
        bool controllerHasTriggerRumble = false;
        int androidCaps = 0;
        std::string activeOutput = "None";
        int activeChannels = 0;
        float currentLow = 0.0f;
        float currentHigh = 0.0f;
        uint64_t eventsTriggered = 0;
    };

    class RumbleEngine {
    public:
        RumbleEngine() = default;
        ~RumbleEngine();

        RumbleEngine(const RumbleEngine&) = delete;
        RumbleEngine& operator=(const RumbleEngine&) = delete;

        void initialize();
        void shutdown();

        void setSettings(const RumbleSettings& settings);
        const RumbleSettings& settings() const { return settings_; }

        void setController(SDL_Gamepad* controller);
        void resetPlayerTracking();
        void stopAll();

        // Observes live DeusExPlayer / Weapon / Augmentation state each frame.
        void samplePlayerState(dx::UObject* player, float dt, bool inGameplay);

        // Advances envelopes and drives SDL3 + Android hardware backends.
        void update(float dt, bool suppressOutput, bool lastInputWasGamepad);

        // ---- Gameplay Event Triggers ----
        void playWeaponFire(std::string_view className, float recoilStrength, int hitDamage, float shotTime, bool silenced, bool handToHand);
        void playMeleeSwing();
        void playMeleeImpact(bool energyBlade, bool shockProd);
        void playDryFire();
        void playReloadStart();
        void playReloadFinish();
        void playGrenadeThrow();
        void playGrenadePlant();
        void playGepLockOn();

        void playExplosion(float distance, float radius);
        void playDamage(float amount, std::string_view damageType);
        void playDeath();

        void playLanding(float impactSpeed, bool speedAugActive);
        void playWaterSplash();

        void playLockpickTick();
        void playMultitoolPulse();
        void playToolComplete();
        void playAugToggle(bool active);
        void playEnergyDepleted();
        void playKeypadKey();
        void playKeypadResult(bool granted);
        void playFrob();
        void playSkillPoints();
        void playTouchButton();

        // Audio-event observer called from engine::clientHearSound hook.
        void onHearSound(std::string_view soundName, float distance, float radius, bool fromPlayerOrOwned);

        // Diagnostic test trigger (e.g. "pistol", "shotgun", "gep", "explosion", "damage", "emp", "lockpick", "heartbeat").
        bool triggerTestPreset(std::string_view preset);

        RumbleDebugInfo debugInfo() const;

    private:
        enum class Curve : uint8_t {
            Linear = 0,
            ExpDecay = 1,
            RampUp = 2,
            Pulse = 3,
        };

        enum class ChannelTag : uint8_t {
            None = 0,
            WeaponFire = 1,
            Flamethrower = 2,
            Explosion = 3,
            Damage = 4,
            Heartbeat = 5,
            Lockpick = 6,
            Multitool = 7,
            TouchUI = 8,
            Burn = 9,
        };

        struct Channel {
            bool active = false;
            Curve curve = Curve::ExpDecay;
            ChannelTag tag = ChannelTag::None;
            uint8_t priority = 1;
            int primitiveHint = 0; // 0=none, 1=tick, 2=click, 3=thud
            float delay = 0.0f;
            float elapsed = 0.0f;
            float duration = 0.0f;
            float lowStart = 0.0f;
            float lowEnd = 0.0f;
            float highStart = 0.0f;
            float highEnd = 0.0f;
            float trigL = 0.0f;
            float trigR = 0.0f;
        };

        void pushChannel(const Channel& ch, bool replaceSameTag = false);
        float strengthScale() const;
        void applyHardwareOutput(float low, float high, float trigL, float trigR, int durationMs, int primitiveHint, bool forceSend, bool lastInputWasGamepad);
        void stopHardware();

        RumbleSettings settings_;
        bool initialized_ = false;

        SDL_Gamepad* controller_ = nullptr;
        bool controllerHasRumble_ = false;
        bool controllerHasTriggerRumble_ = false;
        int androidCaps_ = 0;

        static constexpr size_t kMaxChannels = 16;
        std::array<Channel, kMaxChannels> channels_ {};

        float continuousShakeLow_ = 0.0f;
        float continuousShakeHigh_ = 0.0f;
        float continuousFlameLow_ = 0.0f;
        float continuousFlameHigh_ = 0.0f;

        // Hardware throttle state
        float hwTimer_ = 0.0f;
        float lastSentLow_ = 0.0f;
        float lastSentHigh_ = 0.0f;
        float lastSentTrigL_ = 0.0f;
        float lastSentTrigR_ = 0.0f;
        bool hwActive_ = false;
        bool pendingImpulse_ = false;
        int pendingPrimHint_ = 0;
        int pendingImpulseMs_ = 0;
        std::string activeOutput_ = "None";
        uint64_t eventsTriggered_ = 0;

        // Player state tracking for diff-based detection (mod-safe)
        dx::UObject* trackedPlayer_ = nullptr;
        dx::UObject* prevWeapon_ = nullptr;
        int32_t prevClipCount_ = -1;
        bool prevWeaponFiring_ = false;
        int32_t prevLockMode_ = 0;
        bool prevHeadInWater_ = false;
        bool prevFootInWater_ = false;
        bool prevDead_ = false;
        int32_t prevHealth_ = -1;
        int32_t prevHealthHead_ = -1;
        int32_t prevHealthTorso_ = -1;
        float prevEnergy_ = -1.0f;
        float prevEnergyDrain_ = 0.0f;
        int32_t prevPoisonCounter_ = 0;
        uint32_t prevActiveAugMask_ = 0;
        bool hasPrevAugMask_ = false;
        float heartbeatTimer_ = 0.0f;
        float empPulseCooldown_ = 0.0f;
        float burnPulseCooldown_ = 0.0f;
        float soundExplosionCooldown_ = 0.0f;
        float soundToolCooldown_ = 0.0f;
        float soundFrobCooldown_ = 0.0f;
        float weaponFireCooldown_ = 0.0f;
    };

    // Unified helper for DXLauncher 3D controller tester (falls back to Android device vibrator on handhelds).
    void rumbleLauncherTest(SDL_Gamepad* pad, float left, float right, int durationMs);

} // namespace game
