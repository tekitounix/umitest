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

/// FPU usage policy for deterministic context switching
enum class FpuPolicy : std::uint8_t {
    /// FPU usage forbidden (default)
    /// No FPU context save/restore on context switch
    /// Use for: Shell, MIDI handler, UI tasks
    Forbidden = 0,

    /// FPU exclusively owned by this task
    /// Other tasks must be Forbidden - no save needed
    /// Use for: Audio DSP task (single FPU owner)
    Exclusive = 1,

    /// Traditional lazy stacking (for compatibility)
    /// Hardware handles save/restore via LSPACT
    /// Use for: Multiple FPU tasks (rare in audio)
    LazyStack = 2,
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
    FpuPolicy fpu_policy {FpuPolicy::Forbidden};  ///< FPU handling strategy
    const char* name {"<unnamed>"};  ///< Task name for debugging/shell

    /// @deprecated Use fpu_policy instead
    [[deprecated("Use fpu_policy = FpuPolicy::LazyStack instead")]]
    void set_uses_fpu(bool v) { fpu_policy = v ? FpuPolicy::LazyStack : FpuPolicy::Forbidden; }

    /// Check if task uses FPU (for backward compatibility)
    bool uses_fpu() const { return fpu_policy != FpuPolicy::Forbidden; }
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
    static constexpr std::size_t num_priorities = 4;  // Realtime, Server, User, Idle

    /// Task state (public for shell/debug introspection)
    enum class State : std::uint8_t { Unused, Ready, Running, Blocked };

    /// Task control block (public for shell/debug introspection)
    struct Tcb {
        TaskConfig cfg {};
        State state {State::Unused};
        std::uint8_t next {0xFF};  // Next task in same-priority queue (0xFF = end)
    };

    Kernel() = default;

    // -----------------------------------------------------------------
    // User Application API: Task Management
    // -----------------------------------------------------------------
    
    /// Create a new task. Returns invalid TaskId if no slots available.
    /// Task starts in Ready state and is added to bitmap scheduler.
    TaskId create_task(const TaskConfig& cfg) {
        MaskedCritical<HW> guard;
        auto id = allocate_tcb();
        if (!id.valid()) return {};
        auto& t = tasks[id.value];
        t.cfg = cfg;
        t.state = State::Ready;
        t.next = 0xFF;
        // Add to bitmap scheduler
        bitmap_add_ready(id, cfg.prio);
        return id;
    }

    /// Mark task as ready to run (resume).
    void resume_task(TaskId id) {
        MaskedCritical<HW> guard;
        if (!valid_task(id)) return;
        auto s = tasks[id.value].state;
        if (s == State::Ready || s == State::Running) return;  // Already schedulable
        set_ready_with_bitmap(id);
        schedule();
    }

    /// Mark task as ready without triggering schedule.
    /// Use when the caller manages context switch externally (e.g. ISR → PendSV).
    void resume_task_no_schedule(TaskId id) {
        MaskedCritical<HW> guard;
        if (!valid_task(id)) return;
        auto s = tasks[id.value].state;
        if (s == State::Ready || s == State::Running) return;
        set_ready_with_bitmap(id);
    }

    /// Suspend a task (force to blocked state).
    void suspend_task(TaskId id) {
        MaskedCritical<HW> guard;
        if (!valid_task(id)) return;
        if (tasks[id.value].state == State::Blocked) return;  // Already blocked
        set_blocked_with_bitmap(id);
        // If suspending current task, trigger reschedule
        auto cur = current_task();
        if (cur.has_value() && cur->value == id.value) {
            schedule();
        }
    }

    /// Suspend a task without triggering schedule.
    /// Use when the caller manages context switch externally (e.g. task → SVC → PendSV).
    void suspend_task_no_schedule(TaskId id) {
        MaskedCritical<HW> guard;
        if (!valid_task(id)) return;
        if (tasks[id.value].state == State::Blocked) return;
        set_blocked_with_bitmap(id);
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

        // Remove from bitmap scheduler if ready
        auto& t = tasks[id.value];
        if (t.state == State::Ready || t.state == State::Running) {
            bitmap_remove_ready(id, t.cfg.prio);
        }

        // Clear task state
        t.state = State::Unused;
        t.cfg = {};
        t.next = 0xFF;

        // Clear notifications for this task
        notifications.flags[id.value].store(0, std::memory_order_relaxed);
        notifications.wait_mask[id.value] = 0;

        return true;
    }
    
    /// @deprecated Use resume_task() instead
    void set_task_ready(TaskId id) { resume_task(id); }

    /// Yield CPU to next ready task.
    /// For Server/User tasks: may block if no higher priority task ready.
    /// For Realtime tasks: should use wait() instead.
    ///
    /// This triggers a context switch to allow other tasks to run.
    /// Uses fast path (direct switch) in Thread mode, PendSV in ISR context.
    void yield() {
        // Simply request reschedule - actual switching done by scheduler
        schedule();
    }

