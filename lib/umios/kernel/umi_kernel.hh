#pragma once
#include <cstdint>
#include <cstddef>
#include <array>
#include <optional>
#include <span>
#include <atomic>
#include <type_traits>

// =====================================================================
// UMI-Kernel: Minimal Real-Time Kernel for Audio Applications
// =====================================================================
//
// C++23, header-only, static allocation only.
// Designed for Cortex-M/A, RISC-V, Xtensa with weak memory model support.
//
// API Layers:
// -----------
//   1. Platform Implementer API
//      - Hw<Impl>: Hardware abstraction (must implement for each platform)
//      - ISR entry points: on_timer_irq(), on_buffer_complete()
//
//   2. User Application API
//      - Task management: create_task(), notify(), wait(), wait_block()
//      - Timers: call_later()
//      - IPC: SpscQueue, Notification
//      - Utilities: Stopwatch, LoadMonitor
//
//   3. Kernel Internals (do not use directly)
//      - Scheduler private methods
//      - Context switch machinery
//
// =====================================================================

namespace umi {

// =====================================================================
// SECTION 1: Core Types
// =====================================================================
// Basic types and structures used throughout the kernel.

/// Microseconds (64-bit for long uptime)
using usec = std::uint64_t;

/// Task priority levels (lower value = higher priority)
enum class Priority : std::uint8_t {
    Realtime = 0,  ///< Audio processing, DMA callbacks - highest
    Server   = 1,  ///< Drivers, I/O handlers
    User     = 2,  ///< Application tasks - round-robin among same priority
    Idle     = 3,  ///< Background, sleep management - lowest
};

/// @deprecated Use Priority::Realtime instead
[[deprecated("Use Priority::Realtime")]] static constexpr Priority Audio = Priority::Realtime;

/// Core affinity
enum class Core : std::uint8_t {
    Any = 0xFF,  ///< Can run on any core
};

/// Task identifier (opaque handle)
struct TaskId {
    std::uint16_t value {0xFFFF};
    constexpr bool valid() const { return value != 0xFFFF; }
};

/// Crash dump for post-mortem analysis
struct CrashDump {
    std::uint32_t regs[17] {};      ///< r0-r12, sp, lr, pc, psr (platform-defined)
    const char* assert_file {nullptr};
    std::uint32_t assert_line {0};
    const char* reason {nullptr};
};

/// Task configuration for create_task()
struct TaskConfig {
    void (*entry)(void*) {nullptr};  ///< Task entry point
    void* arg {nullptr};             ///< Argument passed to entry
    Priority prio {Priority::Idle};  ///< Task priority
    std::uint8_t core_affinity {static_cast<std::uint8_t>(Core::Any)};
    bool uses_fpu {false};           ///< Enable FPU context save/restore
    const char* name {"<unnamed>"};  ///< Task name for debugging/shell
};

/// Shared memory region identifiers
enum class SharedRegionId : std::uint8_t {
    Audio       = 0,  ///< Audio I/O buffers (DMA)
    Midi        = 1,  ///< MIDI event ring buffer
    Framebuffer = 2,  ///< Display framebuffer
    HwState     = 3,  ///< Hardware state (read-only for app)
    MaxRegions  = 8,
};

/// Shared memory region descriptor
struct SharedRegionDesc {
    void* base {nullptr};
    std::size_t size {0};
    
    constexpr bool valid() const { return base != nullptr && size > 0; }
};

/// Event bits for wait() syscall
/// Renamed from Event to KernelEvent to avoid conflict with umi::Event struct in event.hh
namespace KernelEvent {
    constexpr std::uint32_t AudioReady = 1 << 0;
    constexpr std::uint32_t MidiReady  = 1 << 1;
    constexpr std::uint32_t VSync      = 1 << 2;
}

// =====================================================================
// SECTION 2: Platform Implementer API (Hardware Abstraction)
// =====================================================================
// Platform developers must implement all methods in their Impl class.
// These are called by the kernel to interact with hardware.

/// Hardware abstraction layer template.
/// Platform must provide a class implementing all static methods.
///
/// Example:
/// @code
/// struct MyPlatformHw {
///     static void set_timer_absolute(usec target);
///     static usec monotonic_time_usecs();
///     static void enter_critical();
///     static void exit_critical();
///     // ... all other methods
/// };
/// using HW = umi::Hw<MyPlatformHw>;
/// @endcode
template <class Impl>
struct Hw {
    // --- Timer ---
    static void set_timer_absolute(usec target) { Impl::set_timer_absolute(target); }
    static usec monotonic_time_usecs() { return Impl::monotonic_time_usecs(); }
    
