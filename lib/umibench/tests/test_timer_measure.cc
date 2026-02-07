// SPDX-License-Identifier: MIT
/// @file
/// @brief Timer and measurement primitive tests.

#include <limits>

#include "test_fixture.hh"

namespace umibench::test {
namespace {

using umi::bench::measure;
using umi::bench::measure_corrected;
using umi::bench::measure_corrected_n;
using umi::bench::measure_n;
using umi::bench::TimerLike;
using umi::test::TestContext;

bool test_chrono_timer_concept(TestContext& t) {
    static_assert(TimerLike<TestTimer>);
    return t.assert_true(true);
}

bool test_chrono_timer_enable(TestContext& t) {
    TestTimer::enable();
    return t.assert_true(true, "enable() should not throw");
}

bool test_chrono_timer_monotonic(TestContext& t) {
    TestTimer::enable();
    const auto t1 = TestTimer::now();
    volatile int x = 0;
    for (int i = 0; i < 1000; ++i) {
        x += i;
    }
    (void)x;
    const auto t2 = TestTimer::now();
    return t.assert_ge(t2, t1);
}

bool test_measure_returns_positive(TestContext& t) {
    TestTimer::enable();
    const auto elapsed = measure<TestTimer>([] {
        volatile int x = 0;
        for (int i = 0; i < 100; ++i) {
            x += i;
        }
        (void)x;
    });
    return t.assert_ge(elapsed, 0u);
}

bool test_measure_n_multiplies(TestContext& t) {
    TestTimer::enable();
    volatile std::uint32_t calls = 0;
    constexpr std::uint32_t iterations = 10;
    const auto elapsed = measure_n<TestTimer>([&] { calls += 1; }, iterations);

    bool ok = true;
    ok &= t.assert_eq(calls, iterations);
    ok &= t.assert_ge(elapsed, 0u);
    return ok;
}

bool test_measure_corrected_subtracts_baseline(TestContext& t) {
    TestTimer::enable();
    const typename TestTimer::Counter baseline = std::numeric_limits<typename TestTimer::Counter>::max();
    const auto result = measure_corrected<TestTimer>([] {}, baseline);
    return t.assert_eq(result, 0u);
}

bool test_measure_corrected_n(TestContext& t) {
    TestTimer::enable();
    const typename TestTimer::Counter baseline = 10;
    const auto result = measure_corrected_n<TestTimer>(
        [] {
            volatile int x = 0;
            x += 1;
            (void)x;
        },
        baseline,
        100);
    return t.assert_ge(result, 0u);
}

} // namespace

/// @brief Register timer and measure related test cases.
/// @param suite Test suite to register into.
void run_timer_measure_tests(umi::test::Suite& suite) {
    umi::test::Suite::section("Timer");
    suite.run("chrono_timer_concept", test_chrono_timer_concept);
    suite.run("chrono_timer_enable", test_chrono_timer_enable);
    suite.run("chrono_timer_monotonic", test_chrono_timer_monotonic);

    umi::test::Suite::section("Measure");
    suite.run("measure_returns_positive", test_measure_returns_positive);
    suite.run("measure_n_multiplies", test_measure_n_multiplies);
    suite.run("measure_corrected_subtracts_baseline", test_measure_corrected_subtracts_baseline);
    suite.run("measure_corrected_n", test_measure_corrected_n);
}

} // namespace umibench::test
