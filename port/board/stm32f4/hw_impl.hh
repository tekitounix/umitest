// SPDX-License-Identifier: MIT
// UMI-OS STM32F4 Hardware Implementation
#pragma once

// IWYU pragma: begin_exports
#include "../../arm/cortex-m/cortex_m4.hh"
#include "../../vendor/stm32/stm32f4/rcc.hh"
#include "../../vendor/stm32/stm32f4/gpio.hh"
#include "../../vendor/stm32/stm32f4/uart.hh"
// IWYU pragma: end_exports
#include <core/umi_kernel.hh>

namespace umi::board::stm32f4 {

using namespace umi::port::arm;
using namespace umi::port::stm32;

/// STM32F4 Hardware implementation for UMI-OS kernel
struct Hw {
    // Time tracking
    static inline umi::usec time_us_ = 0;
    static inline umi::usec timer_target_ = 0;
    
    // --- Timer ---
    static void set_timer_absolute(umi::usec t) { timer_target_ = t; }
    static umi::usec monotonic_time_usecs() { return time_us_; }
    
    // --- Critical Section ---
    static void enter_critical() { CM4::cpsid_i(); }
    static void exit_critical()  { CM4::cpsie_i(); }
    
    // --- Multi-Core (single-core MCU) ---
    static void trigger_ipi(std::uint8_t) {}
    static std::uint8_t current_core() { return 0; }
    
    // --- Context Switch ---
    static void request_context_switch() { SCB::trigger_pendsv(); }
    
    // --- FPU ---
    static void save_fpu() {}
    static void restore_fpu() {}
    
    // --- Audio ---
    static void mute_audio_dma() {}
    
    // --- Backup RAM ---
    static void write_backup_ram(const void*, std::size_t) {}
    static void read_backup_ram(void*, std::size_t) {}
    
    // --- MPU ---
    static void configure_mpu_region(std::size_t, const void*, std::size_t, bool, bool) {}
    
    // --- Cache (not on F4) ---
    static void cache_clean(const void*, std::size_t) {}
    static void cache_invalidate(void*, std::size_t) {}
    static void cache_clean_invalidate(void*, std::size_t) {}
    
    // --- System ---
    [[noreturn]] static void system_reset() { SCB::reset(); }
    static void enter_sleep() { CM4::wfi(); }
    [[noreturn]] static void start_first_task() { while(1) CM4::wfi(); }
    
    // --- Watchdog ---
    static void watchdog_init(std::uint32_t) {}
    static void watchdog_feed() {}
    
    // --- Cycle Counter ---
    static std::uint32_t cycle_count() { return DWT::cycles(); }
    static std::uint32_t cycles_per_usec() { return 168; }  // 168MHz
    
    // --- SysTick Handler (call from ISR) ---
    static void on_systick() { time_us_ += 1000; }  // 1ms tick
};

// Kernel type alias
template <std::size_t MaxTasks = 8, std::size_t MaxTimers = 8>
using Kernel = umi::Kernel<MaxTasks, MaxTimers, umi::Hw<Hw>>;

} // namespace umi::board::stm32f4
