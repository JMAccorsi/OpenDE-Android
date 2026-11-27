#include "game/rumble.h"

#include "core/name.h"
#include "core/object.h"
#include "core/objects.h"
#include "core/vm.h"
#include "engine/actor.h"
#include "engine/props_deusex.h"
#include "engine/props_engine.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

#ifdef __ANDROID__
#include <jni.h>
#endif

namespace game {

    namespace {
        constexpr int kCapDeviceVibrator = 1;
        constexpr int kCapAmplitudeControl = 2;
        constexpr int kCapPrimitives = 4;
        constexpr int kCapInputDeviceVibrator = 8;

        constexpr int kPrimNone = 0;
        constexpr int kPrimTick = 1;
        constexpr int kPrimClick = 2;
        constexpr int kPrimThud = 3;

        constexpr float kHwUpdateInterval = 1.0f / 30.0f; // 30 Hz max IPC rate (zero frame jank)

        std::string toLowerAscii(std::string_view s)
        {
            std::string out;
            out.reserve(s.size());
            for (char c : s) {
                out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
            }
            return out;
        }

        float clamp01(float v)
        {
            return std::clamp(v, 0.0f, 1.0f);
        }

        Uint16 toUint16(float v)
        {
            float c = clamp01(v);
            return static_cast<Uint16>(std::lround(c * 65535.0f));
        }

#ifdef __ANDROID__
        struct AndroidRumbleJni {
            jclass activityClass = nullptr;
            jmethodID getCaps = nullptr;
            jmethodID rumbleDevice = nullptr;
            jmethodID rumbleGamepadLegacy = nullptr;
            jmethodID stopAll = nullptr;
            bool resolved = false;
        };

        AndroidRumbleJni& jniCache()
        {
            static AndroidRumbleJni cache;
            return cache;
        }

        bool ensureJni(JNIEnv*& env)
        {
            env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
            if (!env) {
                return false;
            }
            AndroidRumbleJni& c = jniCache();
            if (c.resolved) {
                return c.activityClass != nullptr;
            }
            c.resolved = true;
            jobject activity = static_cast<jobject>(SDL_GetAndroidActivity());
            if (!activity) {
                return false;
            }
            jclass localCls = env->GetObjectClass(activity);
            if (localCls) {
                c.activityClass = static_cast<jclass>(env->NewGlobalRef(localCls));
                env->DeleteLocalRef(localCls);
            }
            env->DeleteLocalRef(activity);
            if (!c.activityClass) {
                return false;
            }
            c.getCaps = env->GetStaticMethodID(c.activityClass, "rumbleGetCaps", "()I");
            c.rumbleDevice = env->GetStaticMethodID(c.activityClass, "rumbleDevice", "(FFII)V");
            c.rumbleGamepadLegacy = env->GetStaticMethodID(c.activityClass, "rumbleGamepadLegacy", "(FFI)Z");
            c.stopAll = env->GetStaticMethodID(c.activityClass, "rumbleStopAll", "()V");
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
            }
            return true;
        }

        int androidGetCaps()
        {
            JNIEnv* env = nullptr;
            if (!ensureJni(env)) {
                return 0;
            }
            AndroidRumbleJni& c = jniCache();
            if (!c.getCaps) {
                return 0;
            }
            jint caps = env->CallStaticIntMethod(c.activityClass, c.getCaps);
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
                return 0;
            }
            return static_cast<int>(caps);
        }

        void androidRumbleDevice(float low, float high, int durationMs, int primitiveHint)
        {
            JNIEnv* env = nullptr;
            if (!ensureJni(env)) {
                return;
            }
            AndroidRumbleJni& c = jniCache();
            if (!c.rumbleDevice) {
                return;
            }
            env->CallStaticVoidMethod(
                c.activityClass,
                c.rumbleDevice,
                static_cast<jfloat>(low),
                static_cast<jfloat>(high),
                static_cast<jint>(durationMs),
                static_cast<jint>(primitiveHint));
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
            }
        }

        bool androidRumbleGamepadLegacy(float low, float high, int durationMs)
        {
            JNIEnv* env = nullptr;
            if (!ensureJni(env)) {
                return false;
            }
            AndroidRumbleJni& c = jniCache();
            if (!c.rumbleGamepadLegacy) {
                return false;
            }
            jboolean ok = env->CallStaticBooleanMethod(
                c.activityClass,
                c.rumbleGamepadLegacy,
                static_cast<jfloat>(low),
                static_cast<jfloat>(high),
                static_cast<jint>(durationMs));
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
                return false;
            }
            return ok != JNI_FALSE;
        }

        void androidStopAll()
        {
            JNIEnv* env = nullptr;
            if (!ensureJni(env)) {
                return;
            }
            AndroidRumbleJni& c = jniCache();
            if (!c.stopAll) {
                return;
            }
            env->CallStaticVoidMethod(c.activityClass, c.stopAll);
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
            }
        }
#endif
    } // namespace

    RumbleEngine::~RumbleEngine()
    {
        shutdown();
    }

    void RumbleEngine::initialize()
    {
        if (initialized_) {
            return;
        }
        initialized_ = true;
#ifdef __ANDROID__
        androidCaps_ = androidGetCaps();
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
            "OpenDE_Rumble: init android caps=0x%x (device=%d amp=%d prim=%d legacyPad=%d)",
            androidCaps_,
            (androidCaps_ & kCapDeviceVibrator) ? 1 : 0,
            (androidCaps_ & kCapAmplitudeControl) ? 1 : 0,
            (androidCaps_ & kCapPrimitives) ? 1 : 0,
            (androidCaps_ & kCapInputDeviceVibrator) ? 1 : 0);
