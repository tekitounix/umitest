// SPDX-License-Identifier: MIT
// Cortex-M7 Cache Control
#pragma once

#include <cstdint>

namespace umi::cm7 {

/// Cortex-M7 System Control Block cache registers
namespace scb {
constexpr std::uintptr_t SCB_BASE = 0xE000'ED00;

inline auto* CCSIDR  = reinterpret_cast<volatile std::uint32_t*>(0xE000'ED80);
inline auto* CSSELR  = reinterpret_cast<volatile std::uint32_t*>(0xE000'ED84);
inline auto* CCR     = reinterpret_cast<volatile std::uint32_t*>(SCB_BASE + 0x14);
inline auto* ICIALLU = reinterpret_cast<volatile std::uint32_t*>(0xE000'EF50);
inline auto* DCIMVAC = reinterpret_cast<volatile std::uint32_t*>(0xE000'EF5C);
inline auto* DCISW   = reinterpret_cast<volatile std::uint32_t*>(0xE000'EF60);
inline auto* DCCMVAC = reinterpret_cast<volatile std::uint32_t*>(0xE000'EF68);
inline auto* DCCSW   = reinterpret_cast<volatile std::uint32_t*>(0xE000'EF6C);

constexpr std::uint32_t CCR_IC = 1U << 17;  // I-Cache enable
constexpr std::uint32_t CCR_DC = 1U << 16;  // D-Cache enable
} // namespace scb

/// Enable instruction cache
inline void enable_icache() {
    asm volatile("dsb sy" ::: "memory");
    asm volatile("isb sy" ::: "memory");
    *scb::ICIALLU = 0;  // Invalidate entire I-Cache
    asm volatile("dsb sy" ::: "memory");
    asm volatile("isb sy" ::: "memory");
    *scb::CCR |= scb::CCR_IC;
    asm volatile("dsb sy" ::: "memory");
    asm volatile("isb sy" ::: "memory");
}

/// Enable data cache
inline void enable_dcache() {
    asm volatile("dsb sy" ::: "memory");
    asm volatile("isb sy" ::: "memory");

    // Select Level 1 Data cache
    *scb::CSSELR = 0;
    asm volatile("dsb sy" ::: "memory");

    // Invalidate entire D-Cache by set/way
    auto ccsidr = *scb::CCSIDR;
    auto sets = (ccsidr >> 13) & 0x7FFF;
    auto ways = (ccsidr >> 3) & 0x3FF;

    for (std::uint32_t set = 0; set <= sets; ++set) {
        for (std::uint32_t way = 0; way <= ways; ++way) {
            *scb::DCISW = (set << 5) | (way << 30);
        }
    }
    asm volatile("dsb sy" ::: "memory");

    *scb::CCR |= scb::CCR_DC;
    asm volatile("dsb sy" ::: "memory");
    asm volatile("isb sy" ::: "memory");
}

/// Enable FPU (single + double precision on CM7)
inline void enable_fpu() {
    // CPACR: enable CP10 and CP11 (FPU) full access
    auto* CPACR = reinterpret_cast<volatile std::uint32_t*>(0xE000'ED88);
    *CPACR |= (0xFU << 20);  // CP10 + CP11 full access
    asm volatile("dsb sy" ::: "memory");
    asm volatile("isb sy" ::: "memory");
}

} // namespace umi::cm7