    // --- Critical Section ---
    // Cortex-M: BASEPRI, RISC-V: disable + mask, Xtensa: PS.INTLEVEL
    static void enter_critical() { Impl::enter_critical(); }
    static void exit_critical() { Impl::exit_critical(); }
    
    // --- Multi-Core ---
    static void trigger_ipi(std::uint8_t core_id) { Impl::trigger_ipi(core_id); }
    static std::uint8_t current_core() { return Impl::current_core(); }
    
    // --- Context Switch ---
    // Request deferred switch: triggers PendSV/software interrupt
    static void request_context_switch() { Impl::request_context_switch(); }
    
    // --- FPU ---
    static void save_fpu() { Impl::save_fpu(); }
    static void restore_fpu() { Impl::restore_fpu(); }
    
    // --- Audio ---
    static void mute_audio_dma() { Impl::mute_audio_dma(); }
    
    // --- Persistent Storage ---
    static void write_backup_ram(const void* data, std::size_t bytes) { 
        Impl::write_backup_ram(data, bytes); 
    }
    static void read_backup_ram(void* data, std::size_t bytes) { 
        Impl::read_backup_ram(data, bytes); 
    }
    
    // --- MPU ---
    static void configure_mpu_region(std::size_t idx, const void* base, 
                                     std::size_t bytes, bool writable, bool executable) {
        Impl::configure_mpu_region(idx, base, bytes, writable, executable);
    }
    
    // --- Cache ---
    // Required for DMA coherency on Cortex-A, optional on Cortex-M
    static void cache_clean(const void* addr, std::size_t bytes) { 
        Impl::cache_clean(addr, bytes); 
    }
    static void cache_invalidate(void* addr, std::size_t bytes) { 
        Impl::cache_invalidate(addr, bytes); 
    }
    static void cache_clean_invalidate(void* addr, std::size_t bytes) { 
        Impl::cache_clean_invalidate(addr, bytes); 
    }
    
    // --- System ---
    static void system_reset() { Impl::system_reset(); }
    static void enter_sleep() { Impl::enter_sleep(); }  // WFI/WFE
    [[noreturn]] static void start_first_task() { Impl::start_first_task(); }
    
    // --- Watchdog ---
    static void watchdog_init(std::uint32_t timeout_ms) { Impl::watchdog_init(timeout_ms); }
    static void watchdog_feed() { Impl::watchdog_feed(); }
    
    // --- Performance Counters ---
    static std::uint32_t cycle_count() { return Impl::cycle_count(); }
    static std::uint32_t cycles_per_usec() { return Impl::cycles_per_usec(); }
};

// =====================================================================
// SECTION 3: User Application API - Synchronization Primitives
// =====================================================================
// Lock-free, wait-free primitives for inter-task/ISR communication.

/// RAII critical section guard
template <class HW>
class MaskedCritical {
public:
    MaskedCritical() { HW::enter_critical(); }
    ~MaskedCritical() { HW::exit_critical(); }
    MaskedCritical(const MaskedCritical&) = delete;
    MaskedCritical& operator=(const MaskedCritical&) = delete;
};

/// Lock-free Single Producer Single Consumer queue.
/// Wait-free on both ends, safe for ISR-to-task or task-to-task communication.
///
/// @tparam T Item type (must be trivially copyable)
/// @tparam Capacity Queue size (must be power of 2)
///
/// Use cases: MIDI events, sensor data, commands, log entries
///
/// Example:
/// @code
/// SpscQueue<int, 64> queue;
///
/// // Producer (ISR or task)
/// queue.try_push(42);
///
/// // Consumer (task)
/// if (auto val = queue.try_pop()) {
///     process(*val);
/// }
/// @endcode
template <typename T, std::size_t Capacity>
class SpscQueue {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of 2");
    static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");
    
public:
    constexpr SpscQueue() = default;
    
