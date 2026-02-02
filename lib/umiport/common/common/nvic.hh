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
    static auto* iabr() { return reinterpret_cast<volatile std::uint32_t*>(BASE + 0x200); }
    static auto* ipr()  { return reinterpret_cast<volatile std::uint8_t*>(BASE + 0x300); }
    
    // System handler priority registers (for system exceptions)
    static constexpr std::uint32_t SHPR_BASE = 0xE000ED18;
    static auto* shpr() { return reinterpret_cast<volatile std::uint8_t*>(SHPR_BASE); }
    
    static void enable(std::uint32_t n)  { iser()[n >> 5] = 1u << (n & 31); }
    static void disable(std::uint32_t n) { icer()[n >> 5] = 1u << (n & 31); }
    static void pend(std::uint32_t n)    { ispr()[n >> 5] = 1u << (n & 31); }
    static void unpend(std::uint32_t n)  { icpr()[n >> 5] = 1u << (n & 31); }
    
    /// Alias for unpend
    static void clear_pending(std::uint32_t n) { unpend(n); }
    
    /// Check if interrupt is enabled
    static bool is_enabled(std::uint32_t n) {
        return (iser()[n >> 5] & (1u << (n & 31))) != 0;
    }
    
    /// Check if interrupt is pending
    static bool is_pending(std::uint32_t n) {
        return (ispr()[n >> 5] & (1u << (n & 31))) != 0;
    }
    
    /// Check if interrupt is active
    static bool is_active(std::uint32_t n) {
        return (iabr()[n >> 5] & (1u << (n & 31))) != 0;
    }
    
    /// Set priority for peripheral IRQ (0+) or system exception (negative)
    static void set_prio(int n, std::uint8_t p) {
        if (n >= 0) {
            ipr()[n] = p;
        } else {
            // System exception: SHPR index = exception_number - 4
            // SysTick (-1) -> SHPR[11], PendSV (-2) -> SHPR[10], etc.
            int shpr_idx = 12 + n;  // -1 -> 11, -2 -> 10, -5 -> 7
            if (shpr_idx >= 0 && shpr_idx < 12) {
                shpr()[shpr_idx] = p;
            }
        }
    }
    
    /// Get priority
    static std::uint8_t get_prio(int n) {
        if (n >= 0) {
            return ipr()[n];
        } else {
            int shpr_idx = 12 + n;
            if (shpr_idx >= 0 && shpr_idx < 12) {
                return shpr()[shpr_idx];
            }
            return 0;
        }
    }
};

} // namespace umi::port::arm
