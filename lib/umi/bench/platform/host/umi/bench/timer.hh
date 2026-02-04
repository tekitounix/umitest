#pragma once

#include <chrono>
#include <cstdint>

namespace umi::bench {

struct HostTimer {
    using Counter = std::uint64_t;

    static void enable() { reset(); }

    static void reset() {
        base() = clock::now();
    }

    static Counter now() {
        const auto elapsed = clock::now() - base();
        return static_cast<Counter>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count()
        );
    }

private:
    using clock = std::chrono::steady_clock;

    static clock::time_point& base() {
        static clock::time_point value = clock::now();
        return value;
    }
};

using TimerImpl = HostTimer;

} // namespace umi::bench
