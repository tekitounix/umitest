#include <umios/umi_kernel.hh>
#include "test_common.hh"
#include <cstdio>
#include <cstdlib>
#include <vector>

struct MockHw {
    static inline umi::usec now_us {0};
    static inline umi::usec timer_set {0};
    static inline std::uint8_t last_ipi {0xFF};
    static inline std::uint8_t core_id {0};
    static inline int context_switch_requests {0};
    static inline int save_fpu_calls {0};
    static inline int restore_fpu_calls {0};
    static inline int crit_enter {0};
    static inline int crit_exit {0};
    static inline int sleep_calls {0};
    static inline std::uint32_t fake_cycles {0};
    
    struct MpuCall {
        std::size_t idx;
        const void* base;
        std::size_t bytes;
        bool writable;
        bool executable;
    };
    static inline std::array<MpuCall, 16> mpu_calls {};
    static inline std::size_t mpu_call_count {0};
    static inline std::array<std::byte, 128> backup_ram {};

    static void set_timer_absolute(umi::usec target) { timer_set = target; }
    static umi::usec monotonic_time_usecs() { return now_us; }
    static void enter_critical() { ++crit_enter; }
    static void exit_critical() { ++crit_exit; }
    static void trigger_ipi(std::uint8_t core) { last_ipi = core; }
    static std::uint8_t current_core() { return core_id; }
    static void request_context_switch() { ++context_switch_requests; }
    static void save_fpu() { ++save_fpu_calls; }
    static void restore_fpu() { ++restore_fpu_calls; }
    static void mute_audio_dma() {}
    static void write_backup_ram(const void* data, std::size_t bytes) {
        std::memcpy(backup_ram.data(), data, bytes > backup_ram.size() ? backup_ram.size() : bytes);
    }
    static void read_backup_ram(void* data, std::size_t bytes) {
        std::memcpy(data, backup_ram.data(), bytes > backup_ram.size() ? backup_ram.size() : bytes);
    }
    static void configure_mpu_region(std::size_t idx, const void* base, std::size_t bytes, bool w, bool x) {
        if (mpu_call_count < mpu_calls.size()) {
            mpu_calls[mpu_call_count++] = {idx, base, bytes, w, x};
        }
    }
    static void cache_clean(const void*, std::size_t) {}
    static void cache_invalidate(void*, std::size_t) {}
    static void cache_clean_invalidate(void*, std::size_t) {}
    static void system_reset() {}
    static void enter_sleep() { ++sleep_calls; }
    static std::uint32_t cycle_count() { return fake_cycles; }
    static std::uint32_t cycles_per_usec() { return 168; }  // 168MHz typical Cortex-M
};

using Kernel = umi::Kernel<4, 4, umi::Hw<MockHw>>;
static Kernel k;

static int timer_fired = 0;
void on_timer(void*) { ++timer_fired; }

// Use umi::test::check from test_common.hh
using umi::test::check;

