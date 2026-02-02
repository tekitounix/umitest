// SPDX-License-Identifier: MIT
// UMI-OS SysTick Driver for Cortex-M
#pragma once

#include <cstdint>
#include <umios/kernel/driver.hh>
#include <common/systick.hh>

namespace umi::driver::systick {

// ============================================================================
// SysTick Driver State
// ============================================================================

struct State {
    uint64_t tick_count = 0;
    uint32_t tick_hz = 1000;      // Default 1ms ticks
    uint32_t cpu_hz = 168000000;  // Default STM32F4 @168MHz

    // Callback for tick notification
    void (*on_tick)(void* ctx) = nullptr;
    void* tick_ctx = nullptr;
};

// Global state (kernel-owned)
inline State g_state;

// ============================================================================
// Driver Implementation
// ============================================================================

inline int init(const void* config) {
    if (config) {
        auto* cfg = static_cast<const TimerConfig*>(config);
        g_state.tick_hz = cfg->tick_hz;
    }

    // Configure SysTick using static API
    uint32_t reload = (g_state.cpu_hz / g_state.tick_hz) - 1;
    port::arm::SysTick::init(reload);

    return 0;
}

inline void deinit() {
    port::arm::SysTick::disable();
}

inline void irq(uint32_t) {
    g_state.tick_count++;

    if (g_state.on_tick) {
        g_state.on_tick(g_state.tick_ctx);
    }
}

/// Set tick callback
inline void set_callback(void (*cb)(void*), void* ctx) {
    g_state.on_tick = cb;
    g_state.tick_ctx = ctx;
}

/// Get current tick count
inline uint64_t get_ticks() {
    return g_state.tick_count;
}

/// Get time in microseconds
inline uint64_t get_time_us() {
    // ticks * 1000000 / tick_hz
    return (g_state.tick_count * 1000000ULL) / g_state.tick_hz;
}

/// Get time in milliseconds
inline uint32_t get_time_ms() {
    return static_cast<uint32_t>(g_state.tick_count * 1000ULL / g_state.tick_hz);
}

// Driver operations table
inline const Ops kOps = {
    .name = "systick",
    .category = Category::TIMER,
    .init = init,
    .deinit = deinit,
    .irq = irq,
};

}  // namespace umi::driver::systick

// SysTick IRQ handler (called from vector table)
extern "C" inline void SysTick_Handler() {
    umi::driver::systick::irq(0);
}