#endif
    }

    void RumbleEngine::shutdown()
    {
        if (!initialized_) {
            return;
        }
        stopAll();
        controller_ = nullptr;
        controllerHasRumble_ = false;
        controllerHasTriggerRumble_ = false;
        initialized_ = false;
    }

    void RumbleEngine::setSettings(const RumbleSettings& settings)
    {
        bool turningOff = settings.strength == RumbleStrength::Off && settings_.strength != RumbleStrength::Off;
        settings_ = settings;
        if (turningOff) {
            stopAll();
        }
    }

    void RumbleEngine::setController(SDL_Gamepad* controller)
    {
        if (controller_ && controller_ != controller && hwActive_) {
            SDL_RumbleGamepad(controller_, 0, 0, 0);
            if (controllerHasTriggerRumble_) {
                SDL_RumbleGamepadTriggers(controller_, 0, 0, 0);
            }
        }
        controller_ = controller;
        controllerHasRumble_ = false;
        controllerHasTriggerRumble_ = false;
        if (controller_) {
            SDL_PropertiesID props = SDL_GetGamepadProperties(controller_);
            if (props != 0) {
                controllerHasRumble_ = SDL_GetBooleanProperty(props, SDL_PROP_GAMEPAD_CAP_RUMBLE_BOOLEAN, false);
                controllerHasTriggerRumble_ = SDL_GetBooleanProperty(props, SDL_PROP_GAMEPAD_CAP_TRIGGER_RUMBLE_BOOLEAN, false);
            }
        }
#ifdef __ANDROID__
        if (initialized_) {
            androidCaps_ = androidGetCaps();
        }
#endif
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
            "OpenDE_Rumble: controller=%p sdlRumble=%d trigRumble=%d androidCaps=0x%x",
            static_cast<const void*>(controller_),
            controllerHasRumble_ ? 1 : 0,
            controllerHasTriggerRumble_ ? 1 : 0,
            androidCaps_);
    }

    void RumbleEngine::resetPlayerTracking()
    {
        trackedPlayer_ = nullptr;
        prevWeapon_ = nullptr;
        prevClipCount_ = -1;
        prevWeaponFiring_ = false;
        prevLockMode_ = 0;
        prevHeadInWater_ = false;
        prevFootInWater_ = false;
        prevDead_ = false;
        prevHealth_ = -1;
        prevHealthHead_ = -1;
        prevHealthTorso_ = -1;
        prevEnergy_ = -1.0f;
        prevEnergyDrain_ = 0.0f;
        prevPoisonCounter_ = 0;
        prevActiveAugMask_ = 0;
        hasPrevAugMask_ = false;
        heartbeatTimer_ = 0.0f;
        continuousShakeLow_ = 0.0f;
        continuousShakeHigh_ = 0.0f;
        continuousFlameLow_ = 0.0f;
        continuousFlameHigh_ = 0.0f;
    }

    void RumbleEngine::stopAll()
    {
        for (Channel& ch : channels_) {
            ch.active = false;
        }
        continuousShakeLow_ = 0.0f;
        continuousShakeHigh_ = 0.0f;
        continuousFlameLow_ = 0.0f;
        continuousFlameHigh_ = 0.0f;
        pendingImpulse_ = false;
        pendingPrimHint_ = kPrimNone;
        pendingImpulseMs_ = 0;
        stopHardware();
    }

    float RumbleEngine::strengthScale() const
    {
        switch (settings_.strength) {
        case RumbleStrength::Off:
            return 0.0f;
        case RumbleStrength::Low:
            return 0.40f;
        case RumbleStrength::Medium:
            return 0.75f;
        case RumbleStrength::High:
            return 1.00f;
        }
        return 0.75f;
    }

    void RumbleEngine::pushChannel(const Channel& ch, bool replaceSameTag)
    {
        if (settings_.strength == RumbleStrength::Off) {
            return;
        }
        if (ch.duration <= 0.001f) {
            return;
        }

        eventsTriggered_++;

        if (replaceSameTag && ch.tag != ChannelTag::None) {
            for (Channel& slot : channels_) {
                if (slot.active && slot.tag == ch.tag) {
                    slot = ch;
                    slot.active = true;
                    if (ch.delay <= 0.0f) {
                        pendingImpulse_ = true;
                        pendingPrimHint_ = ch.primitiveHint;
                        pendingImpulseMs_ = std::max(pendingImpulseMs_, static_cast<int>(std::lround(ch.duration * 1000.0f)));
                    }
                    return;
                }
            }
        }

        size_t targetIdx = kMaxChannels;
        uint8_t lowestPrio = 255;
        float mostElapsed = -1.0f;

        for (size_t i = 0; i < kMaxChannels; ++i) {
            if (!channels_[i].active) {
                targetIdx = i;
                break;
            }
            if (channels_[i].priority < lowestPrio ||
                (channels_[i].priority == lowestPrio && channels_[i].elapsed > mostElapsed)) {
                lowestPrio = channels_[i].priority;
                mostElapsed = channels_[i].elapsed;
                targetIdx = i;
            }
        }

        if (targetIdx < kMaxChannels) {
            if (channels_[targetIdx].active && channels_[targetIdx].priority > ch.priority) {
                return;
            }
            channels_[targetIdx] = ch;
            channels_[targetIdx].active = true;
            if (ch.delay <= 0.0f) {
                pendingImpulse_ = true;
                if (ch.primitiveHint != kPrimNone && ch.priority >= 2) {
                    pendingPrimHint_ = ch.primitiveHint;
                }
                pendingImpulseMs_ = std::max(pendingImpulseMs_, static_cast<int>(std::lround(ch.duration * 1000.0f)));
            }
        }
    }

    void RumbleEngine::playWeaponFire(std::string_view className, float recoilStrength, int hitDamage, float shotTime, bool silenced, bool handToHand)
    {
        if (weaponFireCooldown_ > 0.0f) {
            return;
        }
        weaponFireCooldown_ = 0.032f;

        if (handToHand) {
            playMeleeSwing();
            return;
        }

        Channel ch {};
        ch.tag = ChannelTag::WeaponFire;
        ch.priority = 6;
        ch.curve = Curve::ExpDecay;

        std::string lc = toLowerAscii(className);
        if (lc.find("sawedoff") != std::string::npos) {
            ch.lowStart = 0.92f;
            ch.highStart = 0.65f;
            ch.trigR = 0.85f;
            ch.duration = 0.145f;
            ch.primitiveHint = kPrimThud;
        } else if (lc.find("shotgun") != std::string::npos) {
            ch.lowStart = 0.85f;
            ch.highStart = 0.55f;
            ch.trigR = 0.80f;
            ch.duration = 0.135f;
            ch.primitiveHint = kPrimThud;
        } else if (lc.find("gep") != std::string::npos || lc.find("law") != std::string::npos) {
            ch.lowStart = 1.00f;
            ch.lowEnd = 0.20f;
            ch.highStart = 0.60f;
            ch.highEnd = 0.10f;
            ch.trigR = 1.00f;
            ch.trigL = 0.45f;
            ch.duration = 0.240f;
            ch.priority = 8;
        } else if (lc.find("plasmarifle") != std::string::npos || lc.find("plasma") != std::string::npos) {
            ch.lowStart = 0.55f;
            ch.highStart = 0.82f;
            ch.trigR = 0.65f;
            ch.duration = 0.115f;
        } else if (lc.find("hideagun") != std::string::npos || lc.find("ps20") != std::string::npos) {
            ch.lowStart = 0.65f;
            ch.highStart = 0.85f;
            ch.trigR = 0.70f;
            ch.duration = 0.140f;
        } else if (lc.find("stealthpistol") != std::string::npos) {
            ch.lowStart = 0.25f;
            ch.highStart = 0.45f;
            ch.trigR = 0.30f;
            ch.duration = 0.052f;
            ch.primitiveHint = kPrimClick;
        } else if (lc.find("minicrossbow") != std::string::npos || lc.find("crossbow") != std::string::npos) {
            ch.lowStart = 0.20f;
            ch.highStart = 0.50f;
            ch.trigR = 0.35f;
            ch.duration = 0.055f;
            ch.primitiveHint = kPrimClick;
        } else if (lc.find("assaultgun") != std::string::npos || lc.find("submachine") != std::string::npos || lc.find("smg") != std::string::npos) {
            ch.lowStart = 0.48f;
            ch.highStart = 0.58f;
            ch.trigR = 0.55f;
            ch.duration = 0.060f;
        } else if (lc.find("rifle") != std::string::npos || lc.find("sniper") != std::string::npos) {
            ch.lowStart = 0.78f;
            ch.highStart = 0.50f;
            ch.trigR = 0.75f;
            ch.duration = 0.125f;
        } else if (lc.find("pistol") != std::string::npos) {
            ch.lowStart = 0.45f;
            ch.highStart = 0.55f;
            ch.trigR = 0.48f;
            ch.duration = 0.072f;
        } else if (lc.find("prod") != std::string::npos) {
            playMeleeImpact(false, true);
            return;
        } else if (lc.find("grenade") != std::string::npos || lc.find("lam") != std::string::npos || lc.find("gas") != std::string::npos || lc.find("emp") != std::string::npos) {
            playGrenadeThrow();
            return;
        } else {
            // Dynamic fallback for mod weapons based on actual weapon properties
            float recoilFactor = clamp01(recoilStrength * 1.15f);
            float dmgFactor = clamp01(static_cast<float>(hitDamage) / 60.0f);
            float power = std::max(recoilFactor, dmgFactor);
            ch.lowStart = std::clamp(0.22f + power * 0.72f, 0.20f, 0.95f);
            ch.highStart = std::clamp(0.30f + power * 0.45f, 0.25f, 0.80f);
            ch.trigR = ch.lowStart * 0.8f;
            float baseDur = shotTime > 0.01f ? std::min(shotTime * 0.55f, 0.14f) : 0.07f;
            ch.duration = std::clamp(baseDur, 0.040f, 0.150f);
        }

        if (silenced && lc.find("stealthpistol") == std::string::npos) {
            ch.lowStart *= 0.62f;
            ch.highStart *= 0.72f;
            ch.trigR *= 0.65f;
            ch.duration *= 0.78f;
        }

        pushChannel(ch, true);
    }

    void RumbleEngine::playMeleeSwing()
    {
        Channel ch {};
        ch.tag = ChannelTag::WeaponFire;
        ch.priority = 3;
        ch.curve = Curve::Linear;
        ch.lowStart = 0.12f;
        ch.highStart = 0.08f;
        ch.duration = 0.028f;
        pushChannel(ch, true);
    }

    void RumbleEngine::playMeleeImpact(bool energyBlade, bool shockProd)
    {
        Channel ch {};
        ch.tag = ChannelTag::WeaponFire;
        ch.priority = 6;
        ch.curve = shockProd ? Curve::Pulse : Curve::ExpDecay;
        if (shockProd) {
            ch.lowStart = 0.25f;
            ch.highStart = 0.90f;
            ch.trigR = 0.60f;
            ch.duration = 0.135f;
        } else if (energyBlade) {
            ch.lowStart = 0.45f;
            ch.highStart = 0.78f;
            ch.trigR = 0.50f;
            ch.duration = 0.095f;
        } else {
            ch.lowStart = 0.55f;
            ch.highStart = 0.50f;
            ch.trigR = 0.45f;
            ch.duration = 0.085f;
            ch.primitiveHint = kPrimThud;
        }
        pushChannel(ch, true);
    }

    void RumbleEngine::playDryFire()
    {
        Channel ch {};
        ch.priority = 3;
        ch.curve = Curve::Linear;
        ch.lowStart = 0.0f;
        ch.highStart = 0.28f;
        ch.trigR = 0.18f;
        ch.duration = 0.022f;
        ch.primitiveHint = kPrimClick;
        pushChannel(ch);
    }

    void RumbleEngine::playReloadStart()
    {
        Channel ch {};
        ch.priority = 3;
        ch.curve = Curve::Linear;
        ch.lowStart = 0.15f;
        ch.highStart = 0.30f;
        ch.duration = 0.032f;
        ch.primitiveHint = kPrimClick;
        pushChannel(ch);
    }

    void RumbleEngine::playReloadFinish()
    {
        Channel ch {};
        ch.priority = 4;
        ch.curve = Curve::ExpDecay;
        ch.lowStart = 0.28f;
        ch.highStart = 0.45f;
        ch.duration = 0.048f;
        ch.primitiveHint = kPrimClick;
        pushChannel(ch);
    }

    void RumbleEngine::playGrenadeThrow()
    {
        Channel ch {};
        ch.priority = 4;
        ch.curve = Curve::ExpDecay;
        ch.lowStart = 0.22f;
        ch.highStart = 0.15f;
        ch.duration = 0.040f;
        pushChannel(ch);
    }

    void RumbleEngine::playGrenadePlant()
    {
        Channel ch {};
        ch.priority = 5;
        ch.curve = Curve::ExpDecay;
        ch.lowStart = 0.28f;
        ch.highStart = 0.52f;
        ch.duration = 0.055f;
        ch.primitiveHint = kPrimClick;
        pushChannel(ch);
    }

    void RumbleEngine::playGepLockOn()
    {
        // Crisp double high-frequency lock-on beep
        Channel c1 {};
        c1.priority = 4;
        c1.curve = Curve::Linear;
        c1.lowStart = 0.0f;
        c1.highStart = 0.28f;
        c1.duration = 0.026f;
        c1.primitiveHint = kPrimTick;
        pushChannel(c1);

        Channel c2 = c1;
        c2.delay = 0.060f;
        c2.highStart = 0.34f;
        pushChannel(c2);
    }

    void RumbleEngine::playExplosion(float distance, float radius)
    {
        float maxRange = std::max(radius, 1400.0f);
        if (distance >= maxRange) {
            return;
        }
        float norm = 1.0f - (distance / maxRange);
        float atten = norm * norm; // inverse-square feel
        if (atten < 0.06f) {
            return;
        }

        Channel ch {};
        ch.tag = ChannelTag::Explosion;
        ch.priority = 9;
        ch.curve = Curve::ExpDecay;
        ch.lowStart = std::clamp(0.25f + atten * 0.75f, 0.25f, 1.00f);
        ch.highStart = std::clamp(atten * 0.85f, 0.05f, 0.85f);
        ch.trigL = ch.lowStart * 0.65f;
        ch.trigR = ch.lowStart * 0.65f;
        ch.duration = 0.18f + atten * 0.28f;
        pushChannel(ch, false);
    }

    void RumbleEngine::playDamage(float amount, std::string_view damageType)
    {
        float sev = clamp01(amount / 45.0f);
        std::string dt = toLowerAscii(damageType);

        Channel ch {};
        ch.tag = ChannelTag::Damage;
        ch.priority = 8;
        ch.curve = Curve::ExpDecay;

        if (dt.find("exploded") != std::string::npos) {
            ch.lowStart = 1.00f;
            ch.highStart = 0.85f;
            ch.trigL = 0.70f;
            ch.trigR = 0.70f;
            ch.duration = 0.28f;
        } else if (dt.find("emp") != std::string::npos || dt.find("shock") != std::string::npos) {
            ch.curve = Curve::Pulse;
            ch.lowStart = 0.20f;
            ch.highStart = 0.88f;
            ch.duration = 0.18f;
        } else if (dt.find("burn") != std::string::npos || dt.find("flame") != std::string::npos) {
            ch.tag = ChannelTag::Burn;
            ch.priority = 5;
            ch.lowStart = 0.32f;
            ch.highStart = 0.55f;
            ch.duration = 0.075f;
        } else if (dt.find("poison") != std::string::npos || dt.find("gas") != std::string::npos || dt.find("rad") != std::string::npos) {
            ch.lowStart = 0.35f;
            ch.highStart = 0.15f;
            ch.duration = 0.12f;
        } else if (dt.find("drown") != std::string::npos) {
            ch.lowStart = 0.60f;
            ch.highStart = 0.10f;
            ch.duration = 0.16f;
        } else {
            ch.lowStart = 0.38f + sev * 0.52f;
            ch.highStart = 0.32f + sev * 0.43f;
            ch.trigL = ch.lowStart * 0.45f;
            ch.duration = 0.085f + sev * 0.075f;
        }
        pushChannel(ch, false);
    }

    void RumbleEngine::playDeath()
    {
        Channel ch {};
        ch.priority = 10;
        ch.curve = Curve::ExpDecay;
        ch.lowStart = 0.88f;
        ch.highStart = 0.32f;
        ch.duration = 0.32f;
        pushChannel(ch);
    }

    void RumbleEngine::playLanding(float impactSpeed, bool speedAugActive)
    {
        float spd = std::fabs(impactSpeed);
        if (spd < 240.0f) {
            return;
        }
        float augScale = speedAugActive ? 0.55f : 1.0f;
        float t = clamp01((spd - 240.0f) / 560.0f);

        Channel ch {};
        ch.priority = 5;
        ch.curve = Curve::ExpDecay;
        ch.lowStart = (0.22f + t * 0.66f) * augScale;
        ch.highStart = (0.08f + t * 0.40f) * augScale;
        ch.duration = 0.042f + t * 0.125f;
        ch.primitiveHint = (t < 0.45f) ? kPrimThud : kPrimNone;
        pushChannel(ch);
    }

    void RumbleEngine::playWaterSplash()
    {
        Channel ch {};
        ch.priority = 4;
        ch.curve = Curve::ExpDecay;
        ch.lowStart = 0.32f;
        ch.highStart = 0.22f;
        ch.duration = 0.085f;
        pushChannel(ch);
    }

    void RumbleEngine::playLockpickTick()
    {
        Channel ch {};
        ch.tag = ChannelTag::Lockpick;
        ch.priority = 3;
        ch.curve = Curve::Linear;
        ch.lowStart = 0.08f;
        ch.highStart = 0.32f;
        ch.duration = 0.024f;
        ch.primitiveHint = kPrimClick;
        pushChannel(ch, true);
    }

    void RumbleEngine::playMultitoolPulse()
    {
        Channel ch {};
        ch.tag = ChannelTag::Multitool;
        ch.priority = 3;
        ch.curve = Curve::Pulse;
        ch.lowStart = 0.05f;
        ch.highStart = 0.30f;
        ch.duration = 0.095f;
        pushChannel(ch, true);
    }

    void RumbleEngine::playToolComplete()
    {
        Channel ch {};
        ch.priority = 5;
        ch.curve = Curve::ExpDecay;
        ch.lowStart = 0.30f;
        ch.highStart = 0.60f;
        ch.duration = 0.065f;
        ch.primitiveHint = kPrimClick;
        pushChannel(ch);
    }

    void RumbleEngine::playAugToggle(bool active)
    {
        Channel ch {};
        ch.priority = 4;
        if (active) {
            ch.curve = Curve::RampUp;
            ch.lowStart = 0.08f;
            ch.lowEnd = 0.24f;
            ch.highStart = 0.18f;
            ch.highEnd = 0.56f;
            ch.duration = 0.085f;
        } else {
            ch.curve = Curve::ExpDecay;
            ch.lowStart = 0.12f;
            ch.highStart = 0.26f;
            ch.duration = 0.045f;
            ch.primitiveHint = kPrimTick;
        }
        pushChannel(ch);
    }

    void RumbleEngine::playEnergyDepleted()
    {
        Channel c1 {};
        c1.priority = 5;
        c1.curve = Curve::Linear;
        c1.lowStart = 0.30f;
        c1.highStart = 0.45f;
        c1.duration = 0.050f;
        pushChannel(c1);

        Channel c2 = c1;
        c2.delay = 0.095f;
        c2.lowStart = 0.22f;
        c2.highStart = 0.35f;
        pushChannel(c2);
    }

    void RumbleEngine::playKeypadKey()
    {
        Channel ch {};
        ch.priority = 2;
        ch.curve = Curve::Linear;
        ch.lowStart = 0.0f;
        ch.highStart = 0.20f;
        ch.duration = 0.018f;
        ch.primitiveHint = kPrimTick;
        pushChannel(ch);
    }

    void RumbleEngine::playKeypadResult(bool granted)
    {
        if (granted) {
            Channel ch {};
            ch.priority = 4;
            ch.curve = Curve::ExpDecay;
            ch.lowStart = 0.15f;
            ch.highStart = 0.45f;
            ch.duration = 0.055f;
            ch.primitiveHint = kPrimClick;
            pushChannel(ch);
        } else {
            Channel c1 {};
            c1.priority = 4;
            c1.curve = Curve::Linear;
            c1.lowStart = 0.40f;
            c1.highStart = 0.35f;
            c1.duration = 0.045f;
            pushChannel(c1);

            Channel c2 = c1;
            c2.delay = 0.085f;
            pushChannel(c2);
        }
    }

    void RumbleEngine::playFrob()
    {
        if (soundFrobCooldown_ > 0.0f) {
            return;
        }
        soundFrobCooldown_ = 0.08f;
        Channel ch {};
        ch.priority = 2;
        ch.curve = Curve::Linear;
        ch.lowStart = 0.08f;
        ch.highStart = 0.20f;
        ch.duration = 0.020f;
        ch.primitiveHint = kPrimTick;
        pushChannel(ch);
    }

    void RumbleEngine::playSkillPoints()
    {
        Channel ch {};
        ch.priority = 4;
        ch.curve = Curve::RampUp;
        ch.lowStart = 0.10f;
        ch.lowEnd = 0.22f;
        ch.highStart = 0.25f;
        ch.highEnd = 0.52f;
        ch.duration = 0.070f;
        pushChannel(ch);
    }

    void RumbleEngine::playTouchButton()
    {
        if (!settings_.touchButtonHaptics || settings_.strength == RumbleStrength::Off) {
            return;
        }
        Channel ch {};
        ch.tag = ChannelTag::TouchUI;
        ch.priority = 1;
        ch.curve = Curve::Linear;
        ch.lowStart = 0.0f;
        ch.highStart = 0.22f;
        ch.duration = 0.016f;
        ch.primitiveHint = kPrimTick;
        pushChannel(ch, true);
    }

    void RumbleEngine::onHearSound(std::string_view soundName, float distance, float radius, bool fromPlayerOrOwned)
    {
        if (settings_.strength == RumbleStrength::Off || soundName.empty()) {
            return;
        }
        std::string s = toLowerAscii(soundName);

        // 1. Spatial explosions (LAM, GEP, TNT, Bots, Turrets, Cars)
        if (s.find("explosion") != std::string::npos || s.find("explode") != std::string::npos) {
            if (soundExplosionCooldown_ <= 0.0f) {
                soundExplosionCooldown_ = 0.07f;
                playExplosion(distance, radius);
            }
            return;
        }

        // 2. Player-centric audio cues
        if (fromPlayerOrOwned || distance < 140.0f) {
            // Backup weapon-fire sound detection (covers any weapon/mod whose ClipCount/bFiring doesn't toggle)
            if (fromPlayerOrOwned && s.find("fire") != std::string::npos && s.find("dryfire") == std::string::npos && s.find("burn") == std::string::npos) {
                if (s.find("shotgun") != std::string::npos) {
                    playWeaponFire("WeaponAssaultShotgun", 0.7f, 40, 0.5f, false, false);
                    return;
                }
                if (s.find("gep") != std::string::npos || s.find("rocket") != std::string::npos) {
                    playWeaponFire("WeaponGEPGun", 1.0f, 100, 1.0f, false, false);
                    return;
                }
                if (s.find("plasma") != std::string::npos) {
                    playWeaponFire("WeaponPlasmaRifle", 0.5f, 35, 0.3f, false, false);
                    return;
                }
                if (s.find("sniper") != std::string::npos || s.find("rifle") != std::string::npos) {
                    playWeaponFire("WeaponRifle", 0.6f, 30, 0.6f, false, false);
                    return;
                }
                if (s.find("assault") != std::string::npos || s.find("smg") != std::string::npos) {
                    playWeaponFire("WeaponAssaultGun", 0.35f, 12, 0.12f, false, false);
                    return;
                }
                if (s.find("stealthpistol") != std::string::npos) {
                    playWeaponFire("WeaponStealthPistol", 0.2f, 10, 0.2f, true, false);
                    return;
                }
                if (s.find("pistol") != std::string::npos) {
                    playWeaponFire("WeaponPistol", 0.3f, 14, 0.25f, false, false);
                    return;
                }
                if (s.find("crossbow") != std::string::npos) {
                    playWeaponFire("WeaponMiniCrossbow", 0.2f, 20, 0.4f, true, false);
                    return;
                }
            }
            if (s.find("dryfire") != std::string::npos || s.find("emptyclick") != std::string::npos) {
                playDryFire();
                return;
            }
            if (s.find("reloadbegin") != std::string::npos || s.find("reloadstart") != std::string::npos) {
                playReloadStart();
                return;
            }
            if (s.find("reloadend") != std::string::npos || s.find("reloadfinish") != std::string::npos || s.find("cock") != std::string::npos) {
                playReloadFinish();
                return;
            }
            if (s.find("lockpick") != std::string::npos) {
                if (soundToolCooldown_ <= 0.0f) {
                    soundToolCooldown_ = 0.15f;
                    playLockpickTick();
                }
                return;
            }
            if (s.find("multitool") != std::string::npos) {
                if (soundToolCooldown_ <= 0.0f) {
                    soundToolCooldown_ = 0.16f;
                    playMultitoolPulse();
                }
                return;
            }
            if (s.find("unlock") != std::string::npos || s.find("hack") != std::string::npos) {
                playToolComplete();
                return;
            }
            if (s.find("logskillpoints") != std::string::npos) {
                playSkillPoints();
                return;
            }
            if (s.find("keypadbeep") != std::string::npos || s.find("keypadkey") != std::string::npos || s.find("pushbutton") != std::string::npos) {
                playKeypadKey();
                return;
            }
            if (s.find("keypadvalid") != std::string::npos || s.find("accessgranted") != std::string::npos) {
                playKeypadResult(true);
                return;
            }
            if (s.find("keypadinvalid") != std::string::npos || s.find("accessdenied") != std::string::npos || s.find("buzz") != std::string::npos) {
                playKeypadResult(false);
                return;
            }
            if (s.find("prod") != std::string::npos && (s.find("fire") != std::string::npos || s.find("shock") != std::string::npos)) {
                playMeleeImpact(false, true);
                return;
            }
            if (s.find("sword") != std::string::npos && s.find("hit") != std::string::npos) {
                playMeleeImpact(true, false);
                return;
            }
            if (s.find("meleehit") != std::string::npos || s.find("fleshhit") != std::string::npos || s.find("woodhit") != std::string::npos || s.find("metalhit") != std::string::npos || s.find("stonehit") != std::string::npos) {
                playMeleeImpact(false, false);
                return;
            }
            if (s.find("lamplant") != std::string::npos || s.find("grenadeplant") != std::string::npos || s.find("beep") != std::string::npos) {
                if (s.find("plant") != std::string::npos) {
                    playGrenadePlant();
                    return;
                }
            }
            if (s.find("pickup") != std::string::npos || s.find("inventory") != std::string::npos) {
                playFrob();
                return;
            }
        }
    }

    void RumbleEngine::samplePlayerState(dx::UObject* player, float dt, bool inGameplay)
    {
        if (!player || player->destroyed || !player->data || !dx::isPlayerPawn(player)) {
            resetPlayerTracking();
            return;
        }

        if (trackedPlayer_ != player) {
            resetPlayerTracking();
            trackedPlayer_ = player;
        }

        const bool isDxPlayer = dx::offsets_deusexplayer.Energy != 0 &&
            player->cls && player->cls->isChildOf(dx::objects::findClass("DeusEx.DeusExPlayer"));

        // 1. Continuous camera shake (ShakeView: helicopters, scripted quakes, heavy impacts)
        continuousShakeLow_ = 0.0f;
        continuousShakeHigh_ = 0.0f;
        if (inGameplay && dx::offsets_playerpawn.shaketimer != 0 && dx::offsets_playerpawn.shakemag != 0) {
            float st = dx::playerpawn::shaketimer(player);
            int32_t sm = dx::playerpawn::shakemag(player);
            if (st > 0.01f && sm > 50) {
                float mag = clamp01(static_cast<float>(sm) / 1500.0f);
                continuousShakeLow_ = 0.14f + mag * 0.72f;
                continuousShakeHigh_ = 0.04f + mag * 0.30f;
            }
        }

        // 2. Health / Damage / Death detection
        int32_t curHealth = dx::pawn::Health(player);
        int32_t curHead = dx::offsets_pawn.HealthHead != 0 ? dx::pawn::HealthHead(player) : curHealth;
        int32_t curTorso = dx::offsets_pawn.HealthTorso != 0 ? dx::pawn::HealthTorso(player) : curHealth;
        bool isDead = dx::vm::stateName(player) == dx::Name("Dying") || curHealth <= 0 || curHead <= 0 || curTorso <= 0;

        if (inGameplay && prevHealth_ >= 0) {
            int32_t totalDrop = 0;
            if (curHealth < prevHealth_) {
                totalDrop = std::max(totalDrop, prevHealth_ - curHealth);
            }
            if (prevHealthHead_ >= 0 && curHead < prevHealthHead_) {
                totalDrop = std::max(totalDrop, prevHealthHead_ - curHead);
            }
            if (prevHealthTorso_ >= 0 && curTorso < prevHealthTorso_) {
                totalDrop = std::max(totalDrop, prevHealthTorso_ - curTorso);
            }
            if (isDead && !prevDead_) {
                playDeath();
            } else if (totalDrop >= 2) {
                // Infer damage flavor from active FlashFog color set by PlayDXTakeDamageHit
                std::string_view flavor = "Shot";
                if (dx::offsets_playerpawn.DesiredFlashFog != 0) {
                    const dx::Vector& fog = dx::playerpawn::DesiredFlashFog(player);
                    if (fog.z > 120.0f && fog.y > 120.0f && fog.x < 40.0f) {
                        flavor = "EMP";
                    } else if (fog.z > 120.0f && fog.x < 40.0f) {
                        flavor = "Drowned";
                    } else if (fog.y > 100.0f && fog.x < 80.0f) {
                        flavor = "PoisonGas";
                    } else if (fog.x > 140.0f && fog.y > 70.0f) {
                        flavor = "Burned";
                    }
                }
                playDamage(static_cast<float>(totalDrop), flavor);
            }
        }
        prevHealth_ = curHealth;
        prevHealthHead_ = curHead;
        prevHealthTorso_ = curTorso;
        prevDead_ = isDead;

        // 3. Critical Health Heartbeat (< 25% head or torso while alive in gameplay)
        if (inGameplay && !isDead && (curHead < 25 || curTorso < 25)) {
            heartbeatTimer_ -= dt;
            if (heartbeatTimer_ <= 0.0f) {
                heartbeatTimer_ = 1.35f;
                Channel lub {};
                lub.tag = ChannelTag::Heartbeat;
                lub.priority = 2;
                lub.curve = Curve::ExpDecay;
                lub.lowStart = 0.22f;
                lub.highStart = 0.0f;
                lub.duration = 0.048f;
                pushChannel(lub);

                Channel dub = lub;
                dub.delay = 0.155f;
                dub.lowStart = 0.16f;
                dub.duration = 0.042f;
                pushChannel(dub);
            }
        } else {
            heartbeatTimer_ = 0.25f;
        }

        // 4. On-fire crackling & EMP drain & Poison
        if (inGameplay && !isDead && dx::offsets_pawn.bOnFire != 0 && dx::pawn::bOnFire(player) != 0) {
            burnPulseCooldown_ -= dt;
            if (burnPulseCooldown_ <= 0.0f) {
                burnPulseCooldown_ = 0.38f;
                playDamage(8.0f, "Burned");
            }
        }

        if (isDxPlayer && inGameplay && !isDead) {
            float curEnergy = dx::deusexplayer::Energy(player);
            float curDrain = dx::deusexplayer::EnergyDrain(player);
            if (curDrain > prevEnergyDrain_ + 1.0f && empPulseCooldown_ <= 0.0f) {
                empPulseCooldown_ = 0.28f;
                playDamage(15.0f, "EMP");
            }
            if (prevEnergy_ > 1.0f && curEnergy <= 0.01f && prevActiveAugMask_ != 0) {
                playEnergyDepleted();
            }
            prevEnergy_ = curEnergy;
            prevEnergyDrain_ = curDrain;

            int32_t curPoison = dx::deusexplayer::poisonCounter(player);
            if (curPoison > prevPoisonCounter_) {
                playDamage(6.0f, "PoisonGas");
            }
            prevPoisonCounter_ = curPoison;
        }

        // 5. Water Entry Splash
        bool headInWater = false;
        bool footInWater = false;
        if (dx::offsets_pawn.HeadRegion != 0 && dx::offsets_zoneinfo.bWaterZone != 0) {
            dx::UObject* hz = reinterpret_cast<const dx::PointRegion*>(dx::pawn::HeadRegion(player))->zone;
            if (hz && !hz->destroyed && hz->data && hz->hasClassBit(dx::CLS_ZoneInfo)) {
                headInWater = dx::zoneinfo::bWaterZone(hz) != 0;
            }
        }
        if (dx::offsets_pawn.FootRegion != 0 && dx::offsets_zoneinfo.bWaterZone != 0) {
            dx::UObject* fz = reinterpret_cast<const dx::PointRegion*>(dx::pawn::FootRegion(player))->zone;
            if (fz && !fz->destroyed && fz->data && fz->hasClassBit(dx::CLS_ZoneInfo)) {
                footInWater = dx::zoneinfo::bWaterZone(fz) != 0;
            }
        }
        if (inGameplay && ((headInWater && !prevHeadInWater_) || (footInWater && !prevFootInWater_ && dx::actor::Velocity(player).z < -120.0f))) {
            playWaterSplash();
        }
        prevHeadInWater_ = headInWater;
        prevFootInWater_ = footInWater;

        // 6. Active Weapon Firing, Flamethrower, GEP Lock-On
        continuousFlameLow_ = 0.0f;
        continuousFlameHigh_ = 0.0f;
        dx::UObject* weapon = nullptr;
        if (isDxPlayer && dx::offsets_deusexplayer.inHand != 0) {
            weapon = dx::deusexplayer::inHand(player);
        }
        if (!weapon && dx::offsets_pawn.Weapon != 0) {
            weapon = dx::pawn::Weapon(player);
        }

        const bool isDxWeapon = weapon && !weapon->destroyed && weapon->data && weapon->cls &&
            dx::offsets_deusexweapon.ClipCount != 0 &&
            weapon->cls->isChildOf(dx::objects::findClass("DeusEx.DeusExWeapon"));

        if (isDxWeapon && inGameplay && !isDead) {
            int32_t clip = dx::deusexweapon::ClipCount(weapon);
            bool firing = dx::offsets_deusexweapon.bFiring != 0 && dx::deusexweapon::bFiring(weapon) != 0;
            bool flameOn = dx::offsets_deusexweapon.bFlameOn != 0 && dx::deusexweapon::bFlameOn(weapon) != 0;
            int32_t lockMode = dx::offsets_deusexweapon.LockMode != 0 ? static_cast<int32_t>(dx::deusexweapon::LockMode(weapon)) : 0;

            if (flameOn) {
                // Organic fluctuating flame pressure
                float wave = 0.5f + 0.5f * std::sin(static_cast<float>(SDL_GetTicks()) * 0.035f);
                continuousFlameLow_ = 0.30f + wave * 0.14f;
                continuousFlameHigh_ = 0.24f + (1.0f - wave) * 0.14f;
            }

            if (weapon == prevWeapon_) {
                bool clipIncreased = clip > prevClipCount_ && prevClipCount_ >= 0;
                bool firedEdge = (firing && !prevWeaponFiring_) || clipIncreased;
                if (firedEdge && !flameOn) {
                    float recoil = dx::offsets_deusexweapon.recoilStrength != 0 ? dx::deusexweapon::recoilStrength(weapon) : 0.3f;
                    int32_t dmg = dx::offsets_deusexweapon.HitDamage != 0 ? dx::deusexweapon::HitDamage(weapon) : 15;
                    float st = dx::offsets_deusexweapon.ShotTime != 0 ? dx::deusexweapon::ShotTime(weapon) : 0.25f;
                    bool sil = dx::offsets_deusexweapon.bHasSilencer != 0 && dx::deusexweapon::bHasSilencer(weapon) != 0;
                    bool h2h = dx::offsets_deusexweapon.bHandToHand != 0 && dx::deusexweapon::bHandToHand(weapon) != 0;
                    playWeaponFire(weapon->cls->name.str(), recoil, dmg, st, sil, h2h);
                }
                // GEP LockMode: 2 = LOCK_Locked in DeusExWeapon
                if (lockMode == 2 && prevLockMode_ != 2) {
                    playGepLockOn();
                }
            }
            prevWeapon_ = weapon;
            prevClipCount_ = clip;
            prevWeaponFiring_ = firing;
            prevLockMode_ = lockMode;
        } else {
            prevWeapon_ = nullptr;
            prevClipCount_ = -1;
            prevWeaponFiring_ = false;
            prevLockMode_ = 0;
        }

        // 7. Augmentation Toggle Detection
        if (isDxPlayer && dx::offsets_deusexplayer.AugmentationSystem != 0) {
            dx::UObject* augMgr = dx::deusexplayer::AugmentationSystem(player);
            if (augMgr && !augMgr->destroyed && augMgr->cls) {
                static const dx::Name nameFirstAug("FirstAug");
                static const dx::Name nameNext("next");
                static const dx::Name nameIsActive("bIsActive");
                static const dx::Name nameAlwaysActive("bAlwaysActive");
                dx::UProperty* firstProp = augMgr->cls->findProperty(nameFirstAug);
                dx::UObject* aug = (firstProp && firstProp->type == dx::PropType::Object)
                    ? *reinterpret_cast<dx::UObject* const*>(augMgr->data + firstProp->offset)
                    : nullptr;
                uint32_t mask = 0;
                uint32_t bitIdx = 0;
                for (int guard = 0; aug && !aug->destroyed && aug->cls && guard < 24; ++guard, ++bitIdx) {
                    dx::UProperty* pAct = aug->cls->findProperty(nameIsActive);
                    dx::UProperty* pAlw = aug->cls->findProperty(nameAlwaysActive);
                    bool alwaysAct = (pAlw && pAlw->type == dx::PropType::Bool) && (*reinterpret_cast<const uint32_t*>(aug->data + pAlw->offset) != 0);
                    bool isAct = (pAct && pAct->type == dx::PropType::Bool) && (*reinterpret_cast<const uint32_t*>(aug->data + pAct->offset) != 0);
                    if (!alwaysAct && isAct) {
                        mask |= (1u << bitIdx);
                    }
                    dx::UProperty* nextProp = aug->cls->findProperty(nameNext);
                    aug = (nextProp && nextProp->type == dx::PropType::Object)
                        ? *reinterpret_cast<dx::UObject* const*>(aug->data + nextProp->offset)
                        : nullptr;
                }
                if (hasPrevAugMask_ && inGameplay && mask != prevActiveAugMask_) {
                    uint32_t turnedOn = mask & ~prevActiveAugMask_;
                    uint32_t turnedOff = prevActiveAugMask_ & ~mask;
                    if (turnedOn != 0) {
                        playAugToggle(true);
                    } else if (turnedOff != 0) {
                        playAugToggle(false);
                    }
                }
                prevActiveAugMask_ = mask;
                hasPrevAugMask_ = true;
            }
        }
    }

    void RumbleEngine::update(float dt, bool suppressOutput, bool lastInputWasGamepad)
    {
        float clampedDt = std::clamp(dt, 0.0f, 0.1f);
        empPulseCooldown_ = std::max(0.0f, empPulseCooldown_ - clampedDt);
        burnPulseCooldown_ = std::max(0.0f, burnPulseCooldown_ - clampedDt);
        soundExplosionCooldown_ = std::max(0.0f, soundExplosionCooldown_ - clampedDt);
        soundToolCooldown_ = std::max(0.0f, soundToolCooldown_ - clampedDt);
        soundFrobCooldown_ = std::max(0.0f, soundFrobCooldown_ - clampedDt);
        weaponFireCooldown_ = std::max(0.0f, weaponFireCooldown_ - clampedDt);

        float scale = strengthScale();
        if (suppressOutput || scale <= 0.001f) {
            if (hwActive_) {
                stopHardware();
            }
            // Still advance short channels so they expire cleanly when unpaused
            for (Channel& ch : channels_) {
                if (!ch.active) continue;
                ch.elapsed += clampedDt;
                if (ch.elapsed >= ch.duration + ch.delay) {
                    ch.active = false;
                }
            }
            return;
        }

        float mixLow = std::max(continuousShakeLow_, continuousFlameLow_);
        float mixHigh = std::max(continuousShakeHigh_, continuousFlameHigh_);
        float mixTrigL = 0.0f;
        float mixTrigR = 0.0f;
        int maxRemainingMs = (mixLow > 0.01f || mixHigh > 0.01f) ? 80 : 0;
        int chosenPrim = pendingPrimHint_;

        for (Channel& ch : channels_) {
            if (!ch.active) {
                continue;
            }
            if (ch.delay > 0.0f) {
                ch.delay -= clampedDt;
                if (ch.delay > 0.0f) {
                    continue;
                }
                pendingImpulse_ = true;
                if (ch.primitiveHint != kPrimNone) {
                    chosenPrim = ch.primitiveHint;
                }
            }

            float t = clamp01(ch.elapsed / std::max(ch.duration, 0.001f));
            float env = 1.0f;
            switch (ch.curve) {
            case Curve::Linear:
                env = 1.0f - t;
                break;
            case Curve::ExpDecay:
                env = std::exp(-3.2f * t);
                break;
            case Curve::RampUp:
                env = t;
                break;
            case Curve::Pulse:
                env = (std::sin(t * 24.0f) >= 0.0f ? 1.0f : 0.35f) * (1.0f - 0.5f * t);
                break;
            }

            float low = 0.0f;
            float high = 0.0f;
            if (ch.curve == Curve::RampUp) {
                low = ch.lowStart + (ch.lowEnd - ch.lowStart) * env;
                high = ch.highStart + (ch.highEnd - ch.highStart) * env;
            } else {
                low = ch.lowEnd + (ch.lowStart - ch.lowEnd) * env;
                high = ch.highEnd + (ch.highStart - ch.highEnd) * env;
            }

            mixLow = std::max(mixLow, low);
            mixHigh = std::max(mixHigh, high);
            mixTrigL = std::max(mixTrigL, ch.trigL * env);
            mixTrigR = std::max(mixTrigR, ch.trigR * env);

            int remMs = static_cast<int>(std::lround((ch.duration - ch.elapsed) * 1000.0f));
            maxRemainingMs = std::max(maxRemainingMs, remMs);

            ch.elapsed += clampedDt;
            if (ch.elapsed >= ch.duration) {
                ch.active = false;
            }
        }

        mixLow = clamp01(mixLow * scale);
        mixHigh = clamp01(mixHigh * scale);
        mixTrigL = clamp01(mixTrigL * scale);
        mixTrigR = clamp01(mixTrigR * scale);

        const bool nowZero = mixLow <= 0.005f && mixHigh <= 0.005f && mixTrigL <= 0.005f && mixTrigR <= 0.005f;
        const bool bigJump = (mixLow > lastSentLow_ + 0.14f) || (mixHigh > lastSentHigh_ + 0.14f);
        const bool isErmOnly = !controllerHasRumble_ && ((androidCaps_ & kCapAmplitudeControl) == 0);
        const bool hasContinuous = continuousShakeLow_ > 0.01f || continuousFlameLow_ > 0.01f;

        hwTimer_ += clampedDt;
        bool shouldSend = false;
        if (nowZero) {
            shouldSend = hwActive_;
        } else if (pendingImpulse_ || bigJump || !hwActive_) {
            shouldSend = true;
        } else if (hwTimer_ >= kHwUpdateInterval) {
            // On ERM motors without amplitude control, avoid superseding a decaying one-shot pulse
            // unless continuous rumble (flamethrower / quake) is active.
            shouldSend = !isErmOnly || hasContinuous;
        }

        if (shouldSend) {
            if (nowZero) {
                // On Android one-shot device vibrator, let the short pulse finish naturally instead
                // of cancelling it mid-spinup (which cuts off ERM motors after ~8-15ms).
                if (controller_ && controllerHasRumble_) {
                    SDL_RumbleGamepad(controller_, 0, 0, 0);
                    if (controllerHasTriggerRumble_) {
                        SDL_RumbleGamepadTriggers(controller_, 0, 0, 0);
                    }
                }
                hwActive_ = false;
                lastSentLow_ = 0.0f;
                lastSentHigh_ = 0.0f;
                lastSentTrigL_ = 0.0f;
                lastSentTrigR_ = 0.0f;
            } else {
                int sendMs = std::clamp(std::max(maxRemainingMs, pendingImpulseMs_), 25, 450);
                if (!pendingImpulse_) {
                    chosenPrim = kPrimNone;
                }
                applyHardwareOutput(mixLow, mixHigh, mixTrigL, mixTrigR, sendMs, chosenPrim, pendingImpulse_ || bigJump, lastInputWasGamepad);
            }
            hwTimer_ = 0.0f;
            pendingImpulse_ = false;
            pendingPrimHint_ = kPrimNone;
            pendingImpulseMs_ = 0;
        }
    }

    void RumbleEngine::applyHardwareOutput(float low, float high, float trigL, float trigR, int durationMs, int primitiveHint, bool, bool lastInputWasGamepad)
    {
        if (low <= 0.005f && high <= 0.005f && trigL <= 0.005f && trigR <= 0.005f) {
            stopHardware();
            return;
        }

        bool wantGamepad = false;
        bool wantDevice = false;

        const bool padCanRumble = controller_ != nullptr &&
            (controllerHasRumble_ || ((androidCaps_ & kCapInputDeviceVibrator) != 0));
        const bool deviceCanRumble = (androidCaps_ & kCapDeviceVibrator) != 0;

        switch (settings_.target) {
        case RumbleTarget::Auto:
            if (lastInputWasGamepad && padCanRumble) {
                wantGamepad = true;
            } else if (deviceCanRumble) {
                wantDevice = true;
            } else if (padCanRumble) {
                wantGamepad = true;
            }
            break;
        case RumbleTarget::GamepadOnly:
            wantGamepad = padCanRumble;
            break;
        case RumbleTarget::DeviceOnly:
            wantDevice = deviceCanRumble;
            break;
        case RumbleTarget::Both:
            wantGamepad = padCanRumble;
            wantDevice = deviceCanRumble;
            break;
        }

        bool sentPad = false;
        if (wantGamepad && controller_) {
            if (controllerHasRumble_) {
                sentPad = SDL_RumbleGamepad(controller_, toUint16(low), toUint16(high), static_cast<Uint32>(durationMs));
                if (controllerHasTriggerRumble_ && (trigL > 0.01f || trigR > 0.01f)) {
                    SDL_RumbleGamepadTriggers(controller_, toUint16(trigL), toUint16(trigR), static_cast<Uint32>(durationMs));
                }
            }
#ifdef __ANDROID__
            if (!sentPad && (androidCaps_ & kCapInputDeviceVibrator) != 0) {
                sentPad = androidRumbleGamepadLegacy(low, high, durationMs);
            }
#endif
        }

        // If Auto wanted gamepad but driver refused rumble, fall back seamlessly to device vibrator.
        if (settings_.target == RumbleTarget::Auto && wantGamepad && !sentPad && deviceCanRumble) {
            wantDevice = true;
        }

        bool sentDevice = false;
#ifdef __ANDROID__
        if (wantDevice && deviceCanRumble) {
            androidRumbleDevice(low, high, durationMs, primitiveHint);
            sentDevice = true;
        }
#else
        (void)primitiveHint;
#endif

        if (sentPad && sentDevice) {
            activeOutput_ = "Both";
        } else if (sentPad) {
            activeOutput_ = "Gamepad";
        } else if (sentDevice) {
            activeOutput_ = "Device";
        } else {
            activeOutput_ = "None";
        }

        hwActive_ = sentPad || sentDevice;
        lastSentLow_ = low;
        lastSentHigh_ = high;
        lastSentTrigL_ = trigL;
        lastSentTrigR_ = trigR;
    }

    void RumbleEngine::stopHardware()
    {
        if (controller_ && controllerHasRumble_) {
            SDL_RumbleGamepad(controller_, 0, 0, 0);
            if (controllerHasTriggerRumble_) {
                SDL_RumbleGamepadTriggers(controller_, 0, 0, 0);
            }
        }
#ifdef __ANDROID__
        androidStopAll();
#endif
        hwActive_ = false;
        lastSentLow_ = 0.0f;
        lastSentHigh_ = 0.0f;
        lastSentTrigL_ = 0.0f;
        lastSentTrigR_ = 0.0f;
    }

    bool RumbleEngine::triggerTestPreset(std::string_view preset)
    {
        std::string p = toLowerAscii(preset);
        if (p == "pistol") {
            playWeaponFire("WeaponPistol", 0.3f, 14, 0.25f, false, false);
        } else if (p == "shotgun") {
            playWeaponFire("WeaponAssaultShotgun", 0.7f, 40, 0.5f, false, false);
        } else if (p == "gep") {
            playWeaponFire("WeaponGEPGun", 1.0f, 100, 1.0f, false, false);
        } else if (p == "explosion") {
            playExplosion(120.0f, 1400.0f);
        } else if (p == "damage") {
            playDamage(28.0f, "Shot");
        } else if (p == "emp") {
            playDamage(25.0f, "EMP");
        } else if (p == "lockpick") {
            playLockpickTick();
        } else if (p == "aug") {
            playAugToggle(true);
        } else if (p == "land") {
            playLanding(620.0f, false);
        } else if (p == "heartbeat") {
            heartbeatTimer_ = 0.0f;
            Channel lub {};
            lub.priority = 5;
            lub.curve = Curve::ExpDecay;
            lub.lowStart = 0.30f;
            lub.duration = 0.055f;
            pushChannel(lub);
            Channel dub = lub;
            dub.delay = 0.16f;
            dub.lowStart = 0.22f;
            pushChannel(dub);
        } else if (p == "stop") {
            stopAll();
        } else {
            return false;
        }
        return true;
    }

    RumbleDebugInfo RumbleEngine::debugInfo() const
    {
        RumbleDebugInfo info;
        info.controllerConnected = controller_ != nullptr;
        info.controllerHasRumble = controllerHasRumble_;
        info.controllerHasTriggerRumble = controllerHasTriggerRumble_;
        info.androidCaps = androidCaps_;
        info.activeOutput = activeOutput_;
        for (const Channel& ch : channels_) {
            if (ch.active) {
                info.activeChannels++;
            }
        }
        info.currentLow = lastSentLow_;
        info.currentHigh = lastSentHigh_;
        info.eventsTriggered = eventsTriggered_;
        return info;
    }

    void rumbleLauncherTest(SDL_Gamepad* pad, float left, float right, int durationMs)
    {
        float l = clamp01(left);
        float r = clamp01(right);
        if (l <= 0.005f && r <= 0.005f) {
            if (pad) {
                SDL_RumbleGamepad(pad, 0, 0, 0);
            }
#ifdef __ANDROID__
            androidStopAll();
#endif
            return;
        }

        bool padSent = false;
        if (pad) {
            SDL_PropertiesID props = SDL_GetGamepadProperties(pad);
            bool hasRumble = props != 0 && SDL_GetBooleanProperty(props, SDL_PROP_GAMEPAD_CAP_RUMBLE_BOOLEAN, false);
            if (hasRumble) {
                padSent = SDL_RumbleGamepad(pad, toUint16(l), toUint16(r), static_cast<Uint32>(durationMs));
            }
        }
#ifdef __ANDROID__
        if (!padSent) {
            if (!androidRumbleGamepadLegacy(l, r, durationMs)) {
                androidRumbleDevice(l, r, durationMs, kPrimNone);
            }
        }
#endif
    }

} // namespace game
