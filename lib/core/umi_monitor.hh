// =====================================================================
// UMI-OS Runtime Monitor
// =====================================================================
//
// Comprehensive runtime monitoring for embedded systems:
//   - Stack usage per task (watermark detection)
//   - Heap usage tracking
//   - Task execution time profiling
//   - DSP load monitoring (uses existing LoadMonitor)
//   - System-wide statistics
//
// =====================================================================

#pragma once

#include <cstdint>
#include <cstddef>
#include <array>

namespace umi {

// =====================================================================
// Stack Monitor
// =====================================================================
//
// Uses stack painting technique:
//   1. Fill stack with magic pattern at task creation
//   2. Periodically scan from bottom to find high-water mark
//
// =====================================================================

/// Stack usage monitor using watermark detection
template<class HW>
class StackMonitor {
public:
    static constexpr std::uint32_t STACK_MAGIC = 0xDEADBEEF;
    
    /// Paint stack with magic pattern (call at task creation)
    static void paint_stack(void* stack_bottom, std::size_t size_bytes) {
        auto* p = static_cast<std::uint32_t*>(stack_bottom);
        std::size_t words = size_bytes / sizeof(std::uint32_t);
        for (std::size_t i = 0; i < words; ++i) {
            p[i] = STACK_MAGIC;
        }
    }
    
    /// Get used stack bytes (high-water mark)
    static std::size_t used_bytes(const void* stack_bottom, std::size_t size_bytes) {
        auto* p = static_cast<const std::uint32_t*>(stack_bottom);
        std::size_t words = size_bytes / sizeof(std::uint32_t);
        
        // Scan from bottom until we find non-magic value
        std::size_t unused_words = 0;
        for (std::size_t i = 0; i < words; ++i) {
            if (p[i] != STACK_MAGIC) break;
            ++unused_words;
        }
        
        return size_bytes - (unused_words * sizeof(std::uint32_t));
    }
    
    /// Get free stack bytes
    static std::size_t free_bytes(const void* stack_bottom, std::size_t size_bytes) {
        return size_bytes - used_bytes(stack_bottom, size_bytes);
    }
    
    /// Get usage percentage (0-100)
    static std::uint8_t usage_percent(const void* stack_bottom, std::size_t size_bytes) {
        if (size_bytes == 0) return 0;
        return static_cast<std::uint8_t>(used_bytes(stack_bottom, size_bytes) * 100 / size_bytes);
    }
    
    /// Check if stack is near overflow (>90% used)
    static bool is_critical(const void* stack_bottom, std::size_t size_bytes) {
        return usage_percent(stack_bottom, size_bytes) > 90;
    }
};

// =====================================================================
// Heap Monitor
// =====================================================================
//
// Tracks heap allocations via wrapped malloc/free or custom allocator.
// For bare-metal: uses _sbrk tracking or explicit registration.
//
// =====================================================================

/// Heap usage tracker
class HeapMonitor {
public:
    /// Register allocation (call from malloc wrapper)
    void on_alloc(std::size_t bytes) {
        current_bytes_ += bytes;
        total_allocs_++;
        if (current_bytes_ > peak_bytes_) {
            peak_bytes_ = current_bytes_;
        }
    }
    
    /// Register deallocation (call from free wrapper)
    void on_free(std::size_t bytes) {
        if (bytes <= current_bytes_) {
            current_bytes_ -= bytes;
        }
        total_frees_++;
    }
    
    /// Set total heap size (from linker symbols)
    void set_total_size(std::size_t bytes) { total_size_ = bytes; }
    
    /// Current heap usage
    std::size_t current_bytes() const { return current_bytes_; }
    
    /// Peak heap usage
    std::size_t peak_bytes() const { return peak_bytes_; }
    
    /// Free heap bytes
    std::size_t free_bytes() const { 
        return (total_size_ > current_bytes_) ? (total_size_ - current_bytes_) : 0; 
    }
    
    /// Usage percentage
    std::uint8_t usage_percent() const {
        if (total_size_ == 0) return 0;
        return static_cast<std::uint8_t>(current_bytes_ * 100 / total_size_);
    }
    
    /// Total allocation count
    std::uint32_t alloc_count() const { return total_allocs_; }
    
    /// Total free count
    std::uint32_t free_count() const { return total_frees_; }
    
    /// Check for memory leaks (allocs != frees)
    bool has_leaks() const { return total_allocs_ != total_frees_; }
    
    /// Reset peak tracking
    void reset_peak() { peak_bytes_ = current_bytes_; }

private:
    std::size_t current_bytes_ {0};
    std::size_t peak_bytes_ {0};
    std::size_t total_size_ {0};
    std::uint32_t total_allocs_ {0};
    std::uint32_t total_frees_ {0};
};

// =====================================================================
// Task Profiler
// =====================================================================
//
// Measures execution time per task:
//   - Total runtime
//   - Last run duration
//   - Average/peak execution time
//
// =====================================================================

/// Per-task execution statistics
struct TaskStats {
    std::uint64_t total_runtime_us {0};   ///< Total time in Running state
    std::uint32_t run_count {0};          ///< Number of times scheduled
    std::uint32_t last_runtime_us {0};    ///< Last execution duration
    std::uint32_t peak_runtime_us {0};    ///< Maximum execution duration
    std::uint32_t avg_runtime_us {0};     ///< Moving average
    
    void reset() {
        total_runtime_us = 0;
        run_count = 0;
        last_runtime_us = 0;
        peak_runtime_us = 0;
        avg_runtime_us = 0;
    }
};

/// Task execution profiler
template<class HW, std::size_t MaxTasks>
class TaskProfiler {
public:
    /// Call when task starts running (context switch in)
    void on_task_start(std::uint16_t task_idx) {
        if (task_idx >= MaxTasks) return;
        start_cycles_[task_idx] = HW::cycle_count();
    }
    
