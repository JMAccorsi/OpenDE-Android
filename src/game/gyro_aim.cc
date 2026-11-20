#include "game/gyro_aim.h"

#include <algorithm>
#include <array>
#include <cmath>

#ifdef __ANDROID__
#include <android/looper.h>
#include <android/sensor.h>
#include <jni.h>
#endif

namespace game {

    namespace {
        constexpr int kAndroidGyroDelayUs = 5500;     // ~182 Hz (below Android 12+ 200 Hz restricted threshold)
        constexpr int kAndroidGravityDelayUs = 20000; // ~50 Hz
        constexpr float kMaxSampleDt = 0.05f;
        constexpr float kMaxFrameRad = 1.5f;

        void rotateNaturalToScreen(int rotation, float nx, float ny, float nz, float& sx, float& sy, float& sz)
        {
            // Matches Android Surface.ROTATION_* -> current screen coords (+X right, +Y up, +Z toward viewer).
            switch (rotation & 3) {
            case 1: // Surface.ROTATION_90 (LandscapeLeft on portrait-natural screens)
                sx = -ny;
                sy = nx;
                sz = nz;
                break;
            case 2: // Surface.ROTATION_180
                sx = -nx;
                sy = -ny;
                sz = nz;
                break;
            case 3: // Surface.ROTATION_270 (LandscapeRight on portrait-natural screens)
                sx = ny;
                sy = -nx;
                sz = nz;
                break;
            case 0:
            default:
                sx = nx;
                sy = ny;
                sz = nz;
                break;
            }
        }
    } // namespace

    GyroAim::~GyroAim()
    {
        shutdown();
    }

    void GyroAim::initialize()
    {
        if (initialized_) {
            return;
        }
        initialized_ = true;
        refreshDisplayRotation();

#ifdef __ANDROID__
        sensorManager_ = ASensorManager_getInstanceForPackage("com.opende.deusex");
        if (!sensorManager_) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "OpenDE_Gyro: ASensorManager_getInstanceForPackage returned null");
        } else {
            androidGyro_ = ASensorManager_getDefaultSensorEx(sensorManager_, ASENSOR_TYPE_GYROSCOPE, false);
            if (!androidGyro_) {
                androidGyro_ = ASensorManager_getDefaultSensor(sensorManager_, ASENSOR_TYPE_GYROSCOPE);
            }
            androidGravity_ = ASensorManager_getDefaultSensorEx(sensorManager_, ASENSOR_TYPE_GRAVITY, false);
            if (!androidGravity_) {
                androidGravity_ = ASensorManager_getDefaultSensor(sensorManager_, ASENSOR_TYPE_GRAVITY);
            }
            if (!androidGravity_) {
                androidGravity_ = ASensorManager_getDefaultSensorEx(sensorManager_, ASENSOR_TYPE_ACCELEROMETER, false);
                if (!androidGravity_) {
                    androidGravity_ = ASensorManager_getDefaultSensor(sensorManager_, ASENSOR_TYPE_ACCELEROMETER);
                }
                androidGravityIsAccel_ = androidGravity_ != nullptr;
            }
            if (androidGyro_) {
                ALooper* looper = ALooper_forThread();
                if (!looper) {
                    looper = ALooper_prepare(ALOOPER_PREPARE_ALLOW_NON_CALLBACKS);
                }
                if (looper) {
                    androidQueue_ = ASensorManager_createEventQueue(sensorManager_, looper, 3, nullptr, nullptr);
                }
            }
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "OpenDE_Gyro: init android gyro='%s' (minDelay=%dus) grav='%s' (accelFallback=%d) rot=%d queue=%p",
                androidGyro_ ? ASensor_getName(androidGyro_) : "none",
                androidGyro_ ? ASensor_getMinDelay(androidGyro_) : -1,
                androidGravity_ ? ASensor_getName(androidGravity_) : "none",
                androidGravityIsAccel_ ? 1 : 0,
                displayRotation_,
                static_cast<const void*>(androidQueue_));
        }