    // Non-copyable (contains atomics)
    SpscQueue(const SpscQueue&) = delete;
    SpscQueue& operator=(const SpscQueue&) = delete;
    
    // --- Producer API (wait-free) ---
    
    /// Push item. Returns false if queue is full.
    bool try_push(const T& item) {
        const std::size_t write = write_pos_.load(std::memory_order_relaxed);
        const std::size_t read = read_pos_.load(std::memory_order_acquire);
        
        if (((write + 1) & mask()) == read) {
            return false;  // Full
        }
        
        buffer_[write] = item;
        write_pos_.store((write + 1) & mask(), std::memory_order_release);
        return true;
    }
    
    /// Check if queue can accept more items (approximate).
    bool has_space() const {
        const std::size_t write = write_pos_.load(std::memory_order_relaxed);
        const std::size_t read = read_pos_.load(std::memory_order_relaxed);
        return ((write + 1) & mask()) != read;
    }
    
    // --- Consumer API (wait-free) ---
    
    /// Pop item. Returns nullopt if empty.
    std::optional<T> try_pop() {
        const std::size_t read = read_pos_.load(std::memory_order_relaxed);
        const std::size_t write = write_pos_.load(std::memory_order_acquire);
        
        if (read == write) {
            return std::nullopt;  // Empty
        }
        
        T item = buffer_[read];
        read_pos_.store((read + 1) & mask(), std::memory_order_release);
        return item;
    }
    
    /// Peek at front without consuming. Returns nullopt if empty.
    std::optional<T> peek() const {
        const std::size_t read = read_pos_.load(std::memory_order_relaxed);
        const std::size_t write = write_pos_.load(std::memory_order_acquire);
        
        if (read == write) {
            return std::nullopt;
        }
        return buffer_[read];
    }
    
    /// Batch read into span. Returns number of items read.
    std::size_t read_all(std::span<T> dest) {
        std::size_t count = 0;
        while (count < dest.size()) {
            auto item = try_pop();
            if (!item) break;
            dest[count++] = *item;
        }
        return count;
    }
    
    // --- Status (approximate, for diagnostics) ---
    
    std::size_t size_approx() const {
        const std::size_t write = write_pos_.load(std::memory_order_relaxed);
        const std::size_t read = read_pos_.load(std::memory_order_relaxed);
        return (write - read) & mask();
    }
    
    bool empty_approx() const {
        return read_pos_.load(std::memory_order_relaxed) == 
               write_pos_.load(std::memory_order_relaxed);
    }
    
    static constexpr std::size_t capacity() { return Capacity; }

private:
    static constexpr std::size_t mask() { return Capacity - 1; }
    
    std::array<T, Capacity> buffer_ {};
    
    // Cache line separation to avoid false sharing
    alignas(64) std::atomic<std::size_t> write_pos_ {0};
    alignas(64) std::atomic<std::size_t> read_pos_ {0};
};

/// Task notification flags (lightweight event system).
/// Each task has 32 notification bits that can be set/cleared atomically.
/// Note: Individual flag operations are atomic, but task state changes
/// require external synchronization (Masked Critical Section).
template <std::size_t MaxTasks>
struct Notification {
    std::array<std::atomic<std::uint32_t>, MaxTasks> flags {};
    std::array<std::uint32_t, MaxTasks> wait_mask {};
    
    /// Set notification bits for a task (atomic, ISR-safe).
    void notify(TaskId id, std::uint32_t bits) {
        if (!id.valid() || id.value >= MaxTasks) return;
        flags[id.value].fetch_or(bits, std::memory_order_release);
    }
    
    /// Take (consume) notification bits matching mask (atomic).
    std::uint32_t take(TaskId id, std::uint32_t mask) {
        if (!id.valid() || id.value >= MaxTasks) return 0;
        std::uint32_t old = flags[id.value].load(std::memory_order_acquire);
        std::uint32_t v = old & mask;
        // Clear only the matched bits
        while (v != 0 && !flags[id.value].compare_exchange_weak(
            old, old & ~mask, std::memory_order_acq_rel, std::memory_order_acquire)) {
            v = old & mask;
        }
        return v;
    }
    
