// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>

namespace umi::port::arm {

struct NVIC {
    static constexpr std::uint32_t BASE = 0xE000E100;
    static auto* iser() { return reinterpret_cast<volatile std::uint32_t*>(BASE); }
    static auto* icer() { return reinterpret_cast<volatile std::uint32_t*>(BASE + 0x80); }
    static auto* ispr() { return reinterpret_cast<volatile std::uint32_t*>(BASE + 0x100); }
    static auto* icpr() { return reinterpret_cast<volatile std::uint32_t*>(BASE + 0x180); }
    static auto* ipr()  { return reinterpret_cast<volatile std::uint8_t*>(BASE + 0x300); }
    
    static void enable(std::uint32_t n)  { iser()[n >> 5] = 1u << (n & 31); }
    static void disable(std::uint32_t n) { icer()[n >> 5] = 1u << (n & 31); }
    static void pend(std::uint32_t n)    { ispr()[n >> 5] = 1u << (n & 31); }
    static void unpend(std::uint32_t n)  { icpr()[n >> 5] = 1u << (n & 31); }
    static void set_prio(std::uint32_t n, std::uint8_t p) { ipr()[n] = p; }
};

} // namespace umi::port::arm
