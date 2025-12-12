// SPDX-License-Identifier: MIT
// UMI-OS ARM Cortex-M Vector Table Management
#pragma once

#include <cstdint>
#include <array>
#include "scb.hh"

namespace umi::port::arm {

/// Compile-time vector table alignment calculation
template <std::size_t N>
constexpr std::size_t vector_table_alignment() {
    // Must be power of 2 >= N * 4, minimum 128
    std::size_t size = N * 4;
    std::size_t align = 128;
    while (align < size) align *= 2;
    return align;
}

/// ARM Cortex-M Vector Table Manager (Header-only, compile-time optimized)
/// @tparam NumIRQs Number of external IRQs (e.g., 82 for STM32F4)
template <std::size_t NumIRQs = 82>
class VectorTable {
public:
    static constexpr std::size_t NUM_EXCEPTIONS = 16;
    static constexpr std::size_t SIZE = NUM_EXCEPTIONS + NumIRQs;
    static constexpr std::size_t ALIGNMENT = vector_table_alignment<SIZE>();
    
    using Handler = void(*)();
    
    enum class Exc : std::uint8_t {
        Reset = 1, NMI = 2, HardFault = 3, MemManage = 4,
        BusFault = 5, UsageFault = 6, SVCall = 11,
        DebugMon = 12, PendSV = 14, SysTick = 15,
    };

private:
    alignas(ALIGNMENT) std::array<Handler, SIZE> tbl_{};

    static void default_handler() { while(1) asm volatile("bkpt #0"); }

public:
    /// Initialize with SP and Reset, all others default
    constexpr void init(std::uint32_t sp, Handler reset) {
        for (auto& h : tbl_) h = default_handler;
        tbl_[0] = reinterpret_cast<Handler>(static_cast<uintptr_t>(sp));
        tbl_[1] = reset;
        SCB::set_vtor(static_cast<std::uint32_t>(reinterpret_cast<uintptr_t>(tbl_.data())));
        asm volatile("dsb\n isb" ::: "memory");
    }
    
    /// Set exception handler, returns previous
    Handler set(Exc e, Handler h) {
        auto i = static_cast<std::size_t>(e);
        Handler prev = tbl_[i];
        tbl_[i] = h ? h : default_handler;
        asm volatile("dsb" ::: "memory");
        return prev;
    }
    
    /// Set IRQ handler (0-based), returns previous
    Handler set_irq(std::size_t n, Handler h) {
        if (n >= NumIRQs) return nullptr;
        auto i = NUM_EXCEPTIONS + n;
        Handler prev = tbl_[i];
        tbl_[i] = h ? h : default_handler;
        asm volatile("dsb" ::: "memory");
        return prev;
    }
    
    /// Get handler
    Handler get(Exc e) const { return tbl_[static_cast<std::size_t>(e)]; }
    Handler get_irq(std::size_t n) const { 
        return n < NumIRQs ? tbl_[NUM_EXCEPTIONS + n] : nullptr; 
    }
    
    std::uint32_t base() const { return reinterpret_cast<std::uint32_t>(tbl_.data()); }
    constexpr std::size_t size() const { return SIZE; }
};

// Convenience aliases
using VecTableF4 = VectorTable<82>;   // STM32F4
using VecTableH7 = VectorTable<150>;  // STM32H7

/// Minimal boot vector (2 entries: SP + Reset) - place in .isr_vector
#define UMI_BOOT_VECTORS(sp, reset) \
    extern "C" { \
    __attribute__((section(".isr_vector"), used)) \
    void* const __boot_vectors[2] = { \
        reinterpret_cast<void*>(&(sp)), \
        reinterpret_cast<void*>(reset), \
    }; }

} // namespace umi::port::arm
