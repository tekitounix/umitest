// SPDX-License-Identifier: MIT
// UMI-OS Backend: STM32F4 Hardware Abstraction
//
// Hardware trait implementation for STM32F4 series @ 168MHz.
// Provides timer, critical section, and system functions for umios kernel.
#pragma once

#include <cstdint>
#include <common/scb.hh>
#include <common/dwt.hh>
#include <arch/cortex_m4.hh>

namespace umi::backend::stm32f4 {

using namespace umi::port::arm;

// ============================================================================
// STM32F4 Hardware Trait
// ============================================================================

/// Hardware abstraction for STM32F4 @ 168MHz.
/// Implements the HW trait interface for umios kernel.
///
/// Template parameters:
///   CpuFreq - CPU frequency in Hz (default 168MHz)
template<uint32_t CpuFreq = 168'000'000>
struct Hw {
    static constexpr uint32_t CPU_FREQ = CpuFreq;
    static constexpr uint32_t CYCLES_PER_USEC = CPU_FREQ / 1'000'000;
    
    // ========================================================================
    // Timer Functions
    // ========================================================================
    
    /// Set absolute timer target (not used with SysTick)
    static void set_timer_absolute(uint64_t /*target_us*/) {
        // SysTick is periodic, not one-shot
        // For one-shot, would need TIM peripheral
    }
    
    /// Get monotonic time in microseconds using DWT cycle counter.
    /// Note: Wraps every ~25 seconds at 168MHz (32-bit counter).
    /// For long-term timing, use a 64-bit extension.
    static uint64_t monotonic_time_usecs() {
        static uint64_t base_time = 0;
        static uint32_t last_cycles = 0;
        
        uint32_t now = DWT::cycles();
        uint32_t delta = now - last_cycles;
        last_cycles = now;
        base_time += delta / CYCLES_PER_USEC;
        
        return base_time;
    }
    
    // ========================================================================
    // Critical Section
    // ========================================================================
    
    /// Enter critical section (disable interrupts)
    static void enter_critical() {
        CM4::cpsid_i();
    }
    
    /// Exit critical section (enable interrupts)
    static void exit_critical() {
        CM4::cpsie_i();
    }
    
    // ========================================================================
    // Multi-Core (Single core on STM32F4)
    // ========================================================================
    
    static void trigger_ipi(uint8_t) { }
    static uint8_t current_core() { return 0; }
    
    // ========================================================================
    // Context Switch
    // ========================================================================
    
    /// Request context switch (trigger PendSV).
    /// Called by kernel when a higher priority task becomes ready.
    static void request_context_switch() {
        SCB::trigger_pendsv();
    }
    
    // ========================================================================
    // FPU Context
    // ========================================================================
    
    /// Save FPU context (handled by lazy stacking on Cortex-M4F)
    static void save_fpu() { }
    
    /// Restore FPU context (handled by lazy stacking on Cortex-M4F)
    static void restore_fpu() { }
    
    // ========================================================================
    // Audio (optional, implement in derived class)
    // ========================================================================
    
    static void mute_audio_dma() { }
    
    // ========================================================================
    // Persistent Storage
    // ========================================================================
    
    static void write_backup_ram(const void*, size_t) { }
    static void read_backup_ram(void*, size_t) { }
    
    // ========================================================================
    // MPU Configuration
    // ========================================================================
    
    static void configure_mpu_region(size_t, const void*, size_t, bool, bool) { }
    
    // ========================================================================
    // Cache (no cache on Cortex-M4)
    // ========================================================================
    
    static void cache_clean(const void*, size_t) { }
    static void cache_invalidate(void*, size_t) { }
    static void cache_clean_invalidate(void*, size_t) { }
    
    // ========================================================================
    // System Control
    // ========================================================================
    
    static void system_reset() {
        SCB::reset();
    }
    
    static void enter_sleep() {
        CM4::wfi();
    }
    
    // ========================================================================
    // Watchdog (not implemented by default)
    // ========================================================================
    
    static void watchdog_init(uint32_t) { }
    static void watchdog_feed() { }
    
    // ========================================================================
    // Performance Counters
    // ========================================================================
    
    static uint32_t cycle_count() {
        return DWT::cycles();
    }
    
    static uint32_t cycles_per_usec() {
        return CYCLES_PER_USEC;
    }
};

// Default instance for 168MHz
using Stm32f4Hw168 = Hw<168'000'000>;

}  // namespace umi::backend::stm32f4
