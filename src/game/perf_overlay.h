#pragma once

#include <array>
#include <cstdint>

namespace dx {
    class Canvas2D;
}

namespace game {
    enum class PerfOverlayMode : int {
        Off = 0,
        Compact = 1,
        Full = 2,
    };

    struct PerfOverlaySample {
        float frameMs = 0.0f;
        float tickMs = 0.0f;
        float renderMs = 0.0f;
        float swapMs = 0.0f;
        float tickActorMs = 0.0f;
        float tickAiMs = 0.0f;
        int tickedActors = 0;
        float lightsMs = 0.0f;
        float occludeMs = 0.0f;
        float skyMs = 0.0f;
        float bspMs = 0.0f;
        float actorsMs = 0.0f;
        float actEmitMs = 0.0f;
        float actFlushMs = 0.0f;
        float ssaoMs = 0.0f;
        float fxaaMs = 0.0f;
        int actActors = 0;
        int actBatches = 0;
        int drawCalls = 0;
        int triangles = 0;
        int meshFast = 0;
        int meshFull = 0;
        int meshRebuild = 0;
        int width = 0;
        int height = 0;
    };

    class PerfOverlay {
    public:
        void setMode(PerfOverlayMode newMode);
        PerfOverlayMode mode() const { return currentMode; }
        void pushSample(const PerfOverlaySample& sample);
        void render(dx::Canvas2D& canvas, int screenWidth, int screenHeight) const;

    private:
        static constexpr size_t kHistorySize = 120;

        PerfOverlayMode currentMode = PerfOverlayMode::Off;
        std::array<float, kHistorySize> frameHistory {};
        size_t historyHead = 0;
        size_t historyCount = 0;
        float accumTimeMs = 0.0f;

        float dispFps = 0.0f;
        float dispFrameMs = 0.0f;
        float dispLow1Fps = 0.0f;
        PerfOverlaySample dispSample {};
    };
}
