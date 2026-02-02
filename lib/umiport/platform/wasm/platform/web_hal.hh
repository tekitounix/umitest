// SPDX-License-Identifier: MIT
// =====================================================================
// UMI-OS WASM Hardware Abstraction Layer
// =====================================================================
//
// Implements umi::Hw<Impl> interface for WebAssembly environment.
// This allows the actual umi::Kernel to run in WASM with full compatibility.
//
// Key differences from embedded:
//   - Single-threaded (no preemption, cooperative scheduling only)
//   - Time source: emscripten_get_now() or performance.now() via JS
//   - Critical sections: no-op (single-threaded, no real interrupts)
//   - Context switch: cooperative via coroutine yield
//   - Audio "interrupts": AudioWorklet callback timing
//
// Architecture:
//   ┌─────────────────────────────────────────┐
//   │ Application (synth_processor.hh)        │  ← Identical code
//   └─────────────────────────────────────────┘
//             ↓
//   ┌─────────────────────────────────────────┐
//   │ umi::Kernel<MaxTasks, MaxTimers, HW>    │  ← Same kernel code
//   └─────────────────────────────────────────┘
//             ↓
//   ┌─────────────────────────────────────────┐
//   │ umi::Hw<WasmHwImpl>                     │  ← This file
//   └─────────────────────────────────────────┘
//             ↓
//   ┌─────────────────────────────────────────┐
//   │ Web APIs (performance.now, localStorage)│
//   └─────────────────────────────────────────┘
//
// =====================================================================

#pragma once

#include <cstdint>
#include <cstddef>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#define UMI_WASM_EXPORT EMSCRIPTEN_KEEPALIVE extern "C"
#else
#define UMI_WASM_EXPORT extern "C"
#endif

namespace umi::web {

// =====================================================================
// HW Simulation Parameters (configurable from JS for accurate timing)
// =====================================================================

struct HwSimParams {
    // CPU timing configuration (for DSP load calculation)
    uint32_t cpu_freq_mhz = 168;           // Simulated CPU clock frequency
    uint32_t isr_overhead_cycles = 100;    // ISR entry/exit overhead
    uint32_t base_cycles_per_sample = 20;  // Base cost per sample (buffer copy)
    uint32_t voice_cycles_per_sample = 200;// Per-voice processing cycles/sample
    uint32_t event_cycles = 80;            // Per-MIDI-event processing cycles

    // Convert cycles to microseconds
    float cycles_to_us(uint32_t cycles) const {
        return static_cast<float>(cycles) / static_cast<float>(cpu_freq_mhz);
    }

    // Calculate processing time for a buffer
    uint32_t calculate_process_us(uint32_t buffer_size, uint32_t active_voices,
                                   uint32_t event_count) const {
        uint64_t total_cycles =
            static_cast<uint64_t>(isr_overhead_cycles) +
            static_cast<uint64_t>(buffer_size) * base_cycles_per_sample +
            static_cast<uint64_t>(buffer_size) * active_voices * voice_cycles_per_sample +
            static_cast<uint64_t>(event_count) * event_cycles;
        return static_cast<uint32_t>(total_cycles / cpu_freq_mhz);
    }
};

inline HwSimParams g_hw_sim_params;

// =====================================================================
// Web Hardware Abstraction Implementation
// =====================================================================
//
// Provides all static methods required by umi::Hw<Impl> template.
// This is the actual backend used by umi::Kernel in WASM environment.
//

struct WasmHwImpl {
    // =========================================================================
    // Timer Operations
    // =========================================================================

    static void set_timer_absolute(std::uint64_t target_us) {
        next_timer_target_ = target_us;
    }

    static std::uint64_t monotonic_time_usecs() {
#ifdef __EMSCRIPTEN__
        // Use emscripten's high-resolution timer
        return static_cast<std::uint64_t>(emscripten_get_now() * 1000.0);
#else
        return time_us_;
#endif
    }

    /// Check if next timer has expired
    static bool timer_expired() {
        return monotonic_time_usecs() >= next_timer_target_;
    }

    /// Get next timer target
    static std::uint64_t next_timer_target() {
        return next_timer_target_;
    }

    // =========================================================================
    // Critical Sections (no-op for single-threaded WASM)
    // =========================================================================

    static void enter_critical() {
        in_critical_ = true;
    }

    static void exit_critical() {
        in_critical_ = false;
    }

    static bool in_critical() {
        return in_critical_;
    }

