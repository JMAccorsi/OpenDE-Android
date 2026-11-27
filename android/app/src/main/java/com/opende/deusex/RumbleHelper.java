package com.opende.deusex;

import android.content.Context;
import android.media.AudioAttributes;
import android.os.Build;
import android.os.VibrationAttributes;
import android.os.VibrationEffect;
import android.os.Vibrator;
import android.os.VibratorManager;
import android.view.InputDevice;

final class RumbleHelper {
    static final int CAP_DEVICE_VIBRATOR = 1;
    static final int CAP_AMPLITUDE_CONTROL = 2;
    static final int CAP_PRIMITIVES = 4;
    static final int CAP_INPUT_DEVICE_VIBRATOR = 8;

    static final int PRIM_NONE = 0;
    static final int PRIM_TICK = 1;
    static final int PRIM_CLICK = 2;
    static final int PRIM_THUD = 3;

    private static volatile Vibrator sDeviceVibrator;
    private static volatile boolean sInitialized = false;
    private static volatile boolean sHasDevice = false;
    private static volatile boolean sHasAmplitude = false;
    private static volatile boolean sHasTick = false;
    private static volatile boolean sHasClick = false;
    private static volatile boolean sHasThud = false;

    private RumbleHelper() {}

    @SuppressWarnings("deprecation")
    private static void ensureInit(Context ctx) {
        if (sInitialized || ctx == null) {
            return;
        }
        synchronized (RumbleHelper.class) {
            if (sInitialized) {
                return;
            }
            Context app = ctx.getApplicationContext();
            Vibrator vib = null;
            try {
                if (Build.VERSION.SDK_INT >= 31) {
                    VibratorManager vm = (VibratorManager) app.getSystemService(Context.VIBRATOR_MANAGER_SERVICE);
                    if (vm != null) {
                        vib = vm.getDefaultVibrator();
                    }
                }
                if (vib == null) {
                    vib = (Vibrator) app.getSystemService(Context.VIBRATOR_SERVICE);
                }
            } catch (Throwable ignored) {
                vib = null;
            }

            if (vib != null && vib.hasVibrator()) {
                sDeviceVibrator = vib;
                sHasDevice = true;
                try {
                    sHasAmplitude = vib.hasAmplitudeControl();
                } catch (Throwable ignored) {
                    sHasAmplitude = false;
                }
                if (Build.VERSION.SDK_INT >= 30) {
                    try {
                        sHasTick = vib.areAllPrimitivesSupported(VibrationEffect.Composition.PRIMITIVE_LOW_TICK)
                            || vib.areAllPrimitivesSupported(VibrationEffect.Composition.PRIMITIVE_TICK);
                        sHasClick = vib.areAllPrimitivesSupported(VibrationEffect.Composition.PRIMITIVE_CLICK);
                        if (Build.VERSION.SDK_INT >= 31) {
                            sHasThud = vib.areAllPrimitivesSupported(VibrationEffect.Composition.PRIMITIVE_THUD);
                        }
                    } catch (Throwable ignored) {
                        sHasTick = false;
                        sHasClick = false;
                        sHasThud = false;
                    }
                }
            }
            sInitialized = true;
        }
    }