    /// Peek notification bits without consuming.
    std::uint32_t peek(TaskId id, std::uint32_t mask) const {
        if (!id.valid() || id.value >= MaxTasks) return 0;
        return flags[id.value].load(std::memory_order_acquire) & mask;
    }
    
    void set_wait_mask(TaskId id, std::uint32_t mask) {
        if (id.valid() && id.value < MaxTasks) wait_mask[id.value] = mask;
    }
    
    void clear_wait_mask(TaskId id) {
        if (id.valid() && id.value < MaxTasks) wait_mask[id.value] = 0;
    }
    
    bool should_wake(TaskId id) const {
        if (!id.valid() || id.value >= MaxTasks) return false;
        return (flags[id.value].load(std::memory_order_acquire) & wait_mask[id.value]) != 0;
    }
};

// =====================================================================
// SECTION 4: User Application API - Timing and Measurement
// =====================================================================

/// High-resolution cycle counter stopwatch.
/// Uses hardware cycle counter (DWT on Cortex-M, etc.)
template <class HW>
class Stopwatch {
public:
    void start() { start_cycles_ = HW::cycle_count(); }
    
    std::uint32_t stop() {
        std::uint32_t end = HW::cycle_count();
        elapsed_cycles_ = end - start_cycles_;  // Handles 32-bit wraparound
        return elapsed_cycles_;
    }
    
    std::uint32_t elapsed_cycles() const { return elapsed_cycles_; }
    
    usec elapsed_usecs() const {
        return elapsed_cycles_ / HW::cycles_per_usec();
    }

private:
    std::uint32_t start_cycles_ {0};
    std::uint32_t elapsed_cycles_ {0};
};

/// CPU load monitor with moving average.
/// Tracks processing time vs. available budget (e.g., audio buffer period).
///
/// Usage:
/// @code
/// LoadMonitor<HW, 8> load;  // 8-sample moving average
///
/// void audio_callback() {
///     load.begin();
///     // ... processing ...
///     load.end(budget_cycles);
///
///     auto pct = LoadMonitor<HW>::to_percent(load.instant());
/// }
/// @endcode
template <class HW, std::size_t AvgWindow = 8>
class LoadMonitor {
public:
    /// Call at start of processing.
    void begin() { watch_.start(); }
    
    /// Call at end of processing with budget cycles.
    void end(std::uint32_t budget_cycles) {
        std::uint32_t used = watch_.stop();
        
        // Fixed-point percentage: 0-10000 = 0.00%-100.00%
        std::uint32_t load = (budget_cycles > 0) ? (used * 10000 / budget_cycles) : 0;
        if (load > 10000) load = 10000;
        
        instant_load_ = load;
        if (load > peak_load_) peak_load_ = load;
        
        // Moving average
        avg_sum_ -= avg_buffer_[avg_idx_];
        avg_buffer_[avg_idx_] = load;
        avg_sum_ += load;
        avg_idx_ = (avg_idx_ + 1) % AvgWindow;
        if (avg_count_ < AvgWindow) ++avg_count_;
    }
    
    /// Instant load (0-10000 = 0.00%-100.00%)
    std::uint32_t instant() const { return instant_load_; }
    
    /// Moving average load
    std::uint32_t average() const { 
        return (avg_count_ > 0) ? (avg_sum_ / avg_count_) : 0; 
    }
    
    /// Peak load since last reset
    std::uint32_t peak() const { return peak_load_; }
    
    /// Reset peak
    void reset_peak() { peak_load_ = 0; }
    
    /// Convert fixed-point to float percentage
    static float to_percent(std::uint32_t load) { 
        return static_cast<float>(load) / 100.0f; 
    }

private:
    Stopwatch<HW> watch_ {};
    std::uint32_t instant_load_ {0};
    std::uint32_t peak_load_ {0};
    std::array<std::uint32_t, AvgWindow> avg_buffer_ {};
    std::uint32_t avg_sum_ {0};
    std::size_t avg_idx_ {0};
    std::size_t avg_count_ {0};
};

// =====================================================================
// SECTION 5: User Application API - Timers
// =====================================================================

/// Timer callback function
struct TimerCallback {
    void (*fn)(void*) {nullptr};
    void* ctx {nullptr};
};

/// Delta-encoded timer queue for efficient tickless operation.
template <std::size_t MaxTimers>
class TimerQueue {
public:
    struct Node {
        usec delta {0};
        TimerCallback cb {};
        bool used {false};
        std::optional<std::uint8_t> next {};
    };