    // =========================================================================
    // Multi-Core (WASM is always single-core)
    // =========================================================================

    static void trigger_ipi(std::uint8_t) {
        // No inter-processor interrupts in single-threaded WASM
    }

    static std::uint8_t current_core() {
        return 0;  // Always core 0
    }

    // =========================================================================
    // Context Switch
    // =========================================================================
    // In WASM, "context switch" means yielding back to the event loop.
    // The kernel scheduler will be re-entered on the next tick.

    static void request_context_switch() {
        context_switch_pending_ = true;
    }

    static bool context_switch_pending() {
        return context_switch_pending_;
    }

    static void clear_context_switch() {
        context_switch_pending_ = false;
    }

    // =========================================================================
    // FPU Context (no-op for WASM)
    // =========================================================================

    static void save_fpu() {}
    static void restore_fpu() {}

    // =========================================================================
    // Audio DMA
    // =========================================================================

    static void mute_audio_dma() {
        audio_muted_ = true;
    }

    static void unmute_audio_dma() {
        audio_muted_ = false;
    }

    static bool is_audio_muted() {
        return audio_muted_;
    }

    // =========================================================================
    // Persistent Storage
    // =========================================================================
    // In WASM, this could be connected to localStorage or IndexedDB via JS.

    using StorageWriteFn = void (*)(const void* data, std::size_t bytes);
    using StorageReadFn = void (*)(void* data, std::size_t bytes);

    static void set_storage_callbacks(StorageWriteFn write_fn, StorageReadFn read_fn) {
        storage_write_fn_ = write_fn;
        storage_read_fn_ = read_fn;
    }

    static void write_backup_ram(const void* data, std::size_t bytes) {
        if (storage_write_fn_) {
            storage_write_fn_(data, bytes);
        }
    }

    static void read_backup_ram(void* data, std::size_t bytes) {
        if (storage_read_fn_) {
            storage_read_fn_(data, bytes);
        }
    }

    // =========================================================================
    // MPU (no-op for WASM)
    // =========================================================================

    static void configure_mpu_region(std::size_t, const void*,
                                     std::size_t, bool, bool) {}

    // =========================================================================
    // Cache (no-op for WASM)
    // =========================================================================

    static void cache_clean(const void*, std::size_t) {}
    static void cache_invalidate(void*, std::size_t) {}
    static void cache_clean_invalidate(void*, std::size_t) {}

    // =========================================================================
    // System Operations
    // =========================================================================

    [[noreturn]] static void system_reset() {
#ifdef __EMSCRIPTEN__
        EM_ASM({ location.reload(); });
#endif
        while (true) {}
    }

    static void enter_sleep() {
        // In WASM, "sleep" yields to the event loop
        // This is effectively a no-op as we're callback-driven
    }

    [[noreturn]] static void start_first_task() {
        // In WASM, this would start the main event loop
        while (true) {
#ifdef __EMSCRIPTEN__
            emscripten_sleep(0);  // Yield to browser
#endif
        }
    }

    // =========================================================================
    // Watchdog
    // =========================================================================

    static void watchdog_init(std::uint32_t timeout_ms) {
        watchdog_timeout_ms_ = timeout_ms;
        watchdog_enabled_ = (timeout_ms > 0);
        watchdog_last_feed_us_ = monotonic_time_usecs();
    }

    static void watchdog_feed() {
        watchdog_last_feed_us_ = monotonic_time_usecs();
    }

    static bool watchdog_expired() {
        if (!watchdog_enabled_) return false;
        uint64_t elapsed_us = monotonic_time_usecs() - watchdog_last_feed_us_;
        return elapsed_us > (watchdog_timeout_ms_ * 1000ULL);
    }

    static bool watchdog_enabled() {
        return watchdog_enabled_;
    }

    static uint32_t watchdog_timeout_ms() {
        return watchdog_timeout_ms_;
    }

    // =========================================================================
    // Performance Counters
    // =========================================================================
    // For DSP load calculation, we use configurable CPU frequency.

    static std::uint32_t cycle_count() {
        // Return simulated cycle count based on time and CPU frequency
        return static_cast<std::uint32_t>(
            monotonic_time_usecs() * g_hw_sim_params.cpu_freq_mhz);
    }

    static std::uint32_t cycles_per_usec() {
        return g_hw_sim_params.cpu_freq_mhz;
    }

    // =========================================================================
    // WASM-Specific Extensions
    // =========================================================================

