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
// =====================================================================

namespace umi {

// =====================================================================
// SECTION 1: Core Types
// =====================================================================

/// Microseconds (64-bit for long uptime)
using usec = std::uint64_t;

/// Task priority levels (lower value = higher priority)
enum class Priority : std::uint8_t {
    Realtime = 0,  ///< Audio processing, DMA callbacks - highest
    Server   = 1,  ///< Drivers, I/O handlers
    User     = 2,  ///< Application tasks - round-robin among same priority
    Idle     = 3,  ///< Background, sleep management - lowest
};

/// Core affinity
enum class Core : std::uint8_t {
    Any = 0xFF,  ///< Can run on any core
};

/// FPU usage policy for deterministic context switching
enum class FpuPolicy : std::uint8_t {
    /// FPU usage forbidden (default)
    Forbidden = 0,

    /// Traditional lazy stacking (for compatibility)
    /// Hardware handles save/restore via LSPACT
    LazyStack = 2,
};

/// Task identifier (opaque handle)
struct TaskId {
    std::uint16_t value {0xFFFF};
    constexpr bool valid() const { return value != 0xFFFF; }
};

/// Task configuration for create_task()
struct TaskConfig {
    void (*entry)(void*) {nullptr};  ///< Task entry point
    void* arg {nullptr};             ///< Argument passed to entry
    Priority prio {Priority::Idle};  ///< Task priority
    std::uint8_t core_affinity {static_cast<std::uint8_t>(Core::Any)};
    FpuPolicy fpu_policy {FpuPolicy::Forbidden};  ///< FPU handling strategy
    const char* name {"<unnamed>"};  ///< Task name for debugging/shell

    /// Check if task uses FPU
    bool uses_fpu() const { return fpu_policy != FpuPolicy::Forbidden; }
};

/// Event bits for wait() syscall
namespace KernelEvent {
    constexpr std::uint32_t AudioReady = 1 << 0;
    constexpr std::uint32_t MidiReady  = 1 << 1;
    constexpr std::uint32_t VSync      = 1 << 2;
}

// =====================================================================
// SECTION 2: Platform Implementer API (Hardware Abstraction)
// =====================================================================

/// Hardware abstraction layer template.
/// Platform must provide a class implementing all static methods.
template <class Impl>
struct Hw {
    // --- Timer ---
    static void set_timer_absolute(usec target) { Impl::set_timer_absolute(target); }
    static usec monotonic_time_usecs() { return Impl::monotonic_time_usecs(); }

    // --- Critical Section ---
    static void enter_critical() { Impl::enter_critical(); }
    static void exit_critical() { Impl::exit_critical(); }

    // --- Multi-Core ---
    static void trigger_ipi(std::uint8_t core_id) { Impl::trigger_ipi(core_id); }
    static std::uint8_t current_core() { return Impl::current_core(); }

    // --- Context Switch ---
    static void request_context_switch() { Impl::request_context_switch(); }

    // --- Power ---
    static void enter_sleep() { Impl::enter_sleep(); }

    // --- Performance Counters ---
    static std::uint32_t cycle_count() { return Impl::cycle_count(); }
    static std::uint32_t cycles_per_usec() { return Impl::cycles_per_usec(); }
};

// =====================================================================
// SECTION 3: Synchronization Primitives
// =====================================================================

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
template <typename T, std::size_t Capacity>
class SpscQueue {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of 2");
    static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");

public:
    constexpr SpscQueue() = default;

    SpscQueue(const SpscQueue&) = delete;
    SpscQueue& operator=(const SpscQueue&) = delete;

    bool try_push(const T& item) {
        const std::size_t write = write_pos_.load(std::memory_order_relaxed);
        const std::size_t read = read_pos_.load(std::memory_order_acquire);

        if (((write + 1) & mask()) == read) {
            return false;
        }

        buffer_[write] = item;
        write_pos_.store((write + 1) & mask(), std::memory_order_release);
        return true;
    }

    bool has_space() const {
        const std::size_t write = write_pos_.load(std::memory_order_relaxed);
        const std::size_t read = read_pos_.load(std::memory_order_relaxed);
        return ((write + 1) & mask()) != read;
    }

    std::optional<T> try_pop() {
        const std::size_t read = read_pos_.load(std::memory_order_relaxed);
        const std::size_t write = write_pos_.load(std::memory_order_acquire);

        if (read == write) {
            return std::nullopt;
        }

        T item = buffer_[read];
        read_pos_.store((read + 1) & mask(), std::memory_order_release);
        return item;
    }

