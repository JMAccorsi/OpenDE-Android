#pragma once

#include <functional>
#include <string_view>

namespace dx {
    class UObject;

    struct RumbleHooks {
        std::function<void(UObject* player, float impactSpeedZ)> onPlayerLanded;
        std::function<void(std::string_view soundName, float distance, float radius, bool fromPlayerOrOwned)> onHearSound;
    };

    inline RumbleHooks& rumbleHooks()
    {
        static RumbleHooks hooks;
        return hooks;
    }
}