    /// Advance simulated time (for non-Emscripten builds or testing)
    static void advance_time(std::uint64_t delta_us) {
        time_us_ += delta_us;
    }

    /// Set time directly (for synchronization with AudioContext.currentTime)
    static void set_time(std::uint64_t time_us) {
        time_us_ = time_us;
    }

    /// Get internal time (for non-Emscripten builds)
    static std::uint64_t get_internal_time() {
        return time_us_;
    }

    /// Reset all state
    static void reset() {
        time_us_ = 0;
        next_timer_target_ = UINT64_MAX;
        in_critical_ = false;
        context_switch_pending_ = false;
        audio_muted_ = false;
        watchdog_timeout_ms_ = 0;
        watchdog_last_feed_us_ = 0;
        watchdog_enabled_ = false;
    }

    /// Set audio parameters for timing calculations
    static void set_audio_params(uint32_t sample_rate, uint32_t buffer_size) {
        sample_rate_ = sample_rate;
        buffer_size_ = buffer_size;
    }

    /// Get audio buffer period in microseconds
    static uint64_t audio_buffer_period_us() {
        return (static_cast<uint64_t>(buffer_size_) * 1000000ULL) / sample_rate_;
    }

    // =========================================================================
    // DSP Load Tracking
    // =========================================================================

    /// Begin audio processing (call at start of process callback)
    static void begin_audio_process() {
        process_start_us_ = monotonic_time_usecs();
        active_voices_ = 0;
        event_count_ = 0;
        in_audio_callback_ = true;
    }

    /// Set number of active voices for load calculation
    static void set_active_voices(uint32_t count) {
        active_voices_ = count;
    }

    /// Add event count for load calculation
    static void add_events(uint32_t count) {
        event_count_ += count;
    }

    /// End audio processing and calculate DSP load
    /// Returns load as 0-10000 (0.00% - 100.00%)
    static uint32_t end_audio_process() {
        in_audio_callback_ = false;

        // Calculate simulated processing time
        uint32_t process_us = g_hw_sim_params.calculate_process_us(
            buffer_size_, active_voices_, event_count_);

        // Calculate budget
        uint64_t budget_us = audio_buffer_period_us();

        // Calculate load percentage (x100 for 0.01% precision)
        uint32_t load_x100 = 0;
        if (budget_us > 0) {
            load_x100 = static_cast<uint32_t>(
                (static_cast<uint64_t>(process_us) * 10000) / budget_us);
        }

        // Cap at 100%
        if (load_x100 > 10000) {
            load_x100 = 10000;
        }

        dsp_load_x100_ = load_x100;
        if (load_x100 > dsp_peak_x100_) {
            dsp_peak_x100_ = load_x100;
        }

        return load_x100;
    }

    /// Get current DSP load (0-10000)
    static uint32_t dsp_load() {
        return dsp_load_x100_;
    }

    /// Get peak DSP load (0-10000)
    static uint32_t dsp_peak() {
        return dsp_peak_x100_;
    }

    /// Reset peak DSP load
    static void reset_dsp_peak() {
        dsp_peak_x100_ = 0;
    }

    /// Check if in audio callback
    static bool in_audio_callback() {
        return in_audio_callback_;
    }

private:
    // Time tracking
    static inline std::uint64_t time_us_ = 0;
    static inline std::uint64_t next_timer_target_ = UINT64_MAX;

    // State flags
    static inline bool in_critical_ = false;
    static inline bool context_switch_pending_ = false;
    static inline bool audio_muted_ = false;
    static inline bool in_audio_callback_ = false;

    // Watchdog
    static inline uint32_t watchdog_timeout_ms_ = 0;
    static inline uint64_t watchdog_last_feed_us_ = 0;
    static inline bool watchdog_enabled_ = false;

    // Audio parameters
    static inline uint32_t sample_rate_ = 48000;
    static inline uint32_t buffer_size_ = 128;

    // DSP load tracking
    static inline uint64_t process_start_us_ = 0;
    static inline uint32_t active_voices_ = 0;
    static inline uint32_t event_count_ = 0;
    static inline uint32_t dsp_load_x100_ = 0;
    static inline uint32_t dsp_peak_x100_ = 0;

    // Storage callbacks
    static inline StorageWriteFn storage_write_fn_ = nullptr;
    static inline StorageReadFn storage_read_fn_ = nullptr;
};

// Backward compatibility alias
using WebHwImpl = WasmHwImpl;

} // namespace umi::web
