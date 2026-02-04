// SPDX-License-Identifier: MIT
// UMI-OS Core: Interrupt Handler Interface
//
// Backend-agnostic interface for dynamic interrupt handler registration.
// Implementations are provided by platform-specific backends.
#pragma once

#include <cstdint>

namespace umi::irq {

/// Interrupt handler function type
using Handler = void(*)();

// ============================================================================
// Backend Interface (must be implemented by platform backend)
// ============================================================================

/// Initialize the interrupt system.
/// For Cortex-M: Sets up SRAM vector table and updates VTOR.
void init();

/// Register an interrupt handler.
/// @param irq_num  IRQ number (negative for system exceptions, 0+ for peripheral IRQs)
///                 System exceptions: -15=SysTick, -14=PendSV, -5=SVC, etc.
/// @param handler  Function pointer (nullptr to use default handler)
/// @return Previous handler
Handler set_handler(int irq_num, Handler handler);

/// Get current interrupt handler.
Handler get_handler(int irq_num);

/// Enable an interrupt.
/// @param irq_num  Peripheral IRQ number (0+)
void enable(int irq_num);

/// Disable an interrupt.
/// @param irq_num  Peripheral IRQ number (0+)
void disable(int irq_num);

/// Set interrupt priority.
/// @param irq_num   IRQ number (negative for system exceptions)
/// @param priority  Priority value (0=highest, platform-dependent max)
void set_priority(int irq_num, uint8_t priority);

/// Get interrupt priority.
uint8_t get_priority(int irq_num);

/// Check if an interrupt is pending.
bool is_pending(int irq_num);

/// Clear pending interrupt.
void clear_pending(int irq_num);

/// Check if currently in interrupt context.
bool in_isr();

// ============================================================================
// RAII Interrupt Guard
// ============================================================================

/// RAII guard for disabling/enabling a specific interrupt
class InterruptGuard {
    int irq_;
    bool was_enabled_;
public:
    explicit InterruptGuard(int irq_num);
    ~InterruptGuard();
    InterruptGuard(const InterruptGuard&) = delete;
    InterruptGuard& operator=(const InterruptGuard&) = delete;
};

/// RAII guard for global interrupt disable
class CriticalSection {
    uint32_t saved_state_;
public:
    CriticalSection();
    ~CriticalSection();
    CriticalSection(const CriticalSection&) = delete;
    CriticalSection& operator=(const CriticalSection&) = delete;
};

}  // namespace umi::irq