#else
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "OpenDE_Gyro: init desktop controller backend");
#endif

        updateActiveState();
    }

    void GyroAim::shutdown()
    {
        if (!initialized_) {
            return;
        }
        if (controller_) {
            if (controllerHasGyro_) {
                SDL_GameControllerSetSensorEnabled(controller_, SDL_SENSOR_GYRO, SDL_FALSE);
            }
            if (controllerHasAccel_) {
                SDL_GameControllerSetSensorEnabled(controller_, SDL_SENSOR_ACCEL, SDL_FALSE);
            }
            controllerSensorsEnabled_ = false;
            controller_ = nullptr;
            controllerHasGyro_ = false;
            controllerHasAccel_ = false;
        }
#ifdef __ANDROID__
        if (androidQueue_) {
            if (androidSensorsEnabled_) {
                if (androidGyro_) {
                    ASensorEventQueue_disableSensor(androidQueue_, androidGyro_);
                }
                if (androidGravity_) {
                    ASensorEventQueue_disableSensor(androidQueue_, androidGravity_);
                }
                androidSensorsEnabled_ = false;
            }
            if (sensorManager_) {
                ASensorManager_destroyEventQueue(sensorManager_, androidQueue_);
            }
            androidQueue_ = nullptr;
        }
        androidGyro_ = nullptr;
        androidGravity_ = nullptr;
        sensorManager_ = nullptr;
#endif
        initialized_ = false;
    }

    void GyroAim::setSettings(const GyroSettings& settings)
    {
        settings_ = settings;
        settings_.sensitivity = std::clamp(settings_.sensitivity, 0.1f, 10.0f);
        updateActiveState();
    }

    void GyroAim::setSuspended(bool suspended)
    {
        if (suspended_ == suspended) {
            return;
        }
        suspended_ = suspended;
        updateActiveState();
    }

    void GyroAim::setController(SDL_GameController* controller)
    {
        if (controller_ == controller) {
            return;
        }
        if (controller_ && controllerSensorsEnabled_) {
            if (controllerHasGyro_) {
                SDL_GameControllerSetSensorEnabled(controller_, SDL_SENSOR_GYRO, SDL_FALSE);
            }
            if (controllerHasAccel_) {
                SDL_GameControllerSetSensorEnabled(controller_, SDL_SENSOR_ACCEL, SDL_FALSE);
            }
        }
        controllerSensorsEnabled_ = false;
        controller_ = controller;
        controllerHasGyro_ = controller && SDL_GameControllerHasSensor(controller, SDL_SENSOR_GYRO) == SDL_TRUE;
        controllerHasAccel_ = controller && SDL_GameControllerHasSensor(controller, SDL_SENSOR_ACCEL) == SDL_TRUE;
        if (controller_) {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "OpenDE_Gyro: controller '%s' hasGyro=%d hasAccel=%d",
                SDL_GameControllerName(controller_) ? SDL_GameControllerName(controller_) : "unknown",
                controllerHasGyro_ ? 1 : 0,
                controllerHasAccel_ ? 1 : 0);
        }
        resetMotionState();
        updateActiveState();
    }

    void GyroAim::refreshDisplayRotation()
    {
#ifdef __ANDROID__
        JNIEnv* env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
        jobject activity = static_cast<jobject>(SDL_AndroidGetActivity());
        if (env && activity) {
            jclass cls = env->GetObjectClass(activity);
            if (cls) {
                jmethodID mid = env->GetStaticMethodID(cls, "getDisplayRotation", "()I");
                if (mid) {
                    jint rot = env->CallStaticIntMethod(cls, mid);
                    if (!env->ExceptionCheck()) {
                        displayRotation_ = static_cast<int>(rot) & 3;
                    }
                }
                if (env->ExceptionCheck()) {
                    env->ExceptionClear();
                }
                env->DeleteLocalRef(cls);
            }
            env->DeleteLocalRef(activity);
        }
#else
        displayRotation_ = 0;
#endif
    }

    void GyroAim::resetMotionState()
    {
        prevCtrlTsUs_ = 0;
        prevCtrlPerfCounter_ = 0;
        pendingCtrlYawRad_ = 0.0f;
        pendingCtrlPitchRad_ = 0.0f;
#ifdef __ANDROID__
        prevAndroidGyroTsNs_ = 0;
#endif
        hasPrevRawGyro_ = false;
        calmSeconds_ = 0.0f;
        bias_[0] = 0.0f;
        bias_[1] = 0.0f;
        bias_[2] = 0.0f;
        lastScreenGyro_[0] = 0.0f;
        lastScreenGyro_[1] = 0.0f;
        lastScreenGyro_[2] = 0.0f;
        hasGravitySample_ = false;
        up_[0] = 0.0f;
        up_[1] = 1.0f;
        up_[2] = 0.0f;
        lastOutput_ = {};
    }

    void GyroAim::updateActiveState()
    {
        if (!initialized_) {
            return;
        }
        const bool wantActive = settings_.mode != GyroAimMode::Off && !suspended_;
        const bool wantController = wantActive && controller_ && controllerHasGyro_;

        if (controllerSensorsEnabled_ != wantController) {
            if (controller_) {
                if (controllerHasGyro_) {
                    SDL_GameControllerSetSensorEnabled(controller_, SDL_SENSOR_GYRO, wantController ? SDL_TRUE : SDL_FALSE);
                }
                if (controllerHasAccel_) {
                    SDL_GameControllerSetSensorEnabled(controller_, SDL_SENSOR_ACCEL, wantController ? SDL_TRUE : SDL_FALSE);
                }
            }
            controllerSensorsEnabled_ = wantController;
            prevCtrlTsUs_ = 0;
            prevCtrlPerfCounter_ = 0;
            pendingCtrlYawRad_ = 0.0f;
            pendingCtrlPitchRad_ = 0.0f;
        }

#ifdef __ANDROID__
        const bool wantAndroid = wantActive && !wantController && androidQueue_ && androidGyro_;
        if (androidSensorsEnabled_ != wantAndroid) {
            if (androidQueue_ && androidGyro_) {
                if (wantAndroid) {
                    int gyroRet = ASensorEventQueue_enableSensor(androidQueue_, androidGyro_);
                    int gyroMin = ASensor_getMinDelay(androidGyro_);
                    int gyroDelay = gyroMin > 0 ? std::max(kAndroidGyroDelayUs, gyroMin) : kAndroidGyroDelayUs;
                    int gyroRateRet = ASensorEventQueue_setEventRate(androidQueue_, androidGyro_, gyroDelay);
                    if (androidGravity_) {
                        ASensorEventQueue_enableSensor(androidQueue_, androidGravity_);
                        int gravMin = ASensor_getMinDelay(androidGravity_);
                        int gravDelay = gravMin > 0 ? std::max(kAndroidGravityDelayUs, gravMin) : kAndroidGravityDelayUs;
                        ASensorEventQueue_setEventRate(androidQueue_, androidGravity_, gravDelay);
                    }
                    // Drain any stale queued events and reset delta timestamp cleanly.
                    std::array<ASensorEvent, 32> stale {};
                    while (ASensorEventQueue_getEvents(androidQueue_, stale.data(), stale.size()) > 0) {
                    }
                    prevAndroidGyroTsNs_ = 0;
                    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "OpenDE_Gyro: android sensors ENABLED delayUs=%d enableRet=%d rateRet=%d rot=%d",
                        gyroDelay, gyroRet, gyroRateRet, displayRotation_);
                } else {
                    ASensorEventQueue_disableSensor(androidQueue_, androidGyro_);
                    if (androidGravity_) {
                        ASensorEventQueue_disableSensor(androidQueue_, androidGravity_);
                    }
                    prevAndroidGyroTsNs_ = 0;
                    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "OpenDE_Gyro: android sensors DISABLED");
                }
            }
            androidSensorsEnabled_ = wantAndroid;
        }
