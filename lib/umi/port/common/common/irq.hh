// SPDX-License-Identifier: MIT
// UMI-OS Backend: Cortex-M Dynamic Interrupt System
//
// SRAM-based vector table with dynamic handler registration.
// Flash only needs minimal boot vectors (SP + Reset).
//
// Usage:
//   1. In startup: call umi::irq::init() early (after stack/bss setup)
//   2. Register handlers: umi::irq::set_handler(irq_num, my_handler)
//   3. Enable interrupts: umi::irq::enable(irq_num)
//
// Boot Vector (Flash):
//   Only 2 entries needed in Flash:
//     extern uint32_t _estack;
//     extern "C" void Reset_Handler();
//     __attribute__((section(".boot_vectors"), used))
//     const void* boot_vectors[2] = { &_estack, Reset_Handler };
//
#pragma once

#include <array>
#include <common/nvic.hh>
#include <common/scb.hh>
#include <cstddef>
#include <cstdint>
#include <umi/core/irq.hh>

namespace umi::backend::cm {

// ============================================================================
// Cortex-M Exception Numbers
// ============================================================================

namespace exc {
constexpr int NMI = -14;
constexpr int HardFault = -13;
constexpr int MemManage = -12;
constexpr int BusFault = -11;
constexpr int UsageFault = -10;
constexpr int SVCall = -5;
constexpr int DebugMon = -4;
constexpr int PendSV = -2;
constexpr int SysTick = -1;
} // namespace exc

// ============================================================================
// Configuration
// ============================================================================

/// Number of external IRQs (platform-specific)
/// STM32F4: 82, STM32H7: 150, RP2040: 26
#ifndef UMI_CM_NUM_IRQS
    #define UMI_CM_NUM_IRQS 82
#endif

/// Number of system exceptions (fixed for Cortex-M)
constexpr size_t NUM_EXCEPTIONS = 16;

/// Total vector table size
constexpr size_t VECTOR_TABLE_SIZE = NUM_EXCEPTIONS + UMI_CM_NUM_IRQS;

// ============================================================================
// SRAM Vector Table
// ============================================================================

/// SRAM-resident vector table.
/// Alignment must be power of 2 >= table size * 4, minimum 128.
template <size_t NumIrqs = UMI_CM_NUM_IRQS>
class VectorTableRAM {
  public:
    static constexpr size_t SIZE = NUM_EXCEPTIONS + NumIrqs;

    // Calculate alignment (power of 2 >= SIZE * 4, min 128)
    static constexpr size_t calc_align() {
        size_t n = SIZE * 4;
        size_t a = 128;
        while (a < n)
            a *= 2;
        return a;
    }
    static constexpr size_t ALIGNMENT = calc_align();

    using Handler = void (*)();

  private:
    // SRAM vector table (properly aligned)
    alignas(ALIGNMENT) std::array<Handler, SIZE> table_{};
    bool initialized_ = false;

    // Default handler - breakpoint and halt
    [[noreturn]] static void default_handler() {
        asm volatile("bkpt #0");
        while (true) {
            asm volatile("" ::: "memory");
        }
    }

  public:
    /// Initialize vector table in SRAM and switch VTOR.
    /// @param initial_sp  Initial stack pointer value
    /// @param reset       Reset handler (for warm reset)
    void init(uint32_t initial_sp, Handler reset) {
        if (initialized_)
            return;

        // Fill with default handlers
        for (auto& h : table_) {
            h = default_handler;
        }

        // Entry 0 is initial SP (cast to handler type)
        table_[0] = reinterpret_cast<Handler>(static_cast<uintptr_t>(initial_sp));

        // Entry 1 is Reset
        table_[1] = reset;

        // Update VTOR to point to SRAM table
        umi::port::arm::SCB::set_vtor(reinterpret_cast<uint32_t>(table_.data()));

        // Ensure write completes before any interrupts
        asm volatile("dsb\n isb" ::: "memory");

        initialized_ = true;
    }

    /// Set handler for system exception or IRQ.
    /// @param num  Exception/IRQ number:
    ///             - System exceptions: Use vector index (2=NMI, 3=HardFault, etc.)
    ///             - IRQs: Use (16 + irq_number)
    Handler set(size_t index, Handler h) {
        if (index >= SIZE)
            return nullptr;
        Handler prev = table_[index];
        table_[index] = h ? h : default_handler;
        asm volatile("dsb" ::: "memory");
        return prev;
    }

