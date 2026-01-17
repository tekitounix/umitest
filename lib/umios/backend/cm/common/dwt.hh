// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>

namespace umi::port::arm {

struct DWT {
    static constexpr std::uint32_t BASE = 0xE0001000;
    static auto& ctrl()   { return *reinterpret_cast<volatile std::uint32_t*>(BASE); }
    static auto& cyccnt() { return *reinterpret_cast<volatile std::uint32_t*>(BASE + 4); }
    static auto& demcr()  { return *reinterpret_cast<volatile std::uint32_t*>(0xE000EDFC); }
    
    static void enable()  { demcr() |= 1u << 24; cyccnt() = 0; ctrl() |= 1; }
    static void disable() { ctrl() &= ~1u; }
    static std::uint32_t cycles() { return cyccnt(); }
    static void reset() { cyccnt() = 0; }
};

} // namespace umi::port::arm