    /// Schedule a timer callback.
    bool schedule(usec now, usec target, TimerCallback cb) {
        auto free_idx = find_free();
        if (!free_idx.has_value()) return false;
        nodes[*free_idx].used = true;
        nodes[*free_idx].cb = cb;
        usec rel = (target > now) ? (target - now) : 0;
        insert_delta(*free_idx, rel);
        return true;
    }

    /// Get next expiry time (absolute).
    std::optional<usec> next_expiry(usec now) const {
        if (!head.has_value()) return std::nullopt;
        return now + nodes[*head].delta;
    }

    /// Dispatch due callbacks (call from timer ISR).
    void dispatch_due([[maybe_unused]] usec now) {
        while (head.has_value()) {
            auto idx = *head;
            if (nodes[idx].delta > 0) break;
            auto cb = nodes[idx].cb;
            head = nodes[idx].next;
            nodes[idx] = {};
            if (cb.fn) cb.fn(cb.ctx);
        }
    }

    /// Advance time by elapsed microseconds (for ticked mode).
    void elapse(usec elapsed_us) {
        auto cur = head;
        while (cur.has_value() && elapsed_us > 0) {
            auto& d = nodes[*cur].delta;
            if (elapsed_us >= d) {
                elapsed_us -= d;
                d = 0;
                cur = nodes[*cur].next;
            } else {
                d -= elapsed_us;
                elapsed_us = 0;
            }
        }
    }

private:
    std::optional<std::uint8_t> head {};
    std::array<Node, MaxTimers> nodes {};

    std::optional<std::uint8_t> find_free() {
        for (std::uint8_t i = 0; i < nodes.size(); ++i) {
            if (!nodes[i].used) return i;
        }
        return std::nullopt;
    }

    void insert_delta(std::uint8_t idx, usec rel) {
        if (!head.has_value()) {
            head = idx;
            nodes[idx].delta = rel;
            nodes[idx].next.reset();
            return;
        }
        std::optional<std::uint8_t> cur = head;
        std::optional<std::uint8_t> prev;
        while (cur.has_value()) {
            if (rel < nodes[*cur].delta) {
                nodes[*cur].delta -= rel;
                break;
            }
            rel -= nodes[*cur].delta;
            prev = cur;
            cur = nodes[*cur].next;
        }
        nodes[idx].delta = rel;
        nodes[idx].next = cur;
        if (prev.has_value()) {
            nodes[*prev].next = idx;
        } else {
            head = idx;
        }
    }
};

// =====================================================================
// SECTION 6: Kernel Class
// =====================================================================
// Main kernel object providing task management, scheduling, and system services.

template <std::size_t MaxTasks, std::size_t MaxTimers, class HW, std::size_t MaxCores = 2>
class Kernel {
public:
    static constexpr std::size_t max_cores = MaxCores;
    
    Kernel() = default;

    // -----------------------------------------------------------------
    // User Application API: Task Management
    // -----------------------------------------------------------------
    
    /// Create a new task. Returns invalid TaskId if no slots available.
    /// Task starts in Ready state.
    TaskId create_task(const TaskConfig& cfg) {
        MaskedCritical<HW> guard;
        auto id = allocate_tcb();
        if (!id.valid()) return {};
        auto& t = tasks[id.value];
        t.cfg = cfg;
        t.state = State::Ready;
        return id;
    }

    /// Mark task as ready to run (resume).
    void resume_task(TaskId id) {
        MaskedCritical<HW> guard;
        if (!valid_task(id)) return;
        tasks[id.value].state = State::Ready;
        schedule();
    }
    
    /// Suspend a task (force to blocked state).
    void suspend_task(TaskId id) {
        MaskedCritical<HW> guard;
        if (!valid_task(id)) return;
        tasks[id.value].state = State::Blocked;
        // If suspending current task, trigger reschedule
        auto cur = current_task();
        if (cur.has_value() && cur->value == id.value) {
            schedule();
        }
    }
    