#endif
    }

    void GyroAim::processGravitySample(float sx, float sy, float sz, float alpha)
    {
        float len = std::sqrt(sx * sx + sy * sy + sz * sz);
        if (len < 2.0f) {
            return;
        }
        float invLen = 1.0f / len;
        float gx = sx * invLen;
        float gy = sy * invLen;
        float gz = sz * invLen;
        if (!hasGravitySample_) {
            up_[0] = gx;
            up_[1] = gy;
            up_[2] = gz;
            hasGravitySample_ = true;
        } else {
            up_[0] += (gx - up_[0]) * alpha;
            up_[1] += (gy - up_[1]) * alpha;
            up_[2] += (gz - up_[2]) * alpha;
            float ulen = std::sqrt(up_[0] * up_[0] + up_[1] * up_[1] + up_[2] * up_[2]);
            if (ulen > 0.001f) {
                float invU = 1.0f / ulen;
                up_[0] *= invU;
                up_[1] *= invU;
                up_[2] *= invU;
            }
        }
        ++gravitySamples_;
    }

    void GyroAim::processGyroSample(float nx, float ny, float nz, int rotation, float dt, bool allowOutput, float& accumYaw, float& accumPitch)
    {
        ++gyroSamples_;
        if (!std::isfinite(nx) || !std::isfinite(ny) || !std::isfinite(nz)) {
            return;
        }

        // Track hardware MEMS zero-rate bias in the chip's native axes when held very still.
        if (dt > 0.0f) {
            float absMag = std::max({ std::fabs(nx), std::fabs(ny), std::fabs(nz) });
            float deltaMag = hasPrevRawGyro_
                ? std::max({ std::fabs(nx - prevRawGyro_[0]), std::fabs(ny - prevRawGyro_[1]), std::fabs(nz - prevRawGyro_[2]) })
                : 0.0f;
            if (hasPrevRawGyro_ && absMag < 0.025f && deltaMag < 0.010f) {
                calmSeconds_ += dt;
                if (calmSeconds_ > 1.2f) {
                    const float blend = std::min(0.05f, dt * 1.5f);
                    bias_[0] = std::clamp(bias_[0] + (nx - bias_[0]) * blend, -0.025f, 0.025f);
                    bias_[1] = std::clamp(bias_[1] + (ny - bias_[1]) * blend, -0.025f, 0.025f);
                    bias_[2] = std::clamp(bias_[2] + (nz - bias_[2]) * blend, -0.025f, 0.025f);
                }
            } else {
                calmSeconds_ = 0.0f;
            }
        }
        prevRawGyro_[0] = nx;
        prevRawGyro_[1] = ny;
        prevRawGyro_[2] = nz;
        hasPrevRawGyro_ = true;

        float sx = 0.0f;
        float sy = 0.0f;
        float sz = 0.0f;
        rotateNaturalToScreen(rotation, nx - bias_[0], ny - bias_[1], nz - bias_[2], sx, sy, sz);
        lastScreenGyro_[0] = sx;
        lastScreenGyro_[1] = sy;
        lastScreenGyro_[2] = sz;

        if (!allowOutput || dt <= 0.0f) {
            return;
        }

        float pitchRate = sx;
        float yawRate = 0.0f;
        switch (settings_.axis) {
        case GyroAxisMode::Yaw:
            yawRate = -sy;
            break;
        case GyroAxisMode::Roll:
            yawRate = -sz;
            break;
        case GyroAxisMode::AutoGravity:
        default:
            if (hasGravitySample_) {
                // Blend local Yaw (-sy) and Roll (-sz) according to the screen's gravity projection
                // so lateral bank angle never couples pitch motion into horizontal turning.
                float yzLen = std::hypot(up_[1], up_[2]);
                if (yzLen > 0.25f) {
                    float uy = up_[1] / yzLen;
                    float uz = up_[2] / yzLen;
                    yawRate = -(sy * uy + sz * uz);
                } else {
                    yawRate = -(sx * up_[0] + sy * up_[1] + sz * up_[2]);
                }
            } else {
                yawRate = -sy;
            }
            break;
        }

        // Smooth radial tightening: suppresses tiny handheld tremor while preserving slow micro-adjustments.
        float speed = std::hypot(yawRate, pitchRate);
        constexpr float kNoiseFloor = 0.012f;
        constexpr float kFullResponse = 0.060f;
        float weight = 0.0f;
        if (speed >= kFullResponse) {
            weight = 1.0f;
        } else if (speed > kNoiseFloor) {
            float t = (speed - kNoiseFloor) / (kFullResponse - kNoiseFloor);
            weight = t * t * (3.0f - 2.0f * t);
        }
        if (weight <= 0.0f) {
            return;
        }

        if (settings_.invertY) {
            pitchRate = -pitchRate;
        }
        float scale = weight * settings_.sensitivity * dt;
        accumYaw += yawRate * scale;
        accumPitch += pitchRate * scale;
    }

    void GyroAim::handleControllerSensorEvent(const SDL_ControllerSensorEvent& ev)
    {
        if (!controllerSensorsEnabled_) {
            return;
        }
        if (ev.sensor == SDL_SENSOR_ACCEL) {
            processGravitySample(ev.data[0], ev.data[1], ev.data[2], 0.06f);
            return;
        }
        if (ev.sensor != SDL_SENSOR_GYRO) {
            return;
        }

        float dt = 0.0f;
        if (ev.timestamp_us > 0) {
            if (prevCtrlTsUs_ > 0 && ev.timestamp_us > prevCtrlTsUs_) {
                dt = static_cast<float>(static_cast<double>(ev.timestamp_us - prevCtrlTsUs_) * 1e-6);
            }
            prevCtrlTsUs_ = ev.timestamp_us;
        } else {
            uint64_t now = SDL_GetPerformanceCounter();
            if (prevCtrlPerfCounter_ > 0 && now > prevCtrlPerfCounter_) {
                dt = static_cast<float>(now - prevCtrlPerfCounter_) / static_cast<float>(SDL_GetPerformanceFrequency());
            }
            prevCtrlPerfCounter_ = now;
        }
        dt = std::clamp(dt, 0.0f, kMaxSampleDt);

        processGyroSample(ev.data[0], ev.data[1], ev.data[2], 0, dt, true, pendingCtrlYawRad_, pendingCtrlPitchRad_);
        pendingCtrlYawRad_ = std::clamp(pendingCtrlYawRad_, -kMaxFrameRad, kMaxFrameRad);
        pendingCtrlPitchRad_ = std::clamp(pendingCtrlPitchRad_, -kMaxFrameRad, kMaxFrameRad);
    }

    GyroDelta GyroAim::poll(bool allowOutput)
    {
        GyroDelta result {};
        if (!initialized_ || settings_.mode == GyroAimMode::Off || suspended_) {
            pendingCtrlYawRad_ = 0.0f;
            pendingCtrlPitchRad_ = 0.0f;
            lastOutput_ = {};
            return result;
        }

        if (controllerSensorsEnabled_) {
            if (allowOutput) {
                result.yawRad = std::clamp(pendingCtrlYawRad_, -kMaxFrameRad, kMaxFrameRad);
                result.pitchRad = std::clamp(pendingCtrlPitchRad_, -kMaxFrameRad, kMaxFrameRad);
            }
            pendingCtrlYawRad_ = 0.0f;
            pendingCtrlPitchRad_ = 0.0f;
            lastOutput_ = result;
            return result;
        }

#ifdef __ANDROID__
        if (androidSensorsEnabled_ && androidQueue_) {
            float accumYaw = 0.0f;
            float accumPitch = 0.0f;
            std::array<ASensorEvent, 64> events {};
            for (int iter = 0; iter < 8; ++iter) {
                ssize_t count = ASensorEventQueue_getEvents(androidQueue_, events.data(), events.size());
                if (count <= 0) {
                    break;
                }
                for (ssize_t i = 0; i < count; ++i) {
                    const ASensorEvent& ev = events[static_cast<size_t>(i)];
                    if (ev.type == ASENSOR_TYPE_GRAVITY || ev.type == ASENSOR_TYPE_ACCELEROMETER) {
                        float sx = 0.0f;
                        float sy = 0.0f;
                        float sz = 0.0f;
                        rotateNaturalToScreen(displayRotation_, ev.acceleration.x, ev.acceleration.y, ev.acceleration.z, sx, sy, sz);
                        float alpha = (ev.type == ASENSOR_TYPE_GRAVITY) ? 0.25f : 0.06f;
                        processGravitySample(sx, sy, sz, alpha);
                    } else if (ev.type == ASENSOR_TYPE_GYROSCOPE) {
                        float dt = 0.0f;
                        if (prevAndroidGyroTsNs_ > 0 && ev.timestamp > prevAndroidGyroTsNs_) {
                            dt = static_cast<float>(static_cast<double>(ev.timestamp - prevAndroidGyroTsNs_) * 1e-9);
                        }
                        prevAndroidGyroTsNs_ = ev.timestamp;
                        dt = std::clamp(dt, 0.0f, kMaxSampleDt);
                        processGyroSample(ev.gyro.x, ev.gyro.y, ev.gyro.z, displayRotation_, dt, allowOutput, accumYaw, accumPitch);
                    }
                }
            }
            if (allowOutput) {
                result.yawRad = std::clamp(accumYaw, -kMaxFrameRad, kMaxFrameRad);
                result.pitchRad = std::clamp(accumPitch, -kMaxFrameRad, kMaxFrameRad);
            }
        }
#else
        (void)allowOutput;
#endif
        lastOutput_ = result;
        return result;
    }

    GyroDebugInfo GyroAim::debugInfo() const
    {
        GyroDebugInfo info;
        info.displayRotation = displayRotation_;
        info.hasGravitySample = hasGravitySample_;
        info.gyroSamples = gyroSamples_;
        info.gravitySamples = gravitySamples_;
        for (int i = 0; i < 3; ++i) {
            info.lastScreenGyro[i] = lastScreenGyro_[i];
            info.lastBias[i] = bias_[i];
            info.upVec[i] = up_[i];
        }
        info.lastOutputYawRad = lastOutput_.yawRad;
        info.lastOutputPitchRad = lastOutput_.pitchRad;

        if (controllerSensorsEnabled_ && controller_) {
            info.backend = "Controller";
            info.enabled = true;
            const char* cname = SDL_GameControllerName(controller_);
            info.gyroSensorName = cname ? cname : "Controller";
            info.gravitySensorName = controllerHasAccel_ ? "ControllerAccel" : "None";
            return info;
        }
#ifdef __ANDROID__
        if (androidGyro_) {
            info.backend = "Android";
            info.enabled = androidSensorsEnabled_;
            const char* gname = ASensor_getName(androidGyro_);
            info.gyroSensorName = gname ? gname : "AndroidGyro";
            if (androidGravity_) {
                const char* grName = ASensor_getName(androidGravity_);
                info.gravitySensorName = grName ? grName : "AndroidGravity";
            } else {
                info.gravitySensorName = "None";
            }
            info.usingGravityFallbackAccel = androidGravityIsAccel_;
            return info;
        }
#endif
        info.backend = "None";
        info.enabled = false;
        return info;
    }

} // namespace game
