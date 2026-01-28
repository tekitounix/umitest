#pragma once
// =====================================================================
// UMI Coroutine Runtime
// =====================================================================
//
// Lightweight coroutine runtime for application-level async operations.
// C++20 coroutines with minimal overhead.
//
// Features:
//   - Task<T>: coroutine return type
//   - Scheduler: cooperative scheduler for multiple coroutines
//   - Awaiters: wait_event(), sleep(), yield()
//   - Chrono literals: co_await 10ms, co_await 1s
//
// =====================================================================

#include <coroutine>
#include <cstdint>
#include <optional>
#include <array>
#include <chrono>

namespace umi::coro {

// =====================================================================
// Forward Declarations
// =====================================================================

template <typename T = void>
class Task;

template <std::size_t MaxTasks = 8>
class Scheduler;

template <std::size_t MaxTasks>
class SchedulerContext;

// =====================================================================
// Task Promise (void specialization)
// =====================================================================

template <>
class Task<void> {
public:
    struct promise_type {
        Task get_return_object() {
            return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        
        std::suspend_always initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        void return_void() noexcept {}
        void unhandled_exception() noexcept { /* embedded: no exceptions */ }
        
        // For scheduler to chain
        std::coroutine_handle<> continuation{};
    };
    
    using handle_type = std::coroutine_handle<promise_type>;
    
    Task() = default;
    explicit Task(handle_type h) : handle_(h) {}
    
    Task(Task&& other) noexcept : handle_(other.handle_) {
        other.handle_ = nullptr;
    }
    
    Task& operator=(Task&& other) noexcept {
        if (this != &other) {
            if (handle_) handle_.destroy();
            handle_ = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }
    
    ~Task() {
        if (handle_) handle_.destroy();
    }
    
    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;
    
    handle_type handle() const { return handle_; }
    bool done() const { return !handle_ || handle_.done(); }
    void resume() { if (handle_ && !handle_.done()) handle_.resume(); }
    
    // Awaitable: co_await task
    bool await_ready() const noexcept { return done(); }
    
    void await_suspend(std::coroutine_handle<> awaiter) noexcept {
        handle_.promise().continuation = awaiter;
    }
    
    void await_resume() noexcept {}

private:
    handle_type handle_{};
};

// =====================================================================
// Task Promise (value-returning)
// =====================================================================

template <typename T>
class Task {
public:
    struct promise_type {
        std::optional<T> value;
        
        Task get_return_object() {
            return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        
        std::suspend_always initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        
        void return_value(T v) noexcept { value = std::move(v); }
        void unhandled_exception() noexcept {}
        
        std::coroutine_handle<> continuation{};
    };
    
    using handle_type = std::coroutine_handle<promise_type>;
    
    Task() = default;
    explicit Task(handle_type h) : handle_(h) {}
    
    Task(Task&& other) noexcept : handle_(other.handle_) {
        other.handle_ = nullptr;
    }
    
    ~Task() {
        if (handle_) handle_.destroy();
    }
    
    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;
    
    handle_type handle() const { return handle_; }
    bool done() const { return !handle_ || handle_.done(); }
    void resume() { if (handle_ && !handle_.done()) handle_.resume(); }
    
    bool await_ready() const noexcept { return done(); }
    
    void await_suspend(std::coroutine_handle<> awaiter) noexcept {
        handle_.promise().continuation = awaiter;
    }
    
    T await_resume() noexcept {
        return std::move(*handle_.promise().value);
    }

private:
    handle_type handle_{};
};

// =====================================================================
// Event Awaiter
// =====================================================================

// Awaiter for kernel events (uses syscall internally)
class WaitEvent {
public:
    explicit WaitEvent(std::uint32_t mask) : mask_(mask) {}
    
    bool await_ready() const noexcept { return false; }
    
    void await_suspend(std::coroutine_handle<> h) noexcept {
        handle_ = h;
        // Will be resumed by scheduler after wait() returns
    }
    
    std::uint32_t await_resume() noexcept { return result_; }
    
    // Called by scheduler
    void set_result(std::uint32_t r) { result_ = r; }
    std::uint32_t mask() const { return mask_; }
    std::coroutine_handle<> handle() const { return handle_; }

private:
    std::uint32_t mask_;
    std::uint32_t result_{0};
    std::coroutine_handle<> handle_{};
};

// =====================================================================
// Yield Awaiter
// =====================================================================

struct Yield {
    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<>) noexcept {}
    void await_resume() noexcept {}
};

inline Yield yield() { return {}; }

// =====================================================================
// Sleep Awaiter
// =====================================================================
//
// Non-blocking sleep using chrono duration.
// Usage:
//   co_await sleep(100ms);
//   co_await sleep(1s);
//   co_await 50ms;  // shorthand via operator co_await
//
// =====================================================================

/// Microseconds type (matches umi_kernel.hh)
using usec = std::uint64_t;

// =====================================================================
// Global Scheduler Interface
// =====================================================================
// For co_await 16ms syntax without explicit context

namespace detail {
    /// Function pointer type for registering sleep
    using RegisterSleepFn = void (*)(std::coroutine_handle<> h, usec duration_us);

