// SPDX-License-Identifier: MIT
/// @file
/// @brief Platform, output, and report formatting tests.

#include <limits>

#include "test_fixture.hh"

namespace umibench::test {
namespace {

using umi::bench::OutputLike;
using umi::bench::report;
using umi::bench::report_compact;
using umi::bench::Runner;
using umi::bench::Stats;
using umi::bench::TimerLike;
using umi::test::TestContext;

bool test_platform_types(TestContext& t) {
    static_assert(TimerLike<TestPlatform::Timer>);
    TestPlatform::Output::init();
    return t.assert_true(true);
}

bool test_platform_init(TestContext& t) {
    TestPlatform::init();
    return t.assert_true(true, "Platform::init() is callable");
}

bool test_null_output_concept(TestContext& t) {
    static_assert(OutputLike<umi::bench::NullOutput>);
    return t.assert_true(true);
}

bool test_stdout_output_concept(TestContext& t) {
    static_assert(OutputLike<TestPlatform::Output>);
    return t.assert_true(true);
}

bool test_output_print_double(TestContext& t) {
    umi::bench::NullOutput::print_double(3.14);
    umi::bench::NullOutput::print_double(0.0);
    umi::bench::NullOutput::print_double(-1.5);
    return t.assert_true(true);
}

bool test_output_print_uint64(TestContext& t) {
    umi::bench::NullOutput::print_uint(std::numeric_limits<std::uint64_t>::max());
    return t.assert_true(true);
}

bool test_platform_target_name(TestContext& t) {
    const char* name = TestPlatform::target_name();
    bool ok = true;
    ok &= t.assert_true(name != nullptr, "target_name() should not be null");
    ok &= t.assert_true(name[0] != '\0', "target_name() should not be empty");
    return ok;
}

bool test_platform_timer_unit(TestContext& t) {
    const char* unit = TestPlatform::timer_unit();
    bool ok = true;
    ok &= t.assert_true(unit != nullptr, "timer_unit() should not be null");
    ok &= t.assert_true(unit[0] != '\0', "timer_unit() should not be empty");
    return ok;
}

bool test_report_full_does_not_crash(TestContext& t) {
    Runner<TestTimer> runner;
    runner.calibrate<32>();
    const auto stats = runner.run<16>(10, [] {
        volatile int x = 0;
        x += 1;
        (void)x;
    });
    report<NullPlatform>("test_report", stats);
    return t.assert_true(true);
}

bool test_report_compact_does_not_crash(TestContext& t) {
    Runner<TestTimer> runner;
    runner.calibrate<32>();
    const auto stats = runner.run<16>(10, [] {
        volatile int x = 0;
        x += 1;
        (void)x;
    });
    report_compact<NullPlatform>("test_compact", stats);
    return t.assert_true(true);
}

bool test_report_full_preserves_uint64(TestContext& t) {
    Stats stats{};
    stats.samples = 1;
    stats.iterations = 1;
    stats.min = 5'000'000'000ULL;
    stats.max = 5'000'000'000ULL;
    stats.median = 5'000'000'000ULL;
    stats.mean = 5'000'000'000.0;
    stats.stddev = 0.0;

    CaptureOutput::clear();
    report<CapturePlatform>("u64_report", stats);

    bool ok = true;
    ok &= t.assert_true(CaptureOutput::contains("min=5000000000"), "min should keep uint64 digits");
    ok &= t.assert_true(CaptureOutput::contains("max=5000000000"), "max should keep uint64 digits");
    ok &= t.assert_true(CaptureOutput::contains("median=5000000000"), "median should keep uint64 digits");
    return ok;
}

bool test_report_compact_preserves_uint64(TestContext& t) {
    Stats stats{};
    stats.samples = 1;
    stats.iterations = 2;
    stats.min = 10'000'000'000ULL;
    stats.max = 10'000'000'000ULL;
    stats.median = 10'000'000'000ULL;
    stats.mean = 10'000'000'000.0;
    stats.stddev = 0.0;

    CaptureOutput::clear();
    report_compact<CapturePlatform>("u64_compact", stats);

    bool ok = true;
    ok &= t.assert_true(CaptureOutput::contains("10000000000"), "compact min should keep uint64 digits");
    ok &= t.assert_true(CaptureOutput::contains("net=5000000000/iter"), "compact net should keep uint64 digits");
    return ok;
}

} // namespace

/// @brief Register platform/output/report test cases.
/// @param suite Test suite to register into.
void run_platform_output_report_tests(umi::test::Suite& suite) {
    umi::test::Suite::section("Platform");
    suite.run("platform_types", test_platform_types);
    suite.run("platform_init", test_platform_init);
    suite.run("platform_target_name", test_platform_target_name);
    suite.run("platform_timer_unit", test_platform_timer_unit);

    umi::test::Suite::section("Output");
    suite.run("null_output_concept", test_null_output_concept);
    suite.run("stdout_output_concept", test_stdout_output_concept);
    suite.run("output_print_double", test_output_print_double);
    suite.run("output_print_uint64", test_output_print_uint64);

    umi::test::Suite::section("Report");
    suite.run("report_full_does_not_crash", test_report_full_does_not_crash);
    suite.run("report_compact_does_not_crash", test_report_compact_does_not_crash);
    suite.run("report_full_preserves_uint64", test_report_full_preserves_uint64);
    suite.run("report_compact_preserves_uint64", test_report_compact_preserves_uint64);
}

} // namespace umibench::test
