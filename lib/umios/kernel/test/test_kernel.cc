// SPDX-License-Identifier: MIT
// UMI-OS Kernel Tests

#include <umios/kernel/umi_kernel.hh>
#include <umitest.hh>
#include <vector>

using namespace umitest;

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

    static void reset() {
        now_us = umi::usec{0};
        timer_set = umi::usec{0};
        last_ipi = 0xFF;
        core_id = 0;
        context_switch_requests = 0;
        crit_enter = 0;
        crit_exit = 0;
        sleep_calls = 0;
        fake_cycles = 0;
    }
};

using HW = umi::Hw<MockHw>;
using Kernel = umi::Kernel<4, 4, HW>;

static int timer_fired = 0;
void on_timer(void*) {
    ++timer_fired;
}

bool test_task_creation_priority(TestContext& t) {
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
    t.assert_ge(MockHw::context_switch_requests, 1);
    auto next = k.get_next_task();
    t.assert_true(next.has_value() && next.value() == t_idle.value, "first switch to idle task");
    k.prepare_switch(next.value());

    auto t_audio = k.create_task(audio_cfg);
    k.resume_task(t_audio);
    k.tick(0);
    next = k.get_next_task();
    t.assert_true(next.has_value() && next.value() == t_audio.value, "preempt to audio task");
    return true;
}

bool test_timer_queue(TestContext& t) {
    Kernel k;
    MockHw::reset();
    timer_fired = 0;

    bool scheduled = k.call_later(1000, umi::TimerCallback{.fn = on_timer, .ctx = nullptr});
    t.assert_true(scheduled, "timer scheduled");
    t.assert_eq(MockHw::timer_set, umi::usec{1000});
    k.tick(500);
    t.assert_eq(timer_fired, 0);
    k.tick(500);
    t.assert_eq(timer_fired, 1);
    return true;
}

bool test_notifications(TestContext& t) {
    Kernel k;
    auto task = k.create_task({.prio = umi::Priority::REALTIME, .name = "notify_test"});

    k.notify(task, 0x1);
    auto v = k.wait(task, 0x1);
    t.assert_eq(v, 0x1u);
    auto v2 = k.wait(task, 0x1);
    t.assert_eq(v2, 0u);
    return true;
}

bool test_wait_block_starvation_fix(TestContext& t) {
    Kernel k;
    MockHw::core_id = 0;

    auto t_rt = k.create_task({.prio = umi::Priority::REALTIME, .name = "audio"});
    auto t_srv = k.create_task({.prio = umi::Priority::SERVER, .name = "server"});

    auto next = k.get_next_task();
    t.assert_true(next.has_value() && *next == t_rt.value, "realtime task selected first");
    k.prepare_switch(*next);

    k.notify(t_rt, umi::KernelEvent::AudioReady);

    auto bits = k.wait_block(t_rt, umi::KernelEvent::AudioReady);
    t.assert_eq(bits, umi::KernelEvent::AudioReady);

    bits = k.wait_block(t_rt, umi::KernelEvent::AudioReady);
    t.assert_eq(bits, 0u);

    t.assert_true(std::string_view(k.get_task_state_str(t_rt)) == "Blocked",
                  "audio task is Blocked after wait_block with no flags");

    next = k.get_next_task();
    t.assert_true(next.has_value() && *next == t_srv.value, "server task selected when audio is Blocked");
    return true;
}

bool test_critical_section(TestContext& t) {
    MockHw::reset();
    int crit_before = MockHw::crit_enter;
    {
        umi::MaskedCritical<HW> guard;
        (void)guard;
    }
    t.assert_eq(MockHw::crit_enter, crit_before + 1);
    t.assert_eq(MockHw::crit_exit, crit_before + 1);
    return true;
}

bool test_delete_task(TestContext& t) {
    Kernel k;
    auto task = k.create_task({.prio = umi::Priority::USER, .name = "temp"});
    t.assert_true(task.valid(), "temp task created");

    bool deleted = k.delete_task(task);
    t.assert_true(deleted, "delete_task succeeded");

    deleted = k.delete_task(task);
    t.assert_true(!deleted, "delete invalid task fails");

    auto t_reuse = k.create_task({.prio = umi::Priority::USER, .name = "reuse"});
    t.assert_true(t_reuse.valid(), "task slot reused after delete");
    return true;
}

