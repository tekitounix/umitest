// SPDX-License-Identifier: MIT
/// @file
/// @brief Statistics aggregation and runner behavior tests.

#include <array>
#include <limits>

#include "test_fixture.hh"

namespace umibench::test {
namespace {

using umi::bench::compute_stats;
using umi::bench::Runner;
using umi::test::TestContext;

bool test_stats_single_sample(TestContext& t) {
    const std::array<std::uint32_t, 1> samples = {42};
    const auto stats = compute_stats(samples);

    bool ok = true;
    ok &= t.assert_eq(stats.min, 42u);
    ok &= t.assert_eq(stats.max, 42u);
    ok &= t.assert_eq(stats.mean, 42.0);
    ok &= t.assert_eq(stats.median, 42u);
    ok &= t.assert_eq(stats.samples, 1u);
    ok &= t.assert_eq(stats.stddev, 0.0);
    ok &= t.assert_eq(stats.cv(), 0.0);
    return ok;
}

bool test_stats_odd_samples(TestContext& t) {
    const std::array<std::uint32_t, 5> samples = {5, 1, 3, 4, 2};
    const auto stats = compute_stats(samples);

    bool ok = true;
    ok &= t.assert_eq(stats.min, 1u);
    ok &= t.assert_eq(stats.max, 5u);
    ok &= t.assert_eq(stats.mean, 3.0);
    ok &= t.assert_eq(stats.median, 3u);
    ok &= t.assert_eq(stats.samples, 5u);
    ok &= t.assert_gt(stats.stddev, 1.4);
    ok &= t.assert_lt(stats.stddev, 1.5);
    ok &= t.assert_gt(stats.cv(), 47.0);
    ok &= t.assert_lt(stats.cv(), 48.0);
    return ok;
}

bool test_stats_even_samples(TestContext& t) {
    const std::array<std::uint32_t, 4> samples = {1, 4, 2, 3};
    const auto stats = compute_stats(samples);

    bool ok = true;
    ok &= t.assert_eq(stats.min, 1u);
    ok &= t.assert_eq(stats.max, 4u);
    ok &= t.assert_eq(stats.mean, 2.5);
    ok &= t.assert_eq(stats.median, 2u);
    ok &= t.assert_eq(stats.samples, 4u);
    ok &= t.assert_gt(stats.stddev, 1.1);
    ok &= t.assert_lt(stats.stddev, 1.2);
    return ok;
}

bool test_stats_iterations_stored(TestContext& t) {
    const std::array<std::uint32_t, 3> samples = {10, 20, 30};
    const auto stats = compute_stats(samples, 100);
    return t.assert_eq(stats.iterations, 100u);
}

bool test_stats_all_same(TestContext& t) {
    const std::array<std::uint32_t, 4> samples = {7, 7, 7, 7};
    const auto stats = compute_stats(samples);

    bool ok = true;
    ok &= t.assert_eq(stats.min, 7u);
    ok &= t.assert_eq(stats.max, 7u);
    ok &= t.assert_eq(stats.mean, 7.0);
    ok &= t.assert_eq(stats.median, 7u);
    ok &= t.assert_eq(stats.stddev, 0.0);
    ok &= t.assert_eq(stats.cv(), 0.0);
    return ok;
}

bool test_stats_large_values(TestContext& t) {
    const std::array<std::uint64_t, 3> samples = {1'000'000'000ULL, 2'000'000'000ULL, 3'000'000'000ULL};
    const auto stats = compute_stats(samples);

    bool ok = true;
    ok &= t.assert_eq(stats.min, 1'000'000'000ULL);
    ok &= t.assert_eq(stats.max, 3'000'000'000ULL);
    ok &= t.assert_eq(stats.mean, 2'000'000'000.0);
    ok &= t.assert_eq(stats.sum, 6'000'000'000.0);
    return ok;
}

bool test_stats_even_median_no_overflow(TestContext& t) {
    const std::array<std::uint64_t, 2> samples = {std::numeric_limits<std::uint64_t>::max(),
                                                  std::numeric_limits<std::uint64_t>::max()};
    const auto stats = compute_stats(samples);
    return t.assert_eq(stats.median, std::numeric_limits<std::uint64_t>::max());
}

bool test_runner_calibrate(TestContext& t) {
    TestTimer::enable();
    Runner<TestTimer> runner;
    runner.calibrate<64>();
    return t.assert_ge(runner.get_baseline(), 0u);
}

bool test_runner_calibrate_returns_self(TestContext& t) {
    TestTimer::enable();
    Runner<TestTimer> runner;
    auto& ref = runner.calibrate<32>();
    return t.assert_eq(&ref, &runner);
}

bool test_runner_run_single_iteration(TestContext& t) {
    TestTimer::enable();
    Runner<TestTimer> runner;
    runner.calibrate<32>();

    const auto stats = runner.run<16>([] {
        volatile int x = 0;
        x += 1;
        (void)x;
    });

    bool ok = true;
    ok &= t.assert_eq(stats.samples, 16u);
    ok &= t.assert_eq(stats.iterations, 1u);
    ok &= t.assert_le(stats.min, stats.max);
    ok &= t.assert_le(stats.min, stats.mean);
    ok &= t.assert_le(stats.mean, stats.max);
    return ok;
}

bool test_runner_run_multiple_iterations(TestContext& t) {
    TestTimer::enable();
    Runner<TestTimer> runner;
    runner.calibrate<32>();

    const auto stats = runner.run<16>(100, [] {
        volatile int x = 0;
        x += 1;
        (void)x;
    });

    bool ok = true;
    ok &= t.assert_eq(stats.samples, 16u);
    ok &= t.assert_eq(stats.iterations, 100u);
    ok &= t.assert_le(stats.min, stats.max);
    return ok;
}

bool test_runner_default_samples(TestContext& t) {
    TestTimer::enable();
    Runner<TestTimer, 32> runner;
    runner.calibrate();

    const auto stats = runner.run([] {});
    return t.assert_eq(stats.samples, 32u);
}

bool test_runner_run_invocation_count_single(TestContext& t) {
    TestTimer::enable();
    Runner<TestTimer> runner;
    volatile std::uint32_t calls = 0;

    (void)runner.run<17>([&] { calls += 1; });
    return t.assert_eq(calls, 17u);
}

bool test_runner_run_invocation_count_multiple(TestContext& t) {
    TestTimer::enable();
    Runner<TestTimer> runner;
    volatile std::uint32_t calls = 0;
    constexpr std::uint32_t iterations = 13;

    (void)runner.run<17>(iterations, [&] { calls += 1; });
    return t.assert_eq(calls, 17u * iterations);
}

} // namespace

/// @brief Register stats and runner test cases.
/// @param suite Test suite to register into.
void run_stats_runner_tests(umi::test::Suite& suite) {
    umi::test::Suite::section("Stats");
    suite.run("stats_single_sample", test_stats_single_sample);
    suite.run("stats_odd_samples", test_stats_odd_samples);
    suite.run("stats_even_samples", test_stats_even_samples);
    suite.run("stats_iterations_stored", test_stats_iterations_stored);
    suite.run("stats_all_same", test_stats_all_same);
    suite.run("stats_large_values", test_stats_large_values);
    suite.run("stats_even_median_no_overflow", test_stats_even_median_no_overflow);

    umi::test::Suite::section("Runner");
    suite.run("runner_calibrate", test_runner_calibrate);
    suite.run("runner_calibrate_returns_self", test_runner_calibrate_returns_self);
    suite.run("runner_run_single_iteration", test_runner_run_single_iteration);
    suite.run("runner_run_multiple_iterations", test_runner_run_multiple_iterations);
    suite.run("runner_default_samples", test_runner_default_samples);
    suite.run("runner_run_invocation_count_single", test_runner_run_invocation_count_single);
    suite.run("runner_run_invocation_count_multiple", test_runner_run_invocation_count_multiple);
}

} // namespace umibench::test