    std::optional<T> peek() const {
        const std::size_t read = read_pos_.load(std::memory_order_relaxed);
        const std::size_t write = write_pos_.load(std::memory_order_acquire);

        if (read == write) {
            return std::nullopt;
        }
        return buffer_[read];
    }

    std::size_t read_all(std::span<T> dest) {
        std::size_t count = 0;
        while (count < dest.size()) {
            auto item = try_pop();
            if (!item) break;
            dest[count++] = *item;
        }
        return count;
    }

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
template <std::size_t MaxTasks>
struct Notification {
    std::array<std::atomic<std::uint32_t>, MaxTasks> flags {};
    std::array<std::uint32_t, MaxTasks> wait_mask {};

    void notify(TaskId id, std::uint32_t bits) {
        if (!id.valid() || id.value >= MaxTasks) return;
        flags[id.value].fetch_or(bits, std::memory_order_release);
    }

    std::uint32_t take(TaskId id, std::uint32_t mask) {
        if (!id.valid() || id.value >= MaxTasks) return 0;
        std::uint32_t old = flags[id.value].load(std::memory_order_acquire);
        std::uint32_t v = old & mask;
        while (v != 0 && !flags[id.value].compare_exchange_weak(
            old, old & ~mask, std::memory_order_acq_rel, std::memory_order_acquire)) {
            v = old & mask;
        }
        return v;
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
// SECTION 4: Timing and Measurement
// =====================================================================

/// High-resolution cycle counter stopwatch.
template <class HW>
class Stopwatch {
public:
    void start() { start_cycles_ = HW::cycle_count(); }

    std::uint32_t stop() {
        std::uint32_t end = HW::cycle_count();
        elapsed_cycles_ = end - start_cycles_;
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
template <class HW, std::size_t AvgWindow = 8>
class LoadMonitor {
public:
    void begin() { watch_.start(); }

    void end(std::uint32_t budget_cycles) {
        std::uint32_t used = watch_.stop();

        std::uint32_t load = (budget_cycles > 0) ? (used * 10000 / budget_cycles) : 0;
        if (load > 10000) load = 10000;

        instant_load_ = load;
        if (load > peak_load_) peak_load_ = load;

        avg_sum_ -= avg_buffer_[avg_idx_];
        avg_buffer_[avg_idx_] = load;
        avg_sum_ += load;
        avg_idx_ = (avg_idx_ + 1) % AvgWindow;
        if (avg_count_ < AvgWindow) ++avg_count_;
    }

    std::uint32_t instant() const { return instant_load_; }
    std::uint32_t average() const {
        return (avg_count_ > 0) ? (avg_sum_ / avg_count_) : 0;
    }
    std::uint32_t peak() const { return peak_load_; }
    void reset_peak() { peak_load_ = 0; }

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
// SECTION 5: Timers
// =====================================================================

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

    bool schedule(usec now, usec target, TimerCallback cb) {
        auto free_idx = find_free();
        if (!free_idx.has_value()) return false;
        nodes[*free_idx].used = true;
        nodes[*free_idx].cb = cb;
        usec rel = (target > now) ? (target - now) : 0;
        insert_delta(*free_idx, rel);
        return true;
    }

    std::optional<usec> next_expiry(usec now) const {
        if (!head.has_value()) return std::nullopt;
        return now + nodes[*head].delta;
    }

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

template <std::size_t MaxTasks, std::size_t MaxTimers, class HW, std::size_t MaxCores = 2>
class Kernel {
public:
    static constexpr std::size_t max_cores = MaxCores;
    static constexpr std::size_t num_priorities = 4;

    enum class State : std::uint8_t { Unused, Ready, Running, Blocked };

    struct Tcb {
        TaskConfig cfg {};
        State state {State::Unused};
        std::uint8_t next {0xFF};
    };

    Kernel() = default;

    // -----------------------------------------------------------------
    // Task Management
    // -----------------------------------------------------------------

    TaskId create_task(const TaskConfig& cfg) {
        MaskedCritical<HW> guard;
        auto id = allocate_tcb();
        if (!id.valid()) return {};
        auto& t = tasks[id.value];
        t.cfg = cfg;
        t.state = State::Ready;
        t.next = 0xFF;
        bitmap_add_ready(id, cfg.prio);
        return id;
    }

    void resume_task(TaskId id) {
        MaskedCritical<HW> guard;
        if (!valid_task(id)) return;
        auto s = tasks[id.value].state;
        if (s == State::Ready || s == State::Running) return;
        set_ready_with_bitmap(id);
        schedule();
    }

    void suspend_task(TaskId id) {
        MaskedCritical<HW> guard;
        if (!valid_task(id)) return;
        if (tasks[id.value].state == State::Blocked) return;
        set_blocked_with_bitmap(id);
        auto cur = current_task();
        if (cur.has_value() && cur->value == id.value) {
            schedule();
        }
    }

    bool delete_task(TaskId id) {
        MaskedCritical<HW> guard;
        if (!valid_task(id)) return false;

        auto cur = current_task();
        if (cur.has_value() && cur->value == id.value) {
            return false;
        }

        auto& t = tasks[id.value];
        if (t.state == State::Ready || t.state == State::Running) {
            bitmap_remove_ready(id, t.cfg.prio);
        }

        t.state = State::Unused;
        t.cfg = {};
        t.next = 0xFF;

        notifications.flags[id.value].store(0, std::memory_order_relaxed);
        notifications.wait_mask[id.value] = 0;

        return true;
    }

    void yield() {
        schedule();
    }

    // -----------------------------------------------------------------
    // Task Notification
    // -----------------------------------------------------------------

    /// Notify a task (set bits) and wake if blocked.
    /// Safe to call from any context (ISR or task).
    void notify(TaskId id, std::uint32_t bits) {
        notifications.notify(id, bits);

        if (!valid_task(id)) return;
        if (tasks[id.value].state != State::Blocked) return;

        MaskedCritical<HW> guard;
        if (tasks[id.value].state == State::Blocked) {
            if (notifications.should_wake(id)) {
                tasks[id.value].state = State::Ready;
                notifications.clear_wait_mask(id);
                bitmap_add_ready(id, tasks[id.value].cfg.prio);
                schedule();
            }
        }
    }

    /// Non-blocking wait: take flags immediately.
    std::uint32_t wait(TaskId id, std::uint32_t mask) {
        return notifications.take(id, mask);
    }

    /// Blocking wait: atomically consume flags or block until notified.
    /// Fixed: take+block in single critical section prevents starvation.
    std::uint32_t wait_block(TaskId id, std::uint32_t mask) {
        {
            MaskedCritical<HW> guard;
            auto bits = notifications.take(id, mask);
            if (bits != 0) {
                return bits;
            }
            // No flags — block
            if (valid_task(id)) {
                notifications.set_wait_mask(id, mask);
                set_blocked_with_bitmap(id);
                schedule();
            }
        }
        // After wakeup, consume the flags
        return notifications.take(id, mask);
    }

    // -----------------------------------------------------------------
    // Timers
    // -----------------------------------------------------------------

    bool call_later(usec delay_us, TimerCallback cb) {
        MaskedCritical<HW> lock;
        usec now = time();
        bool ok = timers.schedule(now, now + delay_us, cb);
        program_next_timer_locked();
        return ok;
    }

    usec time() const { return time_us; }

    // -----------------------------------------------------------------
    // Inter-Core Communication
    // -----------------------------------------------------------------

    void send_ipi(std::uint8_t core_id) { HW::trigger_ipi(core_id); }

    // -----------------------------------------------------------------
    // Query / Debug
    // -----------------------------------------------------------------

    std::optional<TaskId> current_task(std::uint8_t core = 0xFF) const {
        if (core == 0xFF) core = HW::current_core();
        if (core >= MaxCores) return std::nullopt;
        return current_per_core[core];
    }

    const char* get_task_name(TaskId id) const {
        if (!valid_task(id)) return nullptr;
        return tasks[id.value].cfg.name;
    }

    Priority get_task_priority(TaskId id) const {
        if (!valid_task(id)) return Priority::Idle;
        return tasks[id.value].cfg.prio;
    }

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
    // Platform Implementer API: ISR Entry Points
    // -----------------------------------------------------------------

    void tick(usec delta_us) {
        time_us += delta_us;
        timers.elapse(delta_us);
        timers.dispatch_due(time_us);
        schedule();
    }

    void on_timer_irq() {
        time_us = HW::monotonic_time_usecs();
        timers.dispatch_due(time_us);
        schedule();
    }

    // -----------------------------------------------------------------
    // Platform Implementer API: Context Switch Support
    // -----------------------------------------------------------------

    /// O(1) bitmap-based next task selection.
    std::optional<std::uint16_t> get_next_task() {
        auto self_core = HW::current_core();

        auto bitmap = ready_bitmap_.load(std::memory_order_acquire);
        if (bitmap == 0) {
            return std::nullopt;
        }

        auto highest_prio = static_cast<std::size_t>(__builtin_ctz(bitmap));
        auto& queue = priority_queues_[highest_prio];

        if (queue.head == 0xFF) {
            return get_next_task_fallback(self_core);
        }

        auto task_idx = queue.head;
        auto& t = tasks[task_idx];

        // Core affinity check
        if (t.cfg.core_affinity != static_cast<std::uint8_t>(Core::Any) &&
            t.cfg.core_affinity != self_core) {
            std::uint8_t cur = task_idx;
            while (cur != 0xFF) {
                auto& task = tasks[cur];
                if (task.cfg.core_affinity == static_cast<std::uint8_t>(Core::Any) ||
                    task.cfg.core_affinity == self_core) {
                    return cur;
                }
                cur = task.next;
            }
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

        if (highest_prio == static_cast<std::size_t>(Priority::User) && queue.count > 1) {
            last_user_idx = task_idx;
        }

        return task_idx;
    }

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

    void prepare_switch(std::uint16_t next_idx) {
        auto self_core = HW::current_core();
        auto& cur_id = current_per_core[self_core];

        if (cur_id.has_value() && cur_id->value == next_idx) return;

        auto& next = tasks[next_idx];

        next.state = State::Running;
        if (cur_id.has_value() && tasks[cur_id->value].state == State::Running) {
            tasks[cur_id->value].state = State::Ready;
        }
        cur_id = TaskId{next_idx};
    }

private:
    // Per-priority ready queue for O(1) scheduler
    struct PriorityQueue {
        std::uint8_t head {0xFF};
        std::uint8_t tail {0xFF};
        std::uint8_t count {0};
    };

    usec time_us {0};
    std::uint16_t last_user_idx {0};
    std::array<Tcb, MaxTasks> tasks {};
    Notification<MaxTasks> notifications {};
    TimerQueue<MaxTimers> timers {};
    std::array<std::optional<TaskId>, MaxCores> current_per_core {};

    std::atomic<std::uint8_t> ready_bitmap_ {0};
    std::array<PriorityQueue, num_priorities> priority_queues_ {};

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

    void bitmap_add_ready(TaskId id, Priority prio) {
        auto prio_idx = static_cast<std::size_t>(prio);
        auto& queue = priority_queues_[prio_idx];
        auto task_idx = static_cast<std::uint8_t>(id.value);

        tasks[task_idx].next = 0xFF;
        if (queue.tail != 0xFF) {
            tasks[queue.tail].next = task_idx;
        } else {
            queue.head = task_idx;
        }
        queue.tail = task_idx;
        queue.count++;

        ready_bitmap_.fetch_or(1u << prio_idx, std::memory_order_release);
    }

    void bitmap_remove_ready(TaskId id, Priority prio) {
        auto prio_idx = static_cast<std::size_t>(prio);
        auto& queue = priority_queues_[prio_idx];
        auto task_idx = static_cast<std::uint8_t>(id.value);

        std::uint8_t prev = 0xFF;
        std::uint8_t cur = queue.head;
        while (cur != 0xFF) {
            if (cur == task_idx) {
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

        if (queue.count == 0) {
            ready_bitmap_.fetch_and(~(1u << prio_idx), std::memory_order_release);
        }
    }

    void set_ready_with_bitmap(TaskId id) {
        if (!valid_task(id)) return;
        auto& t = tasks[id.value];
        if (t.state == State::Ready) return;
        t.state = State::Ready;
        bitmap_add_ready(id, t.cfg.prio);
    }

    void set_blocked_with_bitmap(TaskId id) {
        if (!valid_task(id)) return;
        auto& t = tasks[id.value];
        if (t.state == State::Blocked) return;
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

    void program_next_timer_locked() {
        usec now = time();
        if (auto t = timers.next_expiry(now)) {
            HW::set_timer_absolute(*t);
        }
    }
};

} // namespace umi