bool test_ipi(TestContext& t) {
    Kernel k;
    MockHw::reset();
    k.send_ipi(2);
    t.assert_eq(MockHw::last_ipi, 2u);
    return true;
}

bool test_stopwatch_and_load(TestContext& t) {
    MockHw::reset();

    umi::Stopwatch<HW> sw;
    MockHw::fake_cycles = 1000;
    sw.start();
    MockHw::fake_cycles = 2680;
    sw.stop();
    t.assert_eq(sw.elapsed_cycles(), 1680u);
    t.assert_eq(sw.elapsed_usecs(), 10u);

    umi::LoadMonitor<HW, 4> load;

    MockHw::fake_cycles = 0;
    load.begin();
    MockHw::fake_cycles = 5000;
    load.end(10000);
    t.assert_eq(load.instant(), 5000u);
    t.assert_eq(load.peak(), 5000u);

    MockHw::fake_cycles = 0;
    load.begin();
    MockHw::fake_cycles = 8000;
    load.end(10000);
    t.assert_eq(load.instant(), 8000u);
    t.assert_eq(load.peak(), 8000u);

    MockHw::fake_cycles = 0;
    load.begin();
    MockHw::fake_cycles = 3000;
    load.end(10000);
    t.assert_eq(load.instant(), 3000u);
    t.assert_eq(load.peak(), 8000u);

    t.assert_gt(load.average(), 0u);

    load.reset_peak();
    t.assert_eq(load.peak(), 0u);
    return true;
}

bool test_spsc_queue(TestContext& t) {
    umi::SpscQueue<int, 16> int_queue;
    t.assert_true(int_queue.empty_approx(), "int_queue initially empty");
    t.assert_true(int_queue.has_space(), "int_queue has space");
    t.assert_true(!int_queue.try_pop().has_value(), "pop from empty returns nullopt");

    bool pushed = int_queue.try_push(42);
    t.assert_true(pushed, "push int");
    t.assert_eq(int_queue.size_approx(), 1u);

    auto peeked = int_queue.peek();
    t.assert_true(peeked.has_value() && *peeked == 42, "peek returns 42");
    t.assert_eq(int_queue.size_approx(), 1u);

    auto popped = int_queue.try_pop();
    t.assert_true(popped.has_value() && *popped == 42, "pop returns 42");
    t.assert_true(int_queue.empty_approx(), "empty after pop");

    for (int i = 0; i < 15; ++i) {
        t.assert_true(int_queue.try_push(i), "push to fill queue");
    }
    t.assert_true(!int_queue.has_space(), "queue full");
    t.assert_true(!int_queue.try_push(999), "push to full fails");

    for (int i = 0; i < 15; ++i) {
        auto val = int_queue.try_pop();
        t.assert_true(val.has_value() && *val == i, "FIFO order preserved");
    }
    t.assert_true(int_queue.empty_approx(), "empty after drain");

    for (int i = 0; i < 5; ++i) {
        int_queue.try_push(i * 10);
    }
    std::array<int, 8> batch{};
    std::size_t read_count = int_queue.read_all(std::span(batch));
    t.assert_eq(read_count, 5u);
    t.assert_eq(batch[0], 0);
    t.assert_eq(batch[4], 40);
    t.assert_true(int_queue.empty_approx(), "empty after read_all");

    struct TestData {
        int x;
        float y;
    };
    umi::SpscQueue<TestData, 8> struct_queue;
    struct_queue.try_push({1, 2.5f});
    auto data = struct_queue.try_pop();
    t.assert_true(data.has_value() && data->x == 1 && data->y == 2.5f, "struct queue works");
    return true;
}

bool test_suspend_task(TestContext& t) {
    Kernel k;
    auto tid = k.create_task({.prio = umi::Priority::USER, .name = "suspend_test"});
    k.resume_task(tid);

    auto state = std::string_view(k.get_task_state_str(tid));
    t.assert_true(state == "Running" || state == "Ready", "task is ready/running after resume");

    k.suspend_task(tid);
    t.assert_true(std::string_view(k.get_task_state_str(tid)) == "Blocked", "task is blocked after suspend");

    k.resume_task(tid);
    t.assert_true(std::string_view(k.get_task_state_str(tid)) != "Blocked", "task is not blocked after re-resume");
    return true;
}

