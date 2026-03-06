// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief Tests for Suite workflow: counting, sections, inline checks, summary.
/// @author Shota Moriguchi @tekitounix
/// @details Verifies that Suite correctly tracks pass/fail counts, that
///          run() and check() interact properly, and that summary() returns
///          the expected exit code.

#include <numbers>

#include "test_fixture.hh"

namespace umitest::test {
namespace {

using umi::test::Suite;
using umi::test::TestContext;

// =============================================================================
// Suite pass/fail counting via sub-suites
// =============================================================================

/// @brief Verify a Suite with only passing tests returns 0 from summary().
bool test_all_pass_suite(TestContext& t) {
    Suite sub("sub-pass");
    sub.run("pass1", [](TestContext& ctx) {
        ctx.assert_true(true);
        return true;
    });
    sub.run("pass2", [](TestContext& ctx) {
        ctx.assert_eq(1, 1);
        return true;
    });
    sub.check(true);

    // summary() should return 0 when all pass
    return t.assert_eq(sub.summary(), 0);
}

/// @brief Verify a Suite with a failing run() returns 1 from summary().
bool test_failure_detected_via_return(TestContext& t) {
    Suite sub("sub-fail-return");
    sub.run("returns-false", [](TestContext&) {
        return false; // explicitly fail
    });
    return t.assert_eq(sub.summary(), 1);
}

/// @brief Verify a Suite detects failure via assert inside run().
bool test_failure_detected_via_assert(TestContext& t) {
    Suite sub("sub-fail-assert");
    sub.run("assert-fails", [](TestContext& ctx) {
        ctx.assert_eq(1, 2); // intentional failure
        return true;         // even returns true — context failure overrides
    });
    return t.assert_eq(sub.summary(), 1);
}

/// @brief Verify check() failures are tracked.
bool test_check_failure_counted(TestContext& t) {
    Suite sub("sub-check-fail");
    sub.check(false, "intentional");
    return t.assert_eq(sub.summary(), 1);
}

/// @brief Verify mixed pass and fail counts.
bool test_mixed_pass_fail(TestContext& t) {
    Suite sub("sub-mixed");
    sub.check(true);
    sub.check(true);
    sub.check(false, "intentional");
    sub.run("pass", [](TestContext&) { return true; });
    sub.run("fail", [](TestContext&) { return false; });

    // 3 pass (2 checks + 1 run), 2 fail (1 check + 1 run)
    return t.assert_eq(sub.summary(), 1);
}

// =============================================================================
// Inline check variants
// =============================================================================

bool test_inline_checks_comprehensive(TestContext& t) {
    Suite sub("sub-inline");
    bool all_ok = true;

    all_ok &= sub.check(true);
    all_ok &= sub.check(1 > 0, "one positive");
    all_ok &= sub.check_eq(10, 10);
    all_ok &= sub.check_ne(1, 2);
    all_ok &= sub.check_lt(1, 2);
    all_ok &= sub.check_le(2, 2);
    all_ok &= sub.check_gt(3, 2);
    all_ok &= sub.check_ge(3, 3);
    all_ok &= sub.check_near(1.0F, 1.0001F);

    // All inline checks returned true
    bool ok = t.assert_true(all_ok, "all inline checks passed");
    // Sub-suite should also report all pass
    ok &= t.assert_eq(sub.summary(), 0);
    return ok;
}

// =============================================================================
// Lambda patterns (real-world usage)
// =============================================================================

bool test_lambda_captures(TestContext& t) {
    int const value = 42;
    double pi = std::numbers::pi;

    Suite sub("sub-lambda");
    sub.run("capture-value", [value](TestContext& ctx) {
        ctx.assert_eq(value, 42);
        return true;
    });
    sub.run("capture-ref", [&pi](TestContext& ctx) {
        ctx.assert_near(pi, std::numbers::pi);
        return true;
    });
    return t.assert_eq(sub.summary(), 0);
}

// =============================================================================
// Multi-section organization
// =============================================================================

bool test_multi_section_workflow(TestContext& t) {
    Suite sub("sub-sections");

    Suite::section("Setup");
    sub.run("init", [](TestContext& ctx) {
        ctx.assert_true(true, "setup ok");
        return true;
    });

    Suite::section("Processing");
    sub.run("step1", [](TestContext& ctx) {
        ctx.assert_eq(1 + 1, 2);
        return true;
    });
    sub.run("step2", [](TestContext& ctx) {
        ctx.assert_gt(100, 0);
        return true;
    });

    Suite::section("Cleanup");
    sub.check(true, "cleanup ok");

    return t.assert_eq(sub.summary(), 0);
}

// =============================================================================
// TestContext has_failed / clear_failed behavior
// =============================================================================

bool test_context_failed_flag(TestContext& t) {
    // TestContext starts as not-failed
    bool ok = t.assert_true(!t.has_failed(), "initially not failed");

    // After a passing assert, still not failed
    t.assert_true(true);
    ok &= t.assert_true(!t.has_failed(), "still not failed after pass");

    return ok;
}

// =============================================================================
// check_near failure path
// =============================================================================

bool test_check_near_failure(TestContext& t) {
    Suite sub("sub-check-near-fail");
    // Values differ by 1.0, well outside default eps=0.001
    bool result = sub.check_near(1.0, 2.0);

    bool ok = t.assert_false(result, "check_near should return false");
    ok &= t.assert_eq(sub.summary(), 1);
    return ok;
}

// =============================================================================
// assert_near failure detection via has_failed
// =============================================================================

bool test_assert_near_failure_detected(TestContext& t) {
    Suite sub("sub-assert-near-fail");
    sub.run("near-fails", [](TestContext& ctx) {
        ctx.assert_near(1.0, 100.0); // intentional failure
        return true;                 // context failure overrides
    });
    return t.assert_eq(sub.summary(), 1);
}

// =============================================================================
// check_false
// =============================================================================

bool test_check_false_basic(TestContext& t) {
    Suite sub("sub-check-false");
    bool all_ok = true;
    all_ok &= sub.check_false(false);
    all_ok &= sub.check_false(1 == 2);

    bool ok = t.assert_true(all_ok, "all check_false calls passed");
    ok &= t.assert_eq(sub.summary(), 0);
    return ok;
}

} // namespace

void run_suite_workflow_tests(umi::test::Suite& suite) {
    Suite::section("Suite counting");
    suite.run("all-pass sub-suite returns 0", test_all_pass_suite);
    suite.run("failure via return false", test_failure_detected_via_return);
    suite.run("failure via assert", test_failure_detected_via_assert);
    suite.run("check failure counted", test_check_failure_counted);
    suite.run("mixed pass/fail", test_mixed_pass_fail);

    Suite::section("Inline check variants");
    suite.run("comprehensive checks", test_inline_checks_comprehensive);

    Suite::section("Lambda patterns");
    suite.run("captures", test_lambda_captures);

    Suite::section("Multi-section workflow");
    suite.run("section organization", test_multi_section_workflow);

    Suite::section("TestContext behavior");
    suite.run("failed flag tracking", test_context_failed_flag);

    Suite::section("Near failure paths");
    suite.run("check_near failure", test_check_near_failure);
    suite.run("assert_near failure detected", test_assert_near_failure_detected);

    Suite::section("check_false");
    suite.run("basic false checks", test_check_false_basic);
}

} // namespace umitest::test
