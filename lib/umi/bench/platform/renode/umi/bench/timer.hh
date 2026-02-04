#pragma once

#include <cstdint>

namespace umi::bench {

struct RenodeTimer {
    using Counter = std::uint32_t;

    static void enable() {
        demcr() |= 1u << 24;
        reset();
        ctrl() |= 1u;
    }

    static void reset() {
        cyccnt() = 0u;
    }

    static Counter now() {
        return cyccnt();
    }

private:
    static constexpr std::uint32_t dwt_base = 0xE0001000;

    static volatile std::uint32_t& ctrl() {
        return *reinterpret_cast<volatile std::uint32_t*>(dwt_base);
    }

    static volatile std::uint32_t& cyccnt() {
        return *reinterpret_cast<volatile std::uint32_t*>(dwt_base + 4u);
    }

    static volatile std::uint32_t& demcr() {
        return *reinterpret_cast<volatile std::uint32_t*>(0xE000EDFC);
    }
};

using TimerImpl = RenodeTimer;

} // namespace umi::bench