    /// Delete a task and free its slot.
    /// Cannot delete the currently running task.
    /// Returns true if deleted, false if failed.
    bool delete_task(TaskId id) {
        MaskedCritical<HW> guard;
        if (!valid_task(id)) return false;
        
        // Cannot delete currently running task
        auto cur = current_task();
        if (cur.has_value() && cur->value == id.value) {
            return false;
        }
        
        // Clear task state
        tasks[id.value].state = State::Unused;
        tasks[id.value].cfg = {};
        
        // Clear notifications for this task
        notifications.flags[id.value].store(0, std::memory_order_relaxed);
        notifications.wait_mask[id.value] = 0;
        
        return true;
    }
    
    /// @deprecated Use resume_task() instead
    void set_task_ready(TaskId id) { resume_task(id); }
    
    /// Get current task for this core (or specified core).
    std::optional<TaskId> current_task(std::uint8_t core = 0xFF) const {
        if (core == 0xFF) core = HW::current_core();
        if (core >= MaxCores) return std::nullopt;
        return current_per_core[core];
    }

    /// Get task name for debugging/shell.
    const char* get_task_name(TaskId id) const {
        if (!valid_task(id)) return nullptr;
        return tasks[id.value].cfg.name;
    }

    /// Get task priority.
    Priority get_task_priority(TaskId id) const {
        if (!valid_task(id)) return Priority::Idle;
        return tasks[id.value].cfg.prio;
    }

    /// Get task state as string (for debugging).
    const char* get_task_state_str(TaskId id) const {
        if (!id.valid() || id.value >= tasks.size()) return "Invalid";
        switch (tasks[id.value].state) {
            case State::Unused:  return "Unused";
            case State::Ready:   return "Ready";
            case State::Running: return "Running";
            case State::Blocked: return "Blocked";
            default:             return "Unknown";
        }
    }

    /// Iterate over all valid tasks (for shell/debugging).
    template<typename Fn>
    void for_each_task(Fn&& fn) const {
        for (std::uint16_t i = 0; i < tasks.size(); ++i) {
            if (tasks[i].state != State::Unused) {
                TaskId id{i};
                fn(id, tasks[i].cfg, tasks[i].state);
            }
        }
    }

    // -----------------------------------------------------------------
    // User Application API: Task Notification
    // -----------------------------------------------------------------

    /// Notify a task (set bits) and wake if blocked.
    /// Safe to call from any context (ISR or task).
    void notify(TaskId id, std::uint32_t bits) {
        // Atomic flag update (wait-free)
        notifications.notify(id, bits);
        
        // Fast path: check if task is blocked before taking lock
        if (!valid_task(id)) return;
        if (tasks[id.value].state != State::Blocked) return;
        
        // Slow path: task state change requires critical section
        MaskedCritical<HW> guard;
        // Re-check under lock (state may have changed)
        if (tasks[id.value].state == State::Blocked) {
            if (notifications.should_wake(id)) {
                tasks[id.value].state = State::Ready;
                notifications.clear_wait_mask(id);
                schedule();
            }
        }
    }

    /// Non-blocking wait: take flags immediately (for ISR/Realtime tasks).
    /// Wait-free, no critical section needed.
    std::uint32_t wait(TaskId id, std::uint32_t mask) { 
        return notifications.take(id, mask); 
    }

    /// Blocking wait: sleep until flags arrive (for Server/User tasks).
    /// Uses critical section for task state management.
    std::uint32_t wait_block(TaskId id, std::uint32_t mask) {
        // First check without blocking
        auto bits = notifications.peek(id, mask);
        if (bits != 0) {
            return notifications.take(id, mask);
        }
        
        // Need to block - use critical section for state change
        {
            MaskedCritical<HW> guard;
            if (valid_task(id)) {
                notifications.set_wait_mask(id, mask);
                tasks[id.value].state = State::Blocked;
                schedule();
            }
        }
        
        // After wakeup, consume the flags
        return notifications.take(id, mask);
    }

    // -----------------------------------------------------------------
    // User Application API: Timers
    // -----------------------------------------------------------------

