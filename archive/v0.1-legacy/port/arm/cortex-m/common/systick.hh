// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>

namespace umi::port::arm {

struct SysTick {
    static constexpr std::uint32_t BASE = 0xE000E010;
    static constexpr std::uint32_t ENABLE = 1, TICKINT = 2, CLKSRC = 4;
    
    static auto& csr()   { return *reinterpret_cast<volatile std::uint32_t*>(BASE); }
    static auto& rvr()   { return *reinterpret_cast<volatile std::uint32_t*>(BASE + 4); }
    static auto& cvr()   { return *reinterpret_cast<volatile std::uint32_t*>(BASE + 8); }
    
    /// Init with reload value (ticks-1)
    static void init(std::uint32_t reload) {
        rvr() = reload; cvr() = 0;
        csr() = CLKSRC | TICKINT | ENABLE;
    }
    /// Init from frequency and period_us
    static void init_us(std::uint32_t freq_hz, std::uint32_t us) {
        init((freq_hz / 1000000) * us - 1);
    }
    static void disable() { csr() = 0; }
    static std::uint32_t current() { return cvr(); }
};

} // namespace umi::port::arm
