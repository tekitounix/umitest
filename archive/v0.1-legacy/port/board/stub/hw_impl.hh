// SPDX-License-Identifier: MIT
// UMI-OS Host/Test Stub Hardware Implementation
#pragma once

#include "../../../core/umi_kernel.hh"

namespace umi::board::stub {

/// Stub hardware for host testing (no real hardware)
struct Hw {
    static inline umi::usec time_us_ = 0;
    
    static void set_timer_absolute(umi::usec) {}
    static umi::usec monotonic_time_usecs() { return time_us_; }
    
    static void enter_critical() {}
    static void exit_critical() {}
    
    static void trigger_ipi(std::uint8_t) {}
    static std::uint8_t current_core() { return 0; }
    
    static void request_context_switch() {}
    static void save_fpu() {}
    static void restore_fpu() {}
    static void mute_audio_dma() {}
    
    static void write_backup_ram(const void*, std::size_t) {}
    static void read_backup_ram(void*, std::size_t) {}
    static void configure_mpu_region(std::size_t, const void*, std::size_t, bool, bool) {}
    
    static void cache_clean(const void*, std::size_t) {}
    static void cache_invalidate(void*, std::size_t) {}
    static void cache_clean_invalidate(void*, std::size_t) {}
    
    [[noreturn]] static void system_reset() { while(1); }
    static void enter_sleep() {}
    [[noreturn]] static void start_first_task() { while(1); }
    
    static void watchdog_init(std::uint32_t) {}
    static void watchdog_feed() {}
    
    static std::uint32_t cycle_count() { return 0; }
    static std::uint32_t cycles_per_usec() { return 1; }
    
    // Test helper
    static void advance_time(umi::usec us) { time_us_ += us; }
};

template <std::size_t MaxTasks = 8, std::size_t MaxTimers = 8>
using Kernel = umi::Kernel<MaxTasks, MaxTimers, umi::Hw<Hw>>;

} // namespace umi::board::stub