    @SuppressWarnings("deprecation")
    private static Vibrator findLegacyGamepadVibrator() {
        try {
            int[] ids = InputDevice.getDeviceIds();
            for (int id : ids) {
                InputDevice dev = InputDevice.getDevice(id);
                if (dev == null) continue;
                int sources = dev.getSources();
                boolean isPad = (sources & InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD
                    || (sources & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK;
                if (!isPad) continue;
                Vibrator v = dev.getVibrator();
                if (v != null && v.hasVibrator()) {
                    return v;
                }
            }
        } catch (Throwable ignored) {
        }
        return null;
    }

    static int getCapabilities(Context ctx) {
        ensureInit(ctx);
        int caps = 0;
        if (sHasDevice) caps |= CAP_DEVICE_VIBRATOR;
        if (sHasAmplitude) caps |= CAP_AMPLITUDE_CONTROL;
        if (sHasTick || sHasClick || sHasThud) caps |= CAP_PRIMITIVES;
        if (findLegacyGamepadVibrator() != null) caps |= CAP_INPUT_DEVICE_VIBRATOR;
        return caps;
    }

    @SuppressWarnings("deprecation")
    private static void vibrateWithAttributes(Vibrator vib, VibrationEffect effect) {
        if (Build.VERSION.SDK_INT >= 33) {
            VibrationAttributes attrs = new VibrationAttributes.Builder()
                .setUsage(VibrationAttributes.USAGE_MEDIA)
                .build();
            vib.vibrate(effect, attrs);
        } else {
            AudioAttributes attrs = new AudioAttributes.Builder()
                .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION)
                .setUsage(AudioAttributes.USAGE_GAME)
                .build();
            vib.vibrate(effect, attrs);
        }
    }

    static void rumbleDevice(Context ctx, float low, float high, int durationMs, int primitiveHint) {
        ensureInit(ctx);
        Vibrator vib = sDeviceVibrator;
        if (vib == null || !sHasDevice) {
            return;
        }

        float l = Math.max(0.0f, Math.min(1.0f, low));
        float h = Math.max(0.0f, Math.min(1.0f, high));
        float combined = Math.max(0.0f, Math.min(1.0f, l * 0.65f + h * 0.35f));
        float peak = Math.max(l, h);
        float intensity = Math.max(combined, peak * 0.85f);

        if (intensity <= 0.005f || durationMs <= 0) {
            try {
                vib.cancel();
            } catch (Throwable ignored) {
            }
            return;
        }

        try {
            // Use crisp HD haptic primitives on supported LRA motors (API 30+) for short tactile ticks/clicks.
            if (Build.VERSION.SDK_INT >= 30 && primitiveHint != PRIM_NONE && durationMs <= 65) {
                int primId = -1;
                if (primitiveHint == PRIM_TICK && sHasTick) {
                    primId = VibrationEffect.Composition.PRIMITIVE_TICK;
                } else if (primitiveHint == PRIM_CLICK && sHasClick) {
                    primId = VibrationEffect.Composition.PRIMITIVE_CLICK;
                } else if (primitiveHint == PRIM_THUD && sHasThud && Build.VERSION.SDK_INT >= 31) {
                    primId = VibrationEffect.Composition.PRIMITIVE_THUD;
                }
                if (primId >= 0) {
                    float scale = Math.max(0.18f, Math.min(1.0f, intensity));
                    VibrationEffect comp = VibrationEffect.startComposition()
                        .addPrimitive(primId, scale)
                        .compose();
                    vibrateWithAttributes(vib, comp);
                    return;
                }
            }

            if (sHasAmplitude) {
                int amp = Math.round(intensity * 255.0f);
                if (amp < 1) amp = 1;
                if (amp > 255) amp = 255;
                int dur = Math.max(12, Math.min(durationMs, 1000));
                VibrationEffect effect = VibrationEffect.createOneShot(dur, amp);
                vibrateWithAttributes(vib, effect);
            } else {
                // ERM fallback (no amplitude control, e.g. Retroid Pocket / budget phones):
                // ERM motors require ~28-35ms just to spin up from rest, so very short pulses (<25ms)
                // feel invisible. Scale duration cleanly so light taps, gunshots, and explosions all feel distinct.
                if (intensity < 0.08f) {
                    return;
                }
                int minDur = 42;
                int maxDur = 280;
                if (primitiveHint == PRIM_TICK) {
                    minDur = 26;
                    maxDur = 36;
                } else if (primitiveHint == PRIM_CLICK) {
                    minDur = 34;
                    maxDur = 52;
                } else if (primitiveHint == PRIM_THUD) {
                    minDur = 55;
                    maxDur = 140;
                }
                int dur = Math.round(durationMs * (0.55f + 0.65f * intensity));
                dur = Math.max(minDur, Math.min(dur, maxDur));
                VibrationEffect effect = VibrationEffect.createOneShot(dur, VibrationEffect.DEFAULT_AMPLITUDE);
                vibrateWithAttributes(vib, effect);
            }
        } catch (Throwable ignored) {
        }
    }

    @SuppressWarnings("deprecation")
    static boolean rumbleGamepadLegacy(float low, float high, int durationMs) {
        Vibrator vib = findLegacyGamepadVibrator();
        if (vib == null) {
            return false;
        }
        float l = Math.max(0.0f, Math.min(1.0f, low));
        float h = Math.max(0.0f, Math.min(1.0f, high));
        float intensity = Math.max(0.0f, Math.min(1.0f, l * 0.65f + h * 0.35f));
        if (intensity <= 0.005f || durationMs <= 0) {
            try {
                vib.cancel();
            } catch (Throwable ignored) {
            }
            return true;
        }
        try {
            int dur = Math.max(12, Math.min(durationMs, 1000));
            if (vib.hasAmplitudeControl()) {
                int amp = Math.max(1, Math.min(255, Math.round(intensity * 255.0f)));
                vib.vibrate(VibrationEffect.createOneShot(dur, amp));
            } else {
                int shapedDur = Math.max(12, Math.min(220, Math.round(dur * (0.25f + 0.75f * intensity))));
                vib.vibrate(VibrationEffect.createOneShot(shapedDur, VibrationEffect.DEFAULT_AMPLITUDE));
            }
            return true;
        } catch (Throwable ignored) {
            return false;
        }
    }

    @SuppressWarnings("deprecation")
    static void stopAll(Context ctx) {
        ensureInit(ctx);
        if (sDeviceVibrator != null) {
            try {
                sDeviceVibrator.cancel();
            } catch (Throwable ignored) {
            }
        }
        try {
            int[] ids = InputDevice.getDeviceIds();
            for (int id : ids) {
                InputDevice dev = InputDevice.getDevice(id);
                if (dev == null) continue;
                int sources = dev.getSources();
                if ((sources & InputDevice.SOURCE_GAMEPAD) != 0 || (sources & InputDevice.SOURCE_JOYSTICK) != 0) {
                    Vibrator v = dev.getVibrator();
                    if (v != null && v.hasVibrator()) {
                        v.cancel();
                    }
                }
            }
        } catch (Throwable ignored) {
        }
    }
}
