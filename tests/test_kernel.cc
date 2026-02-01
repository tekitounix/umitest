#include <cstdio>
#include <cstdlib>
#include <umios/kernel/umi_kernel.hh>
#include <vector>

#include "test_common.hh"

struct MockHw {
    static inline umi::usec now_us{0};
    static inline umi::usec timer_set{0};
    static inline std::uint8_t last_ipi{0xFF};
    static inline std::uint8_t core_id{0};
    static inline int context_switch_requests{0};
    static inline int crit_enter{0};
    static inline int crit_exit{0};
    static inline int sleep_calls{0};
    static inline std::uint32_t fake_cycles{0};

    static void set_timer_absolute(umi::usec target) { timer_set = target; }
    static umi::usec monotonic_time_usecs() { return now_us; }
    static void enter_critical() { ++crit_enter; }
    static void exit_critical() { ++crit_exit; }
    static void trigger_ipi(std::uint8_t core) { last_ipi = core; }
    static std::uint8_t current_core() { return core_id; }
    static void request_context_switch() { ++context_switch_requests; }
    static void enter_sleep() { ++sleep_calls; }
    static std::uint32_t cycle_count() { return fake_cycles; }
    static std::uint32_t cycles_per_usec() { return 168; }
};

using HW = umi::Hw<MockHw>;
using Kernel = umi::Kernel<4, 4, HW>;

static int timer_fired = 0;
void on_timer(void*) {
    ++timer_fired;
}

using umi::test::check;