    /// Schedule a callback after delay_us microseconds.
    bool call_later(usec delay_us, TimerCallback cb) {
        MaskedCritical<HW> lock;
        usec now = time();
        bool ok = timers.schedule(now, now + delay_us, cb);
        program_next_timer_locked();
        return ok;
    }

    /// Current kernel time in microseconds.
    usec time() const { return time_us; }

    // -----------------------------------------------------------------
    // User Application API: Inter-Core Communication
    // -----------------------------------------------------------------

    /// Send inter-processor interrupt to wake a core.
    void send_ipi(std::uint8_t core_id) { HW::trigger_ipi(core_id); }

    // -----------------------------------------------------------------
    // User Application API: Memory Protection (Optional)
    // -----------------------------------------------------------------

    struct Region {
        const void* base;
        std::size_t bytes;
        bool writable;
        bool executable;
    };

    /// Configure MPU regions.
    void configure_mpu(std::span<const Region> regions) {
        std::size_t idx = 0;
        for (auto& r : regions) {
            HW::configure_mpu_region(idx++, r.base, r.bytes, r.writable, r.executable);
        }
    }

    /// Register shared memory region.
    /// Call during kernel init before starting app.
    bool register_shared(SharedRegionId id, void* base, std::size_t bytes) {
        auto slot = static_cast<std::size_t>(id);
        if (slot >= shared_regions.size()) return false;
        shared_regions[slot] = {base, bytes};
        return true;
    }

    /// Get shared memory region descriptor.
    /// Called by SVC handler for get_shared() syscall.
    SharedRegionDesc get_shared(SharedRegionId id) {
        auto slot = static_cast<std::size_t>(id);
        if (slot >= shared_regions.size()) return {};
        auto& r = shared_regions[slot];
        if (r.base == nullptr || r.bytes == 0) return {};
        
        // Configure MPU to allow app access
        HW::configure_mpu_region(slot, r.base, r.bytes, true, false);
        return {r.base, r.bytes};
    }

    /// Get shared region (legacy span interface)
    std::optional<std::span<std::byte>> shared_region(std::size_t slot) {
        if (slot >= shared_regions.size()) return std::nullopt;
        auto& r = shared_regions[slot];
        if (r.base == nullptr) return std::nullopt;
        return std::span<std::byte>(static_cast<std::byte*>(r.base), r.bytes);
    }

    // -----------------------------------------------------------------
    // User Application API: Error Handling
    // -----------------------------------------------------------------

    /// Panic: mute audio, save crash dump, reset.
    [[noreturn]] void panic(const CrashDump& dump) {
        HW::mute_audio_dma();
        HW::write_backup_ram(&dump, sizeof(dump));
        HW::system_reset();
        while (true) { }
    }

    /// Load crash dump from previous panic.
    CrashDump load_last_crash() const {
        CrashDump d{};
        HW::read_backup_ram(&d, sizeof(d));
        return d;
    }

    // -----------------------------------------------------------------
    // Platform Implementer API: ISR Entry Points
    // -----------------------------------------------------------------

    /// Ticked mode: call periodically with delta microseconds.
    void tick(usec delta_us) {
        time_us += delta_us;
        timers.elapse(delta_us);
        timers.dispatch_due(time_us);
        schedule();
    }

    /// Tickless mode: call from timer interrupt.
    /// Uses hardware monotonic timer for accurate time.
    void on_timer_irq() {
        time_us = HW::monotonic_time_usecs();
        timers.dispatch_due(time_us);
        schedule();
    }
    
    /// Tickless mode (legacy): call with current time.
    /// @deprecated Use on_timer_irq() without arguments.
    void on_timer_irq(usec now) {
        if (now > time_us) time_us = now;
        timers.dispatch_due(time_us);
        schedule();
    }

    // -----------------------------------------------------------------
    // Platform Implementer API: Context Switch Support
    // -----------------------------------------------------------------
    // Called from PendSV or software interrupt handler.
    