    // -----------------------------------------------------------------
    // User Application API: FPU Ownership
    // -----------------------------------------------------------------

    /// Set exclusive FPU owner (call during init, before starting tasks).
    /// Only one task can own FPU exclusively.
    /// All other tasks should use FpuPolicy::Forbidden.
    /// Returns true if ownership granted, false if already owned.
    bool set_fpu_owner(TaskId id) {
        if (fpu_owner_.has_value()) return false;
        if (!valid_task(id)) return false;
        if (tasks[id.value].cfg.fpu_policy != FpuPolicy::Exclusive) return false;
        fpu_owner_ = id;
        return true;
    }

    /// Check if task is the FPU owner.
    bool is_fpu_owner(TaskId id) const {
        return fpu_owner_.has_value() && fpu_owner_->value == id.value;
    }

    /// Get FPU owner (if any).
    std::optional<TaskId> get_fpu_owner() const {
        return fpu_owner_;
    }

    // -----------------------------------------------------------------
    // User Application API: Tickless Power Management
    // -----------------------------------------------------------------

    /// Set audio activity status for power management.
    /// When audio is active, only light sleep (WFI) is used.
    /// When audio is inactive, deeper sleep modes are allowed.
    void set_audio_active(bool active) {
        audio_active_ = active;
    }

    /// Check if audio is currently active.
    bool is_audio_active() const {
        return audio_active_;
    }

    /// Get next timer expiry time (for tickless idle).
    /// Returns UINT64_MAX if no timers pending.
    usec get_next_wakeup() const {
        auto now = time_us;
        if (auto t = timers.next_expiry(now)) {
            return *t;
        }
        return UINT64_MAX;
    }

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
    /// O(1) bitmap-based implementation using __builtin_ctz.
    std::optional<std::uint16_t> get_next_task() {
        auto self_core = HW::current_core();

        // O(1): Load bitmap and find highest priority with ready tasks
        auto bitmap = ready_bitmap_.load(std::memory_order_acquire);
        if (bitmap == 0) {
            return std::nullopt;  // No ready tasks
        }

        // __builtin_ctz: Count Trailing Zeros - finds lowest set bit (highest priority)
        // Priority 0 (Realtime) = bit 0, Priority 3 (Idle) = bit 3
        auto highest_prio = static_cast<std::size_t>(__builtin_ctz(bitmap));
        auto& queue = priority_queues_[highest_prio];

        // Get head of queue for this priority
        if (queue.head == 0xFF) {
            // Bitmap inconsistency - fall back to linear search
            return get_next_task_fallback(self_core);
        }

        auto task_idx = queue.head;
        auto& t = tasks[task_idx];

        // Core affinity check
        if (t.cfg.core_affinity != static_cast<std::uint8_t>(Core::Any) &&
            t.cfg.core_affinity != self_core) {
            // Task not eligible for this core - scan queue for next eligible
            std::uint8_t cur = task_idx;
            while (cur != 0xFF) {
                auto& task = tasks[cur];
                if (task.cfg.core_affinity == static_cast<std::uint8_t>(Core::Any) ||
                    task.cfg.core_affinity == self_core) {
                    return cur;
                }
                cur = task.next;
            }
            // No eligible task at this priority, try lower priorities
            for (std::size_t prio = highest_prio + 1; prio < num_priorities; ++prio) {
                if ((bitmap & (1u << prio)) == 0) continue;
                auto& q = priority_queues_[prio];
                cur = q.head;
                while (cur != 0xFF) {
                    auto& task = tasks[cur];
                    if (task.cfg.core_affinity == static_cast<std::uint8_t>(Core::Any) ||
                        task.cfg.core_affinity == self_core) {
                        return cur;
                    }
                    cur = task.next;
                }
            }
            return std::nullopt;
        }

        // For User priority, implement round-robin by rotating queue after selection
        if (highest_prio == static_cast<std::size_t>(Priority::User) && queue.count > 1) {
            last_user_idx = task_idx;
        }

        return task_idx;
    }

