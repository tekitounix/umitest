// SPDX-License-Identifier: MIT
// bench library tests
#include <array>
#include <bench/bench.hh>
#include <bench/core/measure.hh>
#include <bench/core/runner.hh>
#include <bench/core/stats.hh>
#include <bench/platform/host.hh>
#include <bench/timer/chrono.hh>
#include <test.hh>

using namespace umi::bench;
using namespace umi::test;

// =============================================================================
// Timer concept tests
// =============================================================================

bool test_chrono_timer_concept(TestContext& t) {
    // ChronoTimer は TimerLike を満たす
    static_assert(TimerLike<ChronoTimer>);
    return t.assert_true(true);
}

bool test_chrono_timer_enable(TestContext& t) {
    ChronoTimer::enable();
    return t.assert_true(true, "enable() should not throw");
}

bool test_chrono_timer_monotonic(TestContext& t) {
    ChronoTimer::enable();
    auto t1 = ChronoTimer::now();
    volatile int x = 0;
    for (int i = 0; i < 1000; ++i)
        x += i;
    (void)x;
    auto t2 = ChronoTimer::now();
    return t.assert_ge(t2, t1);
}

// =============================================================================
// Measure tests
// =============================================================================

bool test_measure_returns_positive(TestContext& t) {
    ChronoTimer::enable();
    auto elapsed = measure<ChronoTimer>([] {
        volatile int x = 0;
        for (int i = 0; i < 100; ++i)
            x += i;
        (void)x;
    });
    // 0以上（空処理でも0になりうる）
    return t.assert_ge(elapsed, 0u);
}

bool test_measure_n_multiplies(TestContext& t) {
    ChronoTimer::enable();
    auto single = measure<ChronoTimer>([] {
        volatile int x = 0;
        for (int i = 0; i < 100; ++i)
            x += i;
        (void)x;
    });
    auto multiple = measure_n<ChronoTimer>(
        [] {
            volatile int x = 0;
            for (int i = 0; i < 100; ++i)
                x += i;
            (void)x;
        },
        10);
    // N回実行は単体より大きいか同等
    return t.assert_ge(multiple, single);
}

bool test_measure_corrected_subtracts_baseline(TestContext& t) {
    ChronoTimer::enable();
    ChronoTimer::Counter baseline = 100;
    auto result = measure_corrected<ChronoTimer>([] {}, baseline);
    // baseline以下なら0になる
    return t.assert_eq(result, 0u);
}

bool test_measure_corrected_n(TestContext& t) {
    ChronoTimer::enable();
    ChronoTimer::Counter baseline = 10;
    auto result = measure_corrected_n<ChronoTimer>(
        [] {
            volatile int x = 0;
            x += 1;
            (void)x;
        },
        baseline,
        100);
    // 結果は0以上
    return t.assert_ge(result, 0u);
}

// =============================================================================
// Stats tests
// =============================================================================

bool test_stats_single_sample(TestContext& t) {
    std::array<std::uint32_t, 1> samples = {42};
    auto stats = compute_stats(samples);

    bool ok = true;
    ok &= t.assert_eq(stats.min, 42u);
    ok &= t.assert_eq(stats.max, 42u);
    ok &= t.assert_eq(stats.mean, 42.0); // mean is now double
    ok &= t.assert_eq(stats.median, 42u);
    ok &= t.assert_eq(stats.samples, 1u);
    ok &= t.assert_eq(stats.stddev, 0.0); // single sample has no deviation
    ok &= t.assert_eq(stats.cv(), 0.0);   // CV is 0 when no deviation
    return ok;
}

bool test_stats_odd_samples(TestContext& t) {
    std::array<std::uint32_t, 5> samples = {5, 1, 3, 4, 2};
    auto stats = compute_stats(samples);

    bool ok = true;
    ok &= t.assert_eq(stats.min, 1u);
    ok &= t.assert_eq(stats.max, 5u);
    ok &= t.assert_eq(stats.mean, 3.0);  // (1+2+3+4+5)/5 = 3.0 (double)
    ok &= t.assert_eq(stats.median, 3u); // sorted: 1,2,3,4,5 -> middle is 3
    ok &= t.assert_eq(stats.samples, 5u);
    // stddev for [1,2,3,4,5]: sqrt(((4+1+0+1+4)/5)) = sqrt(2) ≈ 1.414
    ok &= t.assert_gt(stats.stddev, 1.4);
    ok &= t.assert_lt(stats.stddev, 1.5);
    // CV = (1.414/3)*100 ≈ 47.1%
    ok &= t.assert_gt(stats.cv(), 47.0);
    ok &= t.assert_lt(stats.cv(), 48.0);
    return ok;
}

bool test_stats_even_samples(TestContext& t) {
    std::array<std::uint32_t, 4> samples = {1, 4, 2, 3};
    auto stats = compute_stats(samples);

    bool ok = true;
    ok &= t.assert_eq(stats.min, 1u);
    ok &= t.assert_eq(stats.max, 4u);
    ok &= t.assert_eq(stats.mean, 2.5);  // (1+2+3+4)/4 = 2.5 (double)
    ok &= t.assert_eq(stats.median, 2u); // sorted: 1,2,3,4 -> (2+3)/2 = 2
    ok &= t.assert_eq(stats.samples, 4u);
    // stddev for [1,2,3,4]: sqrt(((2.25+0.25+0.25+2.25)/4)) = sqrt(1.25) ≈ 1.118
    ok &= t.assert_gt(stats.stddev, 1.1);
    ok &= t.assert_lt(stats.stddev, 1.2);
    return ok;
}