    /// Get handler at index.
    Handler get(size_t index) const { return index < SIZE ? table_[index] : nullptr; }

    /// Get table base address.
    uint32_t base() const { return reinterpret_cast<uint32_t>(table_.data()); }

    /// Check if initialized.
    bool is_initialized() const { return initialized_; }
};

// ============================================================================
// Global Vector Table Instance
// ============================================================================

/// Global SRAM vector table (instantiated in irq_cm.cc)
extern VectorTableRAM<UMI_CM_NUM_IRQS> g_vector_table;

// ============================================================================
// IRQ Number to Vector Index Conversion
// ============================================================================

/// Convert IRQ number to vector table index.
/// IRQ numbering: negative = system exceptions, 0+ = peripheral IRQs
/// Exception mapping:
///   -14 = NMI (vector 2)
///   -13 = HardFault (vector 3)
///   ...
///   -1 = SysTick (vector 15)
///   0+ = Peripheral IRQ (vector 16+)
inline constexpr size_t irq_to_index(int irq_num) {
    if (irq_num >= 0) {
        // Peripheral IRQ: vector index = 16 + irq_num
        return NUM_EXCEPTIONS + static_cast<size_t>(irq_num);
    } else {
        // System exception: vector index = 16 + irq_num
        // e.g., SysTick (-1) -> 15, PendSV (-2) -> 14
        return static_cast<size_t>(static_cast<int>(NUM_EXCEPTIONS) + irq_num);
    }
}

} // namespace umi::backend::cm

// ============================================================================
// External Symbols (defined in application/startup)
// ============================================================================

extern "C" {
extern uint32_t _estack;
[[noreturn]] void Reset_Handler();
}

// ============================================================================
// umi::irq Interface Implementation (inline for header-only option)
// ============================================================================

namespace umi::irq {

inline void init() {
    using namespace umi::backend::cm;
    g_vector_table.init(reinterpret_cast<uint32_t>(&_estack), reinterpret_cast<Handler>(Reset_Handler));
}

inline Handler set_handler(int irq_num, Handler handler) {
    using namespace umi::backend::cm;
    return g_vector_table.set(irq_to_index(irq_num), handler);
}

inline Handler get_handler(int irq_num) {
    using namespace umi::backend::cm;
    return g_vector_table.get(irq_to_index(irq_num));
}

inline void enable(int irq_num) {
    if (irq_num >= 0) {
        umi::port::arm::NVIC::enable(static_cast<uint8_t>(irq_num));
    }
}

inline void disable(int irq_num) {
    if (irq_num >= 0) {
        umi::port::arm::NVIC::disable(static_cast<uint8_t>(irq_num));
    }
}

inline void set_priority(int irq_num, uint8_t priority) {
    umi::port::arm::NVIC::set_prio(irq_num, priority);
}

inline uint8_t get_priority(int irq_num) {
    return umi::port::arm::NVIC::get_prio(irq_num);
}

inline bool is_pending(int irq_num) {
    if (irq_num >= 0) {
        return umi::port::arm::NVIC::is_pending(static_cast<uint8_t>(irq_num));
    }
    return false;
}

inline void clear_pending(int irq_num) {
    if (irq_num >= 0) {
        umi::port::arm::NVIC::clear_pending(static_cast<uint8_t>(irq_num));
    }
}

inline bool in_isr() {
    uint32_t ipsr;
    asm volatile("mrs %0, ipsr" : "=r"(ipsr));
    return ipsr != 0;
}

// RAII Guards
inline InterruptGuard::InterruptGuard(int irq_num) : irq_(irq_num) {
    was_enabled_ = umi::port::arm::NVIC::is_enabled(static_cast<uint8_t>(irq_num));
    disable(irq_num);
}

inline InterruptGuard::~InterruptGuard() {
    if (was_enabled_)
        enable(irq_);
}

inline CriticalSection::CriticalSection() {
    asm volatile("mrs %0, primask" : "=r"(saved_state_));
    asm volatile("cpsid i" ::: "memory");
}

inline CriticalSection::~CriticalSection() {
    asm volatile("msr primask, %0" ::"r"(saved_state_) : "memory");
}

} // namespace umi::irq