    /// Global sleep registration function (set by scheduler)
    inline RegisterSleepFn g_register_sleep = nullptr;
}

/// Set global sleep registration function (called by scheduler)
inline void set_global_scheduler(detail::RegisterSleepFn fn) {
    detail::g_register_sleep = fn;
}

/// Sleep awaiter - suspends coroutine for specified duration
/// Uses global scheduler when available (for co_await 16ms syntax)
class SleepAwaiter {
public:
    explicit SleepAwaiter(usec duration_us) : duration_us_(duration_us) {}

    template <typename Rep, typename Period>
    explicit SleepAwaiter(std::chrono::duration<Rep, Period> duration)
        : duration_us_(std::chrono::duration_cast<std::chrono::microseconds>(duration).count()) {}

    bool await_ready() const noexcept { return duration_us_ == 0; }

    void await_suspend(std::coroutine_handle<> h) noexcept {
        handle_ = h;
        // Use global scheduler if available
        if (detail::g_register_sleep != nullptr) {
            detail::g_register_sleep(h, duration_us_);
        }
    }

    void await_resume() noexcept {}

    usec duration_us() const { return duration_us_; }
    std::coroutine_handle<> handle() const { return handle_; }

private:
    usec duration_us_;
    std::coroutine_handle<> handle_{};
};

/// Create sleep awaiter from duration
template <typename Rep, typename Period>
inline SleepAwaiter sleep(std::chrono::duration<Rep, Period> duration) {
    return SleepAwaiter{duration};
}

/// Create sleep awaiter from microseconds
inline SleepAwaiter sleep_us(usec us) {
    return SleepAwaiter{us};
}

/// Enable: co_await 100ms; (operator co_await for chrono durations)
template <typename Rep, typename Period>
inline SleepAwaiter operator co_await(std::chrono::duration<Rep, Period> duration) {
    return SleepAwaiter{duration};
}

// =====================================================================
// Scheduler
// =====================================================================
//
// Lightweight cooperative scheduler for coroutines within a single RTOS task.
//
// Design:
//   - App runs as ONE kernel task
//   - Coroutines share that task cooperatively
//   - Supports both event waits and timed sleeps
//   - Kernel wait() blocks the entire app task (kernel handles WFI)
//
// =====================================================================

template <std::size_t MaxTasks>
class Scheduler {
public:
    /// Kernel syscall: blocks until event fires or timeout
    using WaitFn = std::uint32_t (*)(std::uint32_t mask, usec timeout_us);
    
    /// Get current time in microseconds
    using TimeFn = usec (*)();
    
    Scheduler(WaitFn wait_fn, TimeFn time_fn)
        : wait_fn_(wait_fn), time_fn_(time_fn) {
        // Register global sleep function for co_await 16ms syntax
        set_global_scheduler([](std::coroutine_handle<> h, usec duration_us) {
            // This lambda captures nothing - it uses the global instance
            // The actual registration happens via the static instance pointer
            if (s_instance != nullptr) {
                s_instance->register_sleep(h, duration_us);
            }
        });
        s_instance = this;
    }
    
    /// Spawn a coroutine
    bool spawn(Task<void>&& task) {
        for (auto& slot : tasks_) {
            if (!slot.handle) {
                slot.handle = task.handle().address();
                slot.wait_mask = 0;
                slot.sleep_deadline = 0;
                task = Task<void>{};
                return true;
            }
        }
        return false;
    }
    