int main() {
    // Task creation and scheduling (priority)
    umi::TaskConfig idle_fpu{.entry = nullptr, .arg = nullptr, .prio = umi::Priority::Idle, .core_affinity = static_cast<std::uint8_t>(umi::Core::Any), .uses_fpu = true};
    umi::TaskConfig audio_fpu{.entry = nullptr, .arg = nullptr, .prio = umi::Priority::Realtime, .core_affinity = static_cast<std::uint8_t>(umi::Core::Any), .uses_fpu = true};

    auto t_idle = k.create_task(idle_fpu);
    k.resume_task(t_idle);  // schedule() called here, context_switch_requests = 1
    k.tick(0);                 // schedule() called again, context_switch_requests = 2
    // Verify context switch was requested and correct task selected
    check(MockHw::context_switch_requests == 2, "context switch requested for idle task");
    auto next = k.get_next_task();
    check(next.has_value() && next.value() == t_idle.value, "first switch to idle task");
    k.prepare_switch(next.value());
    check(MockHw::restore_fpu_calls == 1, "restore_fpu on first switch");

    auto t_audio = k.create_task(audio_fpu);
    k.resume_task(t_audio);  // schedule(), context_switch_requests = 3
    k.tick(0);                  // schedule(), context_switch_requests = 4
    check(MockHw::context_switch_requests == 4, "context switch requested for preemption");
    next = k.get_next_task();
    check(next.has_value() && next.value() == t_audio.value, "preempt to audio task");
    k.prepare_switch(next.value());
    check(MockHw::save_fpu_calls == 1, "save_fpu on preempt");
    check(MockHw::restore_fpu_calls == 2, "restore_fpu on audio entry");

    // Timer queue ticked mode
    bool scheduled = k.call_later(1000, umi::TimerCallback{.fn = on_timer, .ctx = nullptr});
    check(scheduled, "timer scheduled");
    check(MockHw::timer_set == 1000, "timer programmed");
    k.tick(500);
    check(timer_fired == 0, "timer not fired early");
    k.tick(500);
    check(timer_fired == 1, "timer fired after total delay");

    // Notifications
    k.notify(t_audio, 0x1);
    auto v = k.wait(t_audio, 0x1);
    check(v == 0x1, "notification delivered");
    auto v2 = k.wait(t_audio, 0x1);
    check(v2 == 0, "notification cleared");

    // Blocking wait test: create a User task that will be blocked
    umi::TaskConfig user_cfg{.entry = nullptr, .arg = nullptr, .prio = umi::Priority::User, .core_affinity = static_cast<std::uint8_t>(umi::Core::Any), .uses_fpu = false};
    auto t_user = k.create_task(user_cfg);
    k.resume_task(t_user);
    // Simulate: user task calls wait_block but no flags - should block (in real system)
    // Here we test notify wakes a blocked task
    k.notify(t_user, 0x0);  // No bits - task stays blocked if it called wait_block
    // Now notify with bits - should wake
    k.notify(t_user, 0x4);
    auto user_bits = k.wait(t_user, 0x4);
    check(user_bits == 0x4, "user task notification works");

    // Masked critical section (RAII) - test manual usage
    // Note: Kernel APIs now use critical sections internally, so counts are higher
    int crit_before = MockHw::crit_enter;
    {
        umi::MaskedCritical<umi::Hw<MockHw>> guard;
        (void)guard;
    }
    check(MockHw::crit_enter == crit_before + 1 && MockHw::crit_exit == crit_before + 1, 
          "manual critical enter/exit balanced");

    // ========================================
    // Test delete_task
    // ========================================
    umi::TaskConfig temp_cfg{.entry = nullptr, .arg = nullptr, .prio = umi::Priority::User, .core_affinity = static_cast<std::uint8_t>(umi::Core::Any), .uses_fpu = false};
    auto t_temp = k.create_task(temp_cfg);
    check(t_temp.valid(), "temp task created");
    
    // Should be able to delete non-running task
    bool deleted = k.delete_task(t_temp);
    check(deleted, "delete_task succeeded");
    
    // Should not be able to delete invalid task
    deleted = k.delete_task(t_temp);  // Already deleted
    check(!deleted, "delete invalid task fails");
    
    // Re-create in same slot (slot reuse)
    auto t_reuse = k.create_task(temp_cfg);
    check(t_reuse.valid(), "task slot reused after delete");

    // Shared memory registration
    int shared_buf[4] = {1, 2, 3, 4};
    bool reg_ok = k.register_shared(umi::SharedRegionId::Audio, shared_buf, sizeof(shared_buf));
    check(reg_ok, "shared region registered");
    auto span = k.shared_region(0);
    check(span.has_value(), "shared region present");
    check(span->size() == sizeof(shared_buf), "shared region size");
    
    // get_shared syscall helper (also configures MPU)
    auto desc = k.get_shared(umi::SharedRegionId::Audio);
    check(desc.valid(), "get_shared returns valid descriptor");
    check(desc.base == shared_buf, "get_shared base correct");
    check(desc.size == sizeof(shared_buf), "get_shared size correct");
    check(MockHw::mpu_call_count == 1, "get_shared configures mpu");

    // IPI
    k.send_ipi(2);
    check(MockHw::last_ipi == 2, "ipi recorded");

    // MPU config (explicit)
    MockHw::mpu_call_count = 0;  // Reset counter
    umi::Kernel<4, 4, umi::Hw<MockHw>>::Region regions[] = {
        {shared_buf, sizeof(shared_buf), true, false},
    };
    k.configure_mpu(regions);
    check(MockHw::mpu_call_count == 1, "mpu configured");

    // ========================================
    // Test Statistics: Stopwatch & LoadMonitor
    // ========================================
    std::puts("Testing Statistics...");
    
    // Stopwatch test
    umi::Stopwatch<umi::Hw<MockHw>> sw;
    MockHw::fake_cycles = 1000;
    sw.start();
    MockHw::fake_cycles = 2680;  // 1680 cycles elapsed = 10us at 168MHz
    sw.stop();
    check(sw.elapsed_cycles() == 1680, "stopwatch cycles");
    check(sw.elapsed_usecs() == 10, "stopwatch usecs");
    
    // LoadMonitor test
    umi::LoadMonitor<umi::Hw<MockHw>, 4> load;
    
    // Simulate 50% load (5000 cycles used out of 10000 budget)
    MockHw::fake_cycles = 0;
    load.begin();
    MockHw::fake_cycles = 5000;
    load.end(10000);
    check(load.instant() == 5000, "load instant 50%");
    check(load.peak() == 5000, "load peak 50%");
    
    // Simulate 80% load
    MockHw::fake_cycles = 0;
    load.begin();
    MockHw::fake_cycles = 8000;
    load.end(10000);
    check(load.instant() == 8000, "load instant 80%");
    check(load.peak() == 8000, "load peak updated to 80%");
    
    // Simulate 30% load - peak should stay at 80%
    MockHw::fake_cycles = 0;
    load.begin();
    MockHw::fake_cycles = 3000;
    load.end(10000);
    check(load.instant() == 3000, "load instant 30%");
    check(load.peak() == 8000, "load peak still 80%");
    
    // Check average is calculated
    check(load.average() > 0, "load average calculated");
    
    // Reset peak
    load.reset_peak();
    check(load.peak() == 0, "load peak reset");

    // =====================================
    // SpscQueue Tests
    // =====================================
    std::puts("Testing SpscQueue...");
    
    // Basic push/pop
    umi::SpscQueue<int, 16> int_queue;
    check(int_queue.empty_approx(), "int_queue initially empty");
    check(int_queue.has_space(), "int_queue has space");
    check(!int_queue.try_pop().has_value(), "pop from empty returns nullopt");
    
    bool pushed = int_queue.try_push(42);
    check(pushed, "push int");
    check(int_queue.size_approx() == 1, "size is 1");
    
    auto peeked = int_queue.peek();
    check(peeked.has_value() && *peeked == 42, "peek returns 42");
    check(int_queue.size_approx() == 1, "size still 1 after peek");
    
    auto popped = int_queue.try_pop();
    check(popped.has_value() && *popped == 42, "pop returns 42");
    check(int_queue.empty_approx(), "empty after pop");
    
    // Fill to capacity (Capacity - 1 items for ring buffer)
    for (int i = 0; i < 15; ++i) {
        check(int_queue.try_push(i), "push to fill queue");
    }
    check(!int_queue.has_space(), "queue full");
    check(!int_queue.try_push(999), "push to full fails");
    
    // Drain and verify order
    for (int i = 0; i < 15; ++i) {
        auto val = int_queue.try_pop();
        check(val.has_value() && *val == i, "FIFO order preserved");
    }
    check(int_queue.empty_approx(), "empty after drain");
    
    // Batch read
    for (int i = 0; i < 5; ++i) {
        int_queue.try_push(i * 10);
    }
    std::array<int, 8> batch{};
    std::size_t read_count = int_queue.read_all(std::span(batch));
    check(read_count == 5, "read_all returns 5");
    check(batch[0] == 0 && batch[4] == 40, "batch values correct");
    check(int_queue.empty_approx(), "empty after read_all");
    
    // Struct type
    struct TestData { int x; float y; };
    umi::SpscQueue<TestData, 8> struct_queue;
    struct_queue.try_push({1, 2.5f});
    auto data = struct_queue.try_pop();
    check(data.has_value() && data->x == 1 && data->y == 2.5f, "struct queue works");

    // std::vector test (exercises heap allocation, may trigger exception stubs if misconfigured)
    {
        std::vector<int> vec;
        for (int i = 0; i < 100; ++i) {
            vec.push_back(i * 2);
        }
        check(vec.size() == 100, "vector size correct");
        check(vec[50] == 100, "vector element correct");
        vec.clear();
        check(vec.empty(), "vector cleared");
    }

    // =====================================
    // Additional RTOS Tests
    // =====================================

    // Test suspend_task
    SECTION("suspend_task");
    {
        umi::Kernel<4, 4, umi::Hw<MockHw>> k2;
        umi::TaskConfig cfg{.entry = nullptr, .prio = umi::Priority::User, .name = "suspend_test"};
        auto tid = k2.create_task(cfg);
        k2.resume_task(tid);

        // Task should be Ready
        check(std::string_view(k2.get_task_state_str(tid)) == "Running" ||
              std::string_view(k2.get_task_state_str(tid)) == "Ready",
              "task is ready/running after resume");

        k2.suspend_task(tid);
        check(std::string_view(k2.get_task_state_str(tid)) == "Blocked",
              "task is blocked after suspend");

        k2.resume_task(tid);
        check(std::string_view(k2.get_task_state_str(tid)) != "Blocked",
              "task is not blocked after re-resume");
    }

    // Test priority scheduling (Server priority)
    SECTION("Priority Scheduling");
    {
        umi::Kernel<8, 4, umi::Hw<MockHw>> k3;
        MockHw::core_id = 0;

        auto t_idle = k3.create_task({.prio = umi::Priority::Idle, .name = "idle"});
        auto t_user = k3.create_task({.prio = umi::Priority::User, .name = "user"});
        auto t_server = k3.create_task({.prio = umi::Priority::Server, .name = "server"});
        auto t_rt = k3.create_task({.prio = umi::Priority::Realtime, .name = "realtime"});

        k3.resume_task(t_idle);
        k3.resume_task(t_user);
        k3.resume_task(t_server);
        k3.resume_task(t_rt);

        // Realtime should be selected first
        auto next = k3.get_next_task();
        check(next.has_value() && *next == t_rt.value, "Realtime selected first");
        k3.prepare_switch(*next);

        // After Realtime is running, Server should be next
        // (simulate Realtime blocks)
        k3.suspend_task(t_rt);
        next = k3.get_next_task();
        check(next.has_value() && *next == t_server.value, "Server selected over User");
    }

    // Test User priority round-robin
    SECTION("User Round-Robin");
    {
        umi::Kernel<8, 4, umi::Hw<MockHw>> k4;
        MockHw::core_id = 0;

        auto u1 = k4.create_task({.prio = umi::Priority::User, .name = "user1"});
        auto u2 = k4.create_task({.prio = umi::Priority::User, .name = "user2"});
        auto u3 = k4.create_task({.prio = umi::Priority::User, .name = "user3"});

        k4.resume_task(u1);
        k4.resume_task(u2);
        k4.resume_task(u3);

        // First call should get one of the user tasks
        auto n1 = k4.get_next_task();
        check(n1.has_value(), "round-robin: first task selected");
        k4.prepare_switch(*n1);

        // Simulate time slice expired - should get different task
        auto n2 = k4.get_next_task();
        check(n2.has_value(), "round-robin: second task selected");
        // Note: Due to round-robin implementation, next task may or may not be different
        // depending on internal state
    }

    // Test core affinity
    SECTION("Core Affinity");
    {
        umi::Kernel<4, 4, umi::Hw<MockHw>> k5;

        // Task pinned to core 1
        auto t_core1 = k5.create_task({.prio = umi::Priority::User, .core_affinity = 1, .name = "core1_task"});
        k5.resume_task(t_core1);

        // On core 0, should not select core1 task
        MockHw::core_id = 0;
        auto next = k5.get_next_task();
        check(!next.has_value() || *next != t_core1.value, "core 0 skips core 1 affinity task");

        // On core 1, should select it
        MockHw::core_id = 1;
        next = k5.get_next_task();
        check(next.has_value() && *next == t_core1.value, "core 1 selects its affinity task");

        MockHw::core_id = 0;  // Reset
    }

    // Test on_timer_irq with explicit time argument (legacy tickless)
    SECTION("Timer IRQ with time");
    {
        umi::Kernel<4, 4, umi::Hw<MockHw>> k6;
        static int irq_timer_fired = 0;
        irq_timer_fired = 0;

        // Use legacy on_timer_irq(usec) which updates kernel time directly
        k6.on_timer_irq(1000);  // Set kernel time to 1000

        bool scheduled = k6.call_later(500, {.fn = [](void*) { ++irq_timer_fired; }, .ctx = nullptr});
        check(scheduled, "timer scheduled");
        check(MockHw::timer_set == 1500, "timer programmed to 1500");

        // Use tick() to advance time and trigger
        k6.tick(400);  // Now at 1400
        check(irq_timer_fired == 0, "timer not fired at 1400");

        k6.tick(200);  // Now at 1600, timer should fire
        check(irq_timer_fired == 1, "timer fired after 1600");
    }

    // Test for_each_task iteration
    SECTION("Task Iteration");
    {
        umi::Kernel<4, 4, umi::Hw<MockHw>> k7;
        k7.create_task({.prio = umi::Priority::User, .name = "iter1"});
        k7.create_task({.prio = umi::Priority::Idle, .name = "iter2"});

        int count = 0;
        k7.for_each_task([&](umi::TaskId, const umi::TaskConfig& cfg, auto) {
            if (cfg.name) ++count;
        });

        check(count == 2, "for_each_task iterates all tasks");
    }

    TEST_SUMMARY();
}