bool test_stats_iterations_stored(TestContext& t) {
    std::array<std::uint32_t, 3> samples = {10, 20, 30};
    auto stats = compute_stats(samples, 100);
    return t.assert_eq(stats.iterations, 100u);
}

bool test_stats_all_same(TestContext& t) {
    std::array<std::uint32_t, 4> samples = {7, 7, 7, 7};
    auto stats = compute_stats(samples);

    bool ok = true;
    ok &= t.assert_eq(stats.min, 7u);
    ok &= t.assert_eq(stats.max, 7u);
    ok &= t.assert_eq(stats.mean, 7.0); // double mean
    ok &= t.assert_eq(stats.median, 7u);
    ok &= t.assert_eq(stats.stddev, 0.0); // no deviation when all same
    ok &= t.assert_eq(stats.cv(), 0.0);   // CV is 0 when no deviation
    return ok;
}

bool test_stats_large_values(TestContext& t) {
    std::array<std::uint64_t, 3> samples = {1'000'000'000ULL, 2'000'000'000ULL, 3'000'000'000ULL};
    auto stats = compute_stats(samples);

    bool ok = true;
    ok &= t.assert_eq(stats.min, 1'000'000'000ULL);
    ok &= t.assert_eq(stats.max, 3'000'000'000ULL);
    ok &= t.assert_eq(stats.mean, 2'000'000'000.0); // double mean
    ok &= t.assert_eq(stats.sum, 6'000'000'000.0);  // sum field
    return ok;
}

// =============================================================================
// Runner tests
// =============================================================================

bool test_runner_calibrate(TestContext& t) {
    ChronoTimer::enable();
    Runner<ChronoTimer> runner;
    runner.calibrate<64>();
    // baseline は 0 以上
    return t.assert_ge(runner.get_baseline(), 0u);
}

bool test_runner_calibrate_returns_self(TestContext& t) {
    ChronoTimer::enable();
    Runner<ChronoTimer> runner;
    auto& ref = runner.calibrate<32>();
    return t.assert_eq(&ref, &runner);
}

bool test_runner_run_single_iteration(TestContext& t) {
    ChronoTimer::enable();
    Runner<ChronoTimer> runner;
    runner.calibrate<32>();

    auto stats = runner.run<16>([] {
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
    ChronoTimer::enable();
    Runner<ChronoTimer> runner;
    runner.calibrate<32>();

    auto stats = runner.run<16>(100, [] {
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
    ChronoTimer::enable();
    Runner<ChronoTimer, 32> runner; // DefaultSamples = 32
    runner.calibrate();

    auto stats = runner.run([] {});
    return t.assert_eq(stats.samples, 32u);
}

// =============================================================================
// Platform tests
// =============================================================================

bool test_host_platform_types(TestContext& t) {
    static_assert(TimerLike<Host::Timer>);
    // OutputLike は init/putc/puts/print_uint を持つ
    Host::Output::init();
    return t.assert_true(true);
}

bool test_host_platform_init(TestContext& t) {
    Host::init();
    return t.assert_true(true, "Host::init() should not throw");
}

// =============================================================================
// Integration tests
// =============================================================================

bool test_full_benchmark_workflow(TestContext& t) {
    using Platform = Host;
    Platform::init();

    Runner<Platform::Timer> runner;
    runner.calibrate<64>();

    volatile int counter = 0;
    auto stats = runner.run<32>(10, [&] { counter += 1; });

    bool ok = true;
    ok &= t.assert_eq(stats.samples, 32u);
    ok &= t.assert_eq(stats.iterations, 10u);
    ok &= t.assert_le(stats.min, stats.median);
    ok &= t.assert_le(stats.median, stats.max);
    return ok;
}

// =============================================================================
// Main
// =============================================================================

int main() {
    Suite s("bench");

    s.section("Timer");
    s.run("chrono_timer_concept", test_chrono_timer_concept);
    s.run("chrono_timer_enable", test_chrono_timer_enable);
    s.run("chrono_timer_monotonic", test_chrono_timer_monotonic);

    s.section("Measure");
    s.run("measure_returns_positive", test_measure_returns_positive);
    s.run("measure_n_multiplies", test_measure_n_multiplies);
    s.run("measure_corrected_subtracts_baseline", test_measure_corrected_subtracts_baseline);
    s.run("measure_corrected_n", test_measure_corrected_n);

    s.section("Stats");
    s.run("stats_single_sample", test_stats_single_sample);
    s.run("stats_odd_samples", test_stats_odd_samples);
    s.run("stats_even_samples", test_stats_even_samples);
    s.run("stats_iterations_stored", test_stats_iterations_stored);
    s.run("stats_all_same", test_stats_all_same);
    s.run("stats_large_values", test_stats_large_values);

    s.section("Runner");
    s.run("runner_calibrate", test_runner_calibrate);
    s.run("runner_calibrate_returns_self", test_runner_calibrate_returns_self);
    s.run("runner_run_single_iteration", test_runner_run_single_iteration);
    s.run("runner_run_multiple_iterations", test_runner_run_multiple_iterations);
    s.run("runner_default_samples", test_runner_default_samples);

    s.section("Platform");
    s.run("host_platform_types", test_host_platform_types);
    s.run("host_platform_init", test_host_platform_init);

    s.section("Integration");
    s.run("full_benchmark_workflow", test_full_benchmark_workflow);

    return s.summary();
}