    /// Main loop - runs until all coroutines complete
    [[noreturn]] void run() {
        while (true) {
            usec now = time_fn_();
            
            // Wake up sleeping coroutines whose deadline has passed
            for (auto& slot : tasks_) {
                if (slot.handle && slot.sleep_deadline > 0 && now >= slot.sleep_deadline) {
                    slot.sleep_deadline = 0;  // Wake up
                }
            }
            
            // Run all runnable coroutines
            bool progress = true;
            while (progress) {
                progress = false;
                for (auto& slot : tasks_) {
                    if (slot.handle && slot.wait_mask == 0 && slot.sleep_deadline == 0) {
                        auto h = std::coroutine_handle<>::from_address(slot.handle);
                        if (!h.done()) {
                            h.resume();
                            progress = true;
                            if (h.done()) {
                                h.destroy();
                                slot.handle = nullptr;
                            }
                        }
                    }
                }
            }
            
            // Calculate minimum sleep timeout
            usec min_deadline = UINT64_MAX;
            for (const auto& slot : tasks_) {
                if (slot.handle && slot.sleep_deadline > 0) {
                    if (slot.sleep_deadline < min_deadline) {
                        min_deadline = slot.sleep_deadline;
                    }
                }
            }
            
            // Collect wait masks from all blocked coroutines
            std::uint32_t mask = 0;
            for (const auto& slot : tasks_) {
                mask |= slot.wait_mask;
            }
            
            // If no events to wait for and no sleepers, continue
            if (mask == 0 && min_deadline == UINT64_MAX) continue;
            
            // Calculate timeout for wait
            now = time_fn_();
            usec timeout = (min_deadline != UINT64_MAX && min_deadline > now) 
                         ? (min_deadline - now) 
                         : 0;
            
            // Block on kernel (kernel handles sleep/WFI)
            std::uint32_t events = wait_fn_(mask, timeout);
            
            // Clear wait_mask for fired events
            for (auto& slot : tasks_) {
                if (slot.wait_mask && (events & slot.wait_mask)) {
                    slot.wait_mask = 0;
                }
            }
        }
    }
    
    /// Register event wait (called by awaiter)
    void register_wait(std::coroutine_handle<> h, std::uint32_t mask) {
        for (auto& slot : tasks_) {
            if (slot.handle == h.address()) {
                slot.wait_mask = mask;
                return;
            }
        }
    }
    
    /// Register sleep (called by awaiter)
    void register_sleep(std::coroutine_handle<> h, usec duration_us) {
        usec deadline = time_fn_() + duration_us;
        for (auto& slot : tasks_) {
            if (slot.handle == h.address()) {
                slot.sleep_deadline = deadline;
                return;
            }
        }
    }

    /// Static instance for global sleep registration
    static inline Scheduler* s_instance = nullptr;

private:
    struct Slot {
        void* handle{nullptr};
        std::uint32_t wait_mask{0};
        usec sleep_deadline{0};  // 0 = not sleeping
    };
    
    std::array<Slot, MaxTasks> tasks_{};
    WaitFn wait_fn_;
    TimeFn time_fn_;
};

// =====================================================================
// Awaiter Factory (for use in coroutines)
// =====================================================================

// Usage: 
//   co_await ctx.wait_for(Event::VSync | Event::MidiReady);
//   co_await ctx.sleep(100ms);
//   co_await 50ms;  // Also works via operator co_await

template <std::size_t MaxTasks>
class SchedulerContext {
public:
    explicit SchedulerContext(Scheduler<MaxTasks>& sched) : sched_(sched) {}
    
    /// Create awaiter for events
    auto wait_for(std::uint32_t mask) {
        struct EventAwaiter {
            Scheduler<MaxTasks>& sched;
            std::uint32_t mask;
            std::uint32_t result{0};
            
            bool await_ready() const noexcept { return false; }
            
            void await_suspend(std::coroutine_handle<> h) noexcept {
                sched.register_wait(h, mask);
            }
            
            std::uint32_t await_resume() noexcept {
                return result;
            }
        };
        return EventAwaiter{sched_, mask};
    }
    
    /// Create awaiter for timed sleep
    template <typename Rep, typename Period>
    auto sleep(std::chrono::duration<Rep, Period> duration) {
        usec us = std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
        return sleep_us(us);
    }
    
    /// Create awaiter for sleep in microseconds
    auto sleep_us(usec duration_us) {
        struct SleepContextAwaiter {
            Scheduler<MaxTasks>& sched;
            usec duration_us;
            
            bool await_ready() const noexcept { return duration_us == 0; }
            
            void await_suspend(std::coroutine_handle<> h) noexcept {
                sched.register_sleep(h, duration_us);
            }
            
            void await_resume() noexcept {}
        };
        return SleepContextAwaiter{sched_, duration_us};
    }
    
private:
    Scheduler<MaxTasks>& sched_;
};

// =====================================================================
// Chrono Literals for convenient usage
// =====================================================================
// Bring std::chrono_literals into scope for user code

namespace literals {
    using namespace std::chrono_literals;

    /// Enable: co_await 100ms; (operator co_await for chrono durations)
    template <typename Rep, typename Period>
    inline SleepAwaiter operator co_await(std::chrono::duration<Rep, Period> duration) {
        return SleepAwaiter{duration};
    }
}

} // namespace umi::coro