    /// Call when task stops running (context switch out)
    void on_task_stop(std::uint16_t task_idx) {
        if (task_idx >= MaxTasks) return;
        
        std::uint32_t end = HW::cycle_count();
        std::uint32_t elapsed = end - start_cycles_[task_idx];
        std::uint32_t us = elapsed / HW::cycles_per_usec();
        
        auto& s = stats_[task_idx];
        s.total_runtime_us += us;
        s.run_count++;
        s.last_runtime_us = us;
        if (us > s.peak_runtime_us) s.peak_runtime_us = us;
        
        // Exponential moving average (alpha = 1/8)
        s.avg_runtime_us = (s.avg_runtime_us * 7 + us) / 8;
    }
    
    /// Get stats for a task
    const TaskStats& stats(std::uint16_t task_idx) const {
        static const TaskStats empty{};
        return (task_idx < MaxTasks) ? stats_[task_idx] : empty;
    }
    
    /// Reset stats for a task
    void reset(std::uint16_t task_idx) {
        if (task_idx < MaxTasks) stats_[task_idx].reset();
    }
    
    /// Reset all stats
    void reset_all() {
        for (auto& s : stats_) s.reset();
    }
    
    /// Get CPU usage percentage for a task (over last second)
    /// Must be called periodically with elapsed time
    std::uint8_t cpu_percent(std::uint16_t task_idx, std::uint64_t period_us) const {
        if (task_idx >= MaxTasks || period_us == 0) return 0;
        // This is a simplified calculation; for accurate results,
        // track per-period runtime separately
        return 0;  // TODO: implement periodic tracking
    }

private:
    std::array<TaskStats, MaxTasks> stats_ {};
    std::array<std::uint32_t, MaxTasks> start_cycles_ {};
};

// =====================================================================
// System Monitor (aggregate)
// =====================================================================
//
// Combines all monitoring into a single interface.
// Integrates with kernel and shell.
//
// =====================================================================

/// System-wide resource statistics
struct SystemStats {
    // Memory
    std::size_t heap_used {0};
    std::size_t heap_free {0};
    std::size_t heap_peak {0};
    
    // Tasks
    std::uint16_t task_count {0};
    std::uint16_t task_ready {0};
    std::uint16_t task_blocked {0};
    
    // CPU
    std::uint32_t dsp_load_percent_x100 {0};  ///< DSP load (0-10000)
    std::uint32_t cpu_idle_percent {0};       ///< Idle task percentage
    
    // Time
    std::uint64_t uptime_us {0};
    std::uint32_t context_switches {0};
};

/// Aggregated system monitor
template<class HW, std::size_t MaxTasks>
class SystemMonitor {
public:
    HeapMonitor heap;
    TaskProfiler<HW, MaxTasks> tasks;
    
    /// Increment context switch counter
    void on_context_switch() { context_switches_++; }
    
    /// Get context switch count
    std::uint32_t context_switches() const { return context_switches_; }
    
    /// Get aggregated system statistics
    template<typename Kernel>
    SystemStats get_stats(const Kernel& kernel) const {
        SystemStats s;
        
        // Memory
        s.heap_used = heap.current_bytes();
        s.heap_free = heap.free_bytes();
        s.heap_peak = heap.peak_bytes();
        
        // Tasks
        kernel.for_each_task([&s](auto, auto&, auto state) {
            s.task_count++;
            using State = decltype(state);
            if constexpr (requires { state == State::Ready; }) {
                // Note: This won't compile as-is due to enum comparison
                // In real code, compare against kernel's State enum
            }
        });
        
        // Time
        s.uptime_us = kernel.time();
        s.context_switches = context_switches_;
        
        return s;
    }
    
    /// Reset all statistics
    void reset_all() {
        tasks.reset_all();
        context_switches_ = 0;
        heap.reset_peak();
    }

private:
    std::uint32_t context_switches_ {0};
};

// =====================================================================
// IRQ Latency Monitor
// =====================================================================
//
// Measures interrupt latency for real-time performance validation.
//
// =====================================================================

/// IRQ latency tracker
template<class HW>
class IrqLatencyMonitor {
public:
    /// Call at IRQ entry (as early as possible)
    void on_irq_entry() {
        std::uint32_t now = HW::cycle_count();
        std::uint32_t latency = now - last_timer_trigger_;
        
        sample_count_++;
        if (latency > peak_latency_cycles_) peak_latency_cycles_ = latency;
        
        // Running average
        avg_latency_cycles_ = (avg_latency_cycles_ * 7 + latency) / 8;
    }
    
    /// Call when timer fires (to set expected trigger time)
    void on_timer_trigger() {
        last_timer_trigger_ = HW::cycle_count();
    }
    
    /// Peak latency in microseconds
    std::uint32_t peak_latency_us() const {
        return peak_latency_cycles_ / HW::cycles_per_usec();
    }
    
    /// Average latency in microseconds
    std::uint32_t avg_latency_us() const {
        return avg_latency_cycles_ / HW::cycles_per_usec();
    }
    
    /// Reset statistics
    void reset() {
        peak_latency_cycles_ = 0;
        avg_latency_cycles_ = 0;
        sample_count_ = 0;
    }
    
    std::uint32_t sample_count() const { return sample_count_; }

private:
    std::uint32_t last_timer_trigger_ {0};
    std::uint32_t peak_latency_cycles_ {0};
    std::uint32_t avg_latency_cycles_ {0};
    std::uint32_t sample_count_ {0};
};

} // namespace umi