int main() {
    // ========================================
    // Task creation and scheduling (priority)
    // ========================================
    SECTION("Task Creation & Priority");
    {
        Kernel k;
        MockHw::context_switch_requests = 0;

        umi::TaskConfig idle_cfg{
            .prio = umi::Priority::IDLE,
            .uses_fpu = true,
            .name = "idle_fpu",
        };
        umi::TaskConfig audio_cfg{
            .prio = umi::Priority::REALTIME,
            .uses_fpu = true,
            .name = "audio_fpu",
        };

        auto t_idle = k.create_task(idle_cfg);
        k.resume_task(t_idle);
        k.tick(0);
        check(MockHw::context_switch_requests >= 1, "context switch requested for idle task");
        auto next = k.get_next_task();
        check(next.has_value() && next.value() == t_idle.value, "first switch to idle task");
        k.prepare_switch(next.value());

        auto t_audio = k.create_task(audio_cfg);
        k.resume_task(t_audio);
        k.tick(0);
        next = k.get_next_task();
        check(next.has_value() && next.value() == t_audio.value, "preempt to audio task");
        k.prepare_switch(next.value());
    }

    // ========================================
    // Timer queue ticked mode
    // ========================================
    SECTION("Timer Queue");
    {
        Kernel k;
        timer_fired = 0;

        bool scheduled = k.call_later(1000, umi::TimerCallback{.fn = on_timer, .ctx = nullptr});
        check(scheduled, "timer scheduled");
        check(MockHw::timer_set == 1000, "timer programmed");
        k.tick(500);
        check(timer_fired == 0, "timer not fired early");
        k.tick(500);
        check(timer_fired == 1, "timer fired after total delay");
    }

    // ========================================
    // Notifications
    // ========================================
    SECTION("Notifications");
    {
        Kernel k;
        auto t = k.create_task({.prio = umi::Priority::REALTIME, .name = "notify_test"});

        k.notify(t, 0x1);
        auto v = k.wait(t, 0x1);
        check(v == 0x1, "notification delivered");
        auto v2 = k.wait(t, 0x1);
        check(v2 == 0, "notification cleared");
    }

    // ========================================
    // Blocking wait (wait_block) — starvation fix
    // ========================================
    SECTION("wait_block starvation fix");
    {
        Kernel k;
        MockHw::core_id = 0;

        auto t_rt = k.create_task({.prio = umi::Priority::REALTIME, .name = "audio"});
        auto t_srv = k.create_task({.prio = umi::Priority::SERVER, .name = "server"});

        // Simulate: audio task is Running
        auto next = k.get_next_task();
        check(next.has_value() && *next == t_rt.value, "realtime task selected first");
        k.prepare_switch(*next);

        // Notify audio task (simulating DMA ISR)
        k.notify(t_rt, umi::KernelEvent::AudioReady);

        // Audio task calls wait_block — flags are pending, so take+return immediately
        auto bits = k.wait_block(t_rt, umi::KernelEvent::AudioReady);
        check(bits == umi::KernelEvent::AudioReady, "wait_block returns pending flags");

        // Audio task calls wait_block again — no flags pending, should block
        bits = k.wait_block(t_rt, umi::KernelEvent::AudioReady);
        // In real system this would block. In test, task state is now Blocked.
        // Since no notify happened, bits should be 0 (take after wakeup finds nothing).
        check(bits == 0, "wait_block blocks when no flags (returns 0 in test)");

        // Verify audio task is Blocked and server task can be selected
        check(std::string_view(k.get_task_state_str(t_rt)) == "Blocked",
              "audio task is Blocked after wait_block with no flags");

        next = k.get_next_task();
        check(next.has_value() && *next == t_srv.value,
              "server task selected when audio is Blocked");
    }

    // ========================================
    // Masked critical section (RAII)
    // ========================================
    SECTION("Critical Section");
    {
        int crit_before = MockHw::crit_enter;
        {
            umi::MaskedCritical<HW> guard;
            (void)guard;
        }
        check(MockHw::crit_enter == crit_before + 1 && MockHw::crit_exit == crit_before + 1,
              "manual critical enter/exit balanced");
    }

    // ========================================
    // delete_task
    // ========================================
    SECTION("delete_task");
    {
        Kernel k;
        auto t = k.create_task({.prio = umi::Priority::USER, .name = "temp"});
        check(t.valid(), "temp task created");

        bool deleted = k.delete_task(t);
        check(deleted, "delete_task succeeded");

        deleted = k.delete_task(t);
        check(!deleted, "delete invalid task fails");

        auto t_reuse = k.create_task({.prio = umi::Priority::USER, .name = "reuse"});
        check(t_reuse.valid(), "task slot reused after delete");
    }

    // ========================================
    // IPI
    // ========================================
    SECTION("IPI");
    {
        Kernel k;
        k.send_ipi(2);
        check(MockHw::last_ipi == 2, "ipi recorded");
    }

    // ========================================
    // Stopwatch & LoadMonitor
    // ========================================
    SECTION("Statistics");
    {
        umi::Stopwatch<HW> sw;
        MockHw::fake_cycles = 1000;
        sw.start();
        MockHw::fake_cycles = 2680;
        sw.stop();
        check(sw.elapsed_cycles() == 1680, "stopwatch cycles");
        check(sw.elapsed_usecs() == 10, "stopwatch usecs");

        umi::LoadMonitor<HW, 4> load;

        MockHw::fake_cycles = 0;
        load.begin();
        MockHw::fake_cycles = 5000;
        load.end(10000);
        check(load.instant() == 5000, "load instant 50%");
        check(load.peak() == 5000, "load peak 50%");

        MockHw::fake_cycles = 0;
        load.begin();
        MockHw::fake_cycles = 8000;
        load.end(10000);
        check(load.instant() == 8000, "load instant 80%");
        check(load.peak() == 8000, "load peak updated to 80%");

        MockHw::fake_cycles = 0;
        load.begin();
        MockHw::fake_cycles = 3000;
        load.end(10000);
        check(load.instant() == 3000, "load instant 30%");
        check(load.peak() == 8000, "load peak still 80%");

        check(load.average() > 0, "load average calculated");

        load.reset_peak();
        check(load.peak() == 0, "load peak reset");
    }

    // ========================================
    // SpscQueue
    // ========================================
    SECTION("SpscQueue");
    {
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

        for (int i = 0; i < 15; ++i) {
            check(int_queue.try_push(i), "push to fill queue");
        }
        check(!int_queue.has_space(), "queue full");
        check(!int_queue.try_push(999), "push to full fails");

        for (int i = 0; i < 15; ++i) {
            auto val = int_queue.try_pop();
            check(val.has_value() && *val == i, "FIFO order preserved");
        }
        check(int_queue.empty_approx(), "empty after drain");

        for (int i = 0; i < 5; ++i) {
            int_queue.try_push(i * 10);
        }
        std::array<int, 8> batch{};
        std::size_t read_count = int_queue.read_all(std::span(batch));
        check(read_count == 5, "read_all returns 5");
        check(batch[0] == 0 && batch[4] == 40, "batch values correct");
        check(int_queue.empty_approx(), "empty after read_all");

        struct TestData {
            int x;
            float y;
        };
        umi::SpscQueue<TestData, 8> struct_queue;
        struct_queue.try_push({1, 2.5f});
        auto data = struct_queue.try_pop();
        check(data.has_value() && data->x == 1 && data->y == 2.5f, "struct queue works");
    }

    // ========================================
    // suspend_task
    // ========================================
    SECTION("suspend_task");
    {
        Kernel k;
        auto tid = k.create_task({.prio = umi::Priority::USER, .name = "suspend_test"});
        k.resume_task(tid);

        check(std::string_view(k.get_task_state_str(tid)) == "Running" ||
                  std::string_view(k.get_task_state_str(tid)) == "Ready",
              "task is ready/running after resume");

        k.suspend_task(tid);
        check(std::string_view(k.get_task_state_str(tid)) == "Blocked", "task is blocked after suspend");

        k.resume_task(tid);
        check(std::string_view(k.get_task_state_str(tid)) != "Blocked", "task is not blocked after re-resume");
    }

    // ========================================
    // Priority Scheduling
    // ========================================
    SECTION("Priority Scheduling");
    {
        umi::Kernel<8, 4, HW> k;
        MockHw::core_id = 0;

        auto t_idle = k.create_task({.prio = umi::Priority::IDLE, .name = "idle"});
        auto t_user = k.create_task({.prio = umi::Priority::USER, .name = "user"});
        auto t_server = k.create_task({.prio = umi::Priority::SERVER, .name = "server"});
        auto t_rt = k.create_task({.prio = umi::Priority::REALTIME, .name = "realtime"});

        k.resume_task(t_idle);
        k.resume_task(t_user);
        k.resume_task(t_server);
        k.resume_task(t_rt);

        auto next = k.get_next_task();
        check(next.has_value() && *next == t_rt.value, "Realtime selected first");
        k.prepare_switch(*next);

        k.suspend_task(t_rt);
        next = k.get_next_task();
        check(next.has_value() && *next == t_server.value, "Server selected over User");
    }

    // ========================================
    // User Round-Robin
    // ========================================
    SECTION("User Round-Robin");
    {
        umi::Kernel<8, 4, HW> k;
        MockHw::core_id = 0;

        auto u1 = k.create_task({.prio = umi::Priority::USER, .name = "user1"});
        auto u2 = k.create_task({.prio = umi::Priority::USER, .name = "user2"});
        auto u3 = k.create_task({.prio = umi::Priority::USER, .name = "user3"});

        k.resume_task(u1);
        k.resume_task(u2);
        k.resume_task(u3);

        auto n1 = k.get_next_task();
        check(n1.has_value(), "round-robin: first task selected");
        k.prepare_switch(*n1);

        auto n2 = k.get_next_task();
        check(n2.has_value(), "round-robin: second task selected");
    }

    // ========================================
    // Core Affinity
    // ========================================
    SECTION("Core Affinity");
    {
        Kernel k;
        auto t_core1 = k.create_task({.prio = umi::Priority::USER, .core_affinity = 1, .name = "core1_task"});
        k.resume_task(t_core1);

        MockHw::core_id = 0;
        auto next = k.get_next_task();
        check(!next.has_value() || *next != t_core1.value, "core 0 skips core 1 affinity task");

        MockHw::core_id = 1;
        next = k.get_next_task();
        check(next.has_value() && *next == t_core1.value, "core 1 selects its affinity task");

        MockHw::core_id = 0;
    }

    // ========================================
    // Task Iteration
    // ========================================
    SECTION("Task Iteration");
    {
        Kernel k;
        k.create_task({.prio = umi::Priority::USER, .name = "iter1"});
        k.create_task({.prio = umi::Priority::IDLE, .name = "iter2"});

        int count = 0;
        k.for_each_task([&](umi::TaskId, const umi::TaskConfig& cfg, auto) {
            if (cfg.name)
                ++count;
        });

        check(count == 2, "for_each_task iterates all tasks");
    }

    // ========================================
    // std::vector (heap sanity check)
    // ========================================
    SECTION("Heap sanity");
    {
        std::vector<int> vec;
        for (int i = 0; i < 100; ++i) {
            vec.push_back(i * 2);
        }
        check(vec.size() == 100, "vector size correct");
        check(vec[50] == 100, "vector element correct");
    }

    TEST_SUMMARY();
}
