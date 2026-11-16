#pragma once

#include <chrono>

namespace dx {
    // Milliseconds since process start. Keeping the origin near zero avoids the
    // float precision loss of steady_clock::time_since_epoch() (uptime in ms
    // stored in a float quantizes to 0.125-0.25 ms on devices up for days).
    inline float profNowMs()
    {
        static const std::chrono::steady_clock::time_point base = std::chrono::steady_clock::now();
        return std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - base).count();
    }
}