    /// Fallback O(n) scheduler for edge cases (bitmap inconsistency)
    std::optional<std::uint16_t> get_next_task_fallback(std::uint8_t self_core) {
        std::optional<TaskId> best;
        Priority best_prio = Priority::Idle;

        for (std::uint16_t i = 0; i < tasks.size(); ++i) {
            auto& t = tasks[i];
            if (t.state != State::Ready && t.state != State::Running) continue;
            if (t.cfg.core_affinity != static_cast<std::uint8_t>(Core::Any) &&
                t.cfg.core_affinity != self_core) continue;

            if (!best.has_value() || t.cfg.prio < best_prio) {
                best_prio = t.cfg.prio;
                best = TaskId{i};
            }
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

        // FPU context handling based on policy
        // Exclusive mode: Only one task owns FPU, no save/restore needed
        // LazyStack mode: Hardware handles via LSPACT
        // Forbidden mode: No FPU operations
        if (cur_id.has_value()) {
            auto cur_policy = tasks[cur_id->value].cfg.fpu_policy;
            auto next_policy = next.cfg.fpu_policy;

            // Only save/restore if both tasks use lazy stacking
            // Exclusive owner never needs save (others are Forbidden)
            if (cur_policy == FpuPolicy::LazyStack) {
                HW::save_fpu();
            }
            if (next_policy == FpuPolicy::LazyStack) {
                HW::restore_fpu();
            }
            // Exclusive mode: no action needed
            // The exclusive owner's FPU state remains in registers
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

    struct SharedRegion {
        void* base {nullptr};
        std::size_t bytes {0};
    };

    // Per-priority ready queue for O(1) scheduler
    struct PriorityQueue {
        std::uint8_t head {0xFF};   // First ready task (0xFF = empty)
        std::uint8_t tail {0xFF};   // Last ready task for O(1) enqueue
        std::uint8_t count {0};     // Number of tasks in queue
    };

    usec time_us {0};
    std::uint16_t last_user_idx {0};
    std::array<Tcb, MaxTasks> tasks {};
    std::array<SharedRegion, 8> shared_regions {};
    Notification<MaxTasks> notifications {};
    TimerQueue<MaxTimers> timers {};
    std::array<std::optional<TaskId>, MaxCores> current_per_core {};

    // O(1) Bitmap scheduler infrastructure
    std::atomic<std::uint8_t> ready_bitmap_ {0};  // Bit per priority level
    std::array<PriorityQueue, num_priorities> priority_queues_ {};

    // FPU exclusive owner (for Exclusive policy)
    std::optional<TaskId> fpu_owner_ {};

    // Tickless power management state
    bool audio_active_ {false};  // True when audio DMA is running

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

    // -----------------------------------------------------------------
    // O(1) Bitmap Scheduler Helpers
    // -----------------------------------------------------------------

    // Add task to priority queue and update bitmap
    void bitmap_add_ready(TaskId id, Priority prio) {
        auto prio_idx = static_cast<std::size_t>(prio);
        auto& queue = priority_queues_[prio_idx];
        auto task_idx = static_cast<std::uint8_t>(id.value);

        // Add to tail of queue
        tasks[task_idx].next = 0xFF;
        if (queue.tail != 0xFF) {
            tasks[queue.tail].next = task_idx;
        } else {
            queue.head = task_idx;
        }
        queue.tail = task_idx;
        queue.count++;

        // Set priority bit in bitmap
        ready_bitmap_.fetch_or(1u << prio_idx, std::memory_order_release);
    }

    // Remove task from priority queue and update bitmap if empty
    void bitmap_remove_ready(TaskId id, Priority prio) {
        auto prio_idx = static_cast<std::size_t>(prio);
        auto& queue = priority_queues_[prio_idx];
        auto task_idx = static_cast<std::uint8_t>(id.value);

        // Find and remove from queue
        std::uint8_t prev = 0xFF;
        std::uint8_t cur = queue.head;
        while (cur != 0xFF) {
            if (cur == task_idx) {
                // Found - unlink
                if (prev != 0xFF) {
                    tasks[prev].next = tasks[cur].next;
                } else {
                    queue.head = tasks[cur].next;
                }
                if (queue.tail == cur) {
                    queue.tail = prev;
                }
                tasks[cur].next = 0xFF;
                queue.count--;
                break;
            }
            prev = cur;
            cur = tasks[cur].next;
        }

        // Clear bitmap bit if queue is now empty
        if (queue.count == 0) {
            ready_bitmap_.fetch_and(~(1u << prio_idx), std::memory_order_release);
        }
    }

    // Set task to Ready state with bitmap update
    void set_ready_with_bitmap(TaskId id) {
        if (!valid_task(id)) return;
        auto& t = tasks[id.value];
        if (t.state == State::Ready) return;  // Already ready
        t.state = State::Ready;
        bitmap_add_ready(id, t.cfg.prio);
    }

    // Set task to Blocked state with bitmap update
    void set_blocked_with_bitmap(TaskId id) {
        if (!valid_task(id)) return;
        auto& t = tasks[id.value];
        if (t.state == State::Blocked) return;  // Already blocked
        Priority prio = t.cfg.prio;
        if (t.state == State::Ready || t.state == State::Running) {
            bitmap_remove_ready(id, prio);
        }
        t.state = State::Blocked;
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
