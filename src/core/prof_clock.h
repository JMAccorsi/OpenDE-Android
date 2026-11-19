#pragma once

#include <chrono>

#ifdef __ANDROID__
#include <dlfcn.h>
#endif

namespace dx {
    // Milliseconds since process start. Keeping the origin near zero avoids the
    // float precision loss of steady_clock::time_since_epoch() (uptime in ms
    // stored in a float quantizes to 0.125-0.25 ms on devices up for days).
    inline float profNowMs()
    {
        static const std::chrono::steady_clock::time_point base = std::chrono::steady_clock::now();
        return std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - base).count();
    }

    struct ScopedTrace {
#ifdef __ANDROID__
        using BeginFn = void (*)(const char*);
        using EndFn = void (*)();

        static BeginFn beginFn()
        {
            static BeginFn fn = reinterpret_cast<BeginFn>(dlsym(RTLD_DEFAULT, "ATrace_beginSection"));
            return fn;
        }

        static EndFn endFn()
        {
            static EndFn fn = reinterpret_cast<EndFn>(dlsym(RTLD_DEFAULT, "ATrace_endSection"));
            return fn;
        }

        bool active = false;

        explicit ScopedTrace(const char* name)
        {
            if (BeginFn fn = beginFn()) {
                fn(name);
                active = true;
            }
        }

        ~ScopedTrace()
        {
            if (active) {
                if (EndFn fn = endFn()) {
                    fn();
                }
            }
        }
#else
        explicit ScopedTrace(const char*) {}
        ~ScopedTrace() = default;
#endif
        ScopedTrace(const ScopedTrace&) = delete;
        ScopedTrace& operator=(const ScopedTrace&) = delete;
    };
}

#define DX_TRACE_CONCAT_INNER(a, b) a##b
#define DX_TRACE_CONCAT(a, b) DX_TRACE_CONCAT_INNER(a, b)
#ifdef __ANDROID__
#define DX_TRACE_SCOPE(name) ::dx::ScopedTrace DX_TRACE_CONCAT(dxTraceScope_, __LINE__)(name)
#else
#define DX_TRACE_SCOPE(name) static_cast<void>(0)
#endif