bool test_priority_scheduling(TestContext& t) {
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
    t.assert_true(next.has_value() && *next == t_rt.value, "Realtime selected first");
    k.prepare_switch(*next);

    k.suspend_task(t_rt);
    next = k.get_next_task();
    t.assert_true(next.has_value() && *next == t_server.value, "Server selected over User");
    return true;
}

bool test_user_round_robin(TestContext& t) {
    umi::Kernel<8, 4, HW> k;
    MockHw::core_id = 0;

    auto u1 = k.create_task({.prio = umi::Priority::USER, .name = "user1"});
    auto u2 = k.create_task({.prio = umi::Priority::USER, .name = "user2"});
    auto u3 = k.create_task({.prio = umi::Priority::USER, .name = "user3"});

    k.resume_task(u1);
    k.resume_task(u2);
    k.resume_task(u3);

    auto n1 = k.get_next_task();
    t.assert_true(n1.has_value(), "round-robin: first task selected");
    k.prepare_switch(*n1);

    auto n2 = k.get_next_task();
    t.assert_true(n2.has_value(), "round-robin: second task selected");
    return true;
}

bool test_core_affinity(TestContext& t) {
    Kernel k;
    auto t_core1 = k.create_task({.prio = umi::Priority::USER, .core_affinity = 1, .name = "core1_task"});
    k.resume_task(t_core1);

    MockHw::core_id = 0;
    auto next = k.get_next_task();
    t.assert_true(!next.has_value() || *next != t_core1.value, "core 0 skips core 1 affinity task");

    MockHw::core_id = 1;
    next = k.get_next_task();
    t.assert_true(next.has_value() && *next == t_core1.value, "core 1 selects its affinity task");

    MockHw::core_id = 0;
    return true;
}

bool test_task_iteration(TestContext& t) {
    Kernel k;
    k.create_task({.prio = umi::Priority::USER, .name = "iter1"});
    k.create_task({.prio = umi::Priority::IDLE, .name = "iter2"});

    int count = 0;
    k.for_each_task([&](umi::TaskId, const umi::TaskConfig& cfg, auto) {
        if (cfg.name)
            ++count;
    });

    t.assert_eq(count, 2);
    return true;
}

bool test_heap_sanity(TestContext& t) {
    std::vector<int> vec;
    for (int i = 0; i < 100; ++i) {
        vec.push_back(i * 2);
    }
    t.assert_eq(vec.size(), 100u);
    t.assert_eq(vec[50], 100);
    return true;
}

int main() {
    Suite s("umios/kernel");

    s.section("Task Creation & Priority");
    s.run("task creation and priority", test_task_creation_priority);

    s.section("Timer Queue");
    s.run("timer queue", test_timer_queue);

    s.section("Notifications");
    s.run("notifications", test_notifications);

    s.section("wait_block starvation fix");
    s.run("wait_block starvation fix", test_wait_block_starvation_fix);

    s.section("Critical Section");
    s.run("critical section", test_critical_section);

    s.section("delete_task");
    s.run("delete_task", test_delete_task);

    s.section("IPI");
    s.run("ipi", test_ipi);

    s.section("Statistics");
    s.run("stopwatch and load monitor", test_stopwatch_and_load);

    s.section("SpscQueue");
    s.run("spsc queue", test_spsc_queue);

    s.section("suspend_task");
    s.run("suspend_task", test_suspend_task);

    s.section("Priority Scheduling");
    s.run("priority scheduling", test_priority_scheduling);

    s.section("User Round-Robin");
    s.run("user round-robin", test_user_round_robin);

    s.section("Core Affinity");
    s.run("core affinity", test_core_affinity);

    s.section("Task Iteration");
    s.run("task iteration", test_task_iteration);

    s.section("Heap sanity");
    s.run("heap sanity", test_heap_sanity);

    return s.summary();
}
