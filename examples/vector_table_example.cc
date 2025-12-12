// SPDX-License-Identifier: MIT
// UMI-OS Example: Dynamic Vector Table Usage
//
// This example shows how to use the RAM-based vector table
// for dynamic interrupt handler registration.
//
// NOTE: This is ARM Cortex-M only code. clangd warnings on host are expected.

#pragma GCC diagnostic ignored "-Wshorten-64-to-32"

#include "port/arm/cortex-m/common/vector_table.hh"
#include "port/arm/cortex-m/common/nvic.hh"

namespace {

// Global vector table instance (uses ~400 bytes of RAM for STM32F4)
umi::port::arm::VecTableF4 g_vectors;

// Example: Custom SysTick handler
volatile std::uint32_t systick_counter = 0;

void MySysTick_Handler() {
    systick_counter = systick_counter + 1;
}

// Example: Custom USART2 handler (IRQ 38)
volatile bool uart_rx_flag = false;

void MyUSART2_Handler() {
    uart_rx_flag = true;
    // Clear interrupt flag, read data, etc.
}

} // namespace

extern "C" {
    extern std::uint32_t _estack[];
    void Reset_Handler();
}

/// Initialize UMI-OS vector table and core handlers
/// Call this early in startup, before enabling interrupts
void umi_init_vectors() {
    // Initialize with stack pointer and reset handler
    g_vectors.init(reinterpret_cast<std::uintptr_t>(&_estack), Reset_Handler);
    
    // Register custom handlers for system exceptions
    g_vectors.set(umi::port::arm::VecTableF4::Exc::SysTick, MySysTick_Handler);
    
    // Register custom handlers for peripheral IRQs
    // STM32F4 USART2 is IRQ 38
    g_vectors.set_irq(38, MyUSART2_Handler);
    
    // Enable the IRQ in NVIC
    umi::port::arm::NVIC::enable(38);
}

/// Example: Register a handler at runtime
/// This can be called from application code
void register_irq_handler(std::uint32_t irq_num, void(*handler)()) {
    g_vectors.set_irq(irq_num, handler);
    umi::port::arm::NVIC::enable(irq_num);
}

/// Example: Unregister a handler
void unregister_irq_handler(std::uint32_t irq_num) {
    umi::port::arm::NVIC::disable(irq_num);
    g_vectors.set_irq(irq_num, nullptr);  // Restores default handler
}

// =============================================================================
// Usage Example in main()
// =============================================================================

/*
int main() {
    // Initialize vector table first
    umi_init_vectors();
    
    // Initialize SysTick
    umi::port::arm::SysTick::init(72000 - 1);  // 1ms tick @ 72MHz
    
    // Main loop
    while (1) {
        if (uart_rx_flag) {
            uart_rx_flag = false;
            // Process received data
        }
        asm volatile("wfi");
    }
}
*/