    /// Get next task to switch to for this core.
    /// Returns task index, or nullopt if no ready task (enter sleep).
    std::optional<std::uint16_t> get_next_task() {
        auto self_core = HW::current_core();
        std::optional<TaskId> best;
        Priority best_prio = Priority::Idle;
        std::optional<TaskId> first_user;  // First User task found (for round-robin)
        
        auto start_idx = (last_user_idx + 1) % tasks.size();
        
        for (std::uint16_t count = 0; count < tasks.size(); ++count) {
            std::uint16_t i = (start_idx + count) % tasks.size();
            auto& t = tasks[i];
            if (t.state != State::Ready && t.state != State::Running) continue;
            
            // Core affinity check
            if (t.cfg.core_affinity != static_cast<std::uint8_t>(Core::Any) && 
                t.cfg.core_affinity != self_core) continue;
            
            // Higher priority than User always wins
            if (t.cfg.prio < Priority::User) {
                if (!best.has_value() || t.cfg.prio < best_prio) {
                    best_prio = t.cfg.prio;
                    best = TaskId{i};
                }
            }
            // Round-robin for User priority: take first found (starting from last_user_idx+1)
            else if (t.cfg.prio == Priority::User && !first_user.has_value()) {
                first_user = TaskId{i};
            }
            // Idle priority: only if nothing else
            else if (!best.has_value() && !first_user.has_value() && t.cfg.prio == Priority::Idle) {
                best_prio = t.cfg.prio;
                best = TaskId{i};
            }
        }
        
        // User priority wins over Idle, but loses to Realtime
        if (first_user.has_value() && (!best.has_value() || best_prio >= Priority::User)) {
            last_user_idx = first_user->value;
            return first_user->value;
        }
        
        if (!best.has_value()) return std::nullopt;
        return best->value;
    }
    
    /// Prepare context switch to task. Call before/after stack swap.
    void prepare_switch(std::uint16_t next_idx) {
        auto self_core = HW::current_core();
        auto& cur_id = current_per_core[self_core];
        
        if (cur_id.has_value() && cur_id->value == next_idx) return;
        
        auto& next = tasks[next_idx];
        
        // FPU lazy stacking
        if (cur_id.has_value() && tasks[cur_id->value].cfg.uses_fpu) {
            HW::save_fpu();
        }
        if (next.cfg.uses_fpu) {
            HW::restore_fpu();
        }
        
        // State transition
        next.state = State::Running;
        if (cur_id.has_value() && tasks[cur_id->value].state == State::Running) {
            tasks[cur_id->value].state = State::Ready;
        }
        cur_id = TaskId{next_idx};
    }

private:
    // -----------------------------------------------------------------
    // Kernel Internals
    // -----------------------------------------------------------------
    
    enum class State : std::uint8_t { Unused, Ready, Running, Blocked };

    struct Tcb {
        TaskConfig cfg {};
        State state {State::Unused};
    };

    struct SharedRegion {
        void* base {nullptr};
        std::size_t bytes {0};
    };

    usec time_us {0};
    std::uint16_t last_user_idx {0};
    std::array<Tcb, MaxTasks> tasks {};
    std::array<SharedRegion, 8> shared_regions {};
    Notification<MaxTasks> notifications {};
    TimerQueue<MaxTimers> timers {};
    std::array<std::optional<TaskId>, MaxCores> current_per_core {};

    bool valid_task(TaskId id) const { 
        return id.valid() && id.value < tasks.size() && 
               tasks[id.value].state != State::Unused; 
    }

    TaskId allocate_tcb() {
        for (std::uint16_t i = 0; i < tasks.size(); ++i) {
            if (tasks[i].state == State::Unused) {
                tasks[i].state = State::Blocked;
                return TaskId{i};
            }
        }
        return {};
    }

    void schedule() {
        auto next = get_next_task();
        if (next.has_value()) {
            auto self_core = HW::current_core();
            auto& cur = current_per_core[self_core];
            if (!cur.has_value() || cur->value != *next) {
                HW::request_context_switch();
            }
        } else {
            HW::enter_sleep();
        }
    }

    void program_next_timer() {
        MaskedCritical<HW> lock;
        program_next_timer_locked();
    }
    
    void program_next_timer_locked() {
        usec now = time();
        if (auto t = timers.next_expiry(now)) {
            HW::set_timer_absolute(*t);
        }
    }
};

} // namespace umi
