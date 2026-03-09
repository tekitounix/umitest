#pragma once

// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief BasicSuite<R> test runner template parameterized by reporter.
/// @author Shota Moriguchi @tekitounix

#include <concepts>
#include <utility>

#include <umitest/context.hh>
#include <umitest/failure.hh>
#include <umitest/reporter.hh>

namespace umi::test {

/// @brief Test runner that collects pass/fail statistics and delegates output to reporter R.
/// @tparam R Reporter satisfying ReporterLike concept.
template <ReporterLike R>
class BasicSuite {
  public:
    /// @pre name is null-terminated and valid for the lifetime of this BasicSuite.
    explicit BasicSuite(const char* name, R reporter = R{}) : suite_name(name), reporter(std::move(reporter)) {}

    /// @brief Begin a named section for visual grouping in output.
    void section(const char* title) { reporter.section(title); }

    /// @brief Run a structured test.
    /// @tparam F Callable taking TestContext& and returning void.
    /// @param test_name Name displayed in output.
    /// @param fn Test body; use ctx.eq() etc. for assertions.
    template <typename F>
        requires requires(F& f, TestContext& ctx) {
            { f(ctx) } -> std::same_as<void>;
        }
    void run(const char* test_name, F&& fn) {
        reporter.test_begin(test_name);
        TestContext ctx(
            test_name,
            [](const FailureView& fv, void* p) { static_cast<BasicSuite*>(p)->reporter.report_failure(fv); },
            this);
        fn(ctx);
        total_checked += ctx.checked_count();
        total_failed_checks += ctx.failed_count();
        if (ctx.ok()) {
            reporter.test_pass(test_name);
            passed++;
        } else {
            reporter.test_fail(test_name);
            failed++;
        }
    }

    /// @brief Print final summary and return exit code.
    /// @return 0 if all passed, 1 if any failed.
    [[nodiscard]] int summary() {
        const SummaryView sv{
            .suite_name = suite_name,
            .cases_passed = passed,
            .cases_failed = failed,
            .assertions_checked = total_checked,
            .assertions_failed = total_failed_checks,
        };
        reporter.summary(sv);
        return failed > 0 ? 1 : 0;
    }

    /// @brief Access the reporter (for self-test / recording reporter pattern).
    [[nodiscard]] const R& get_reporter() const { return reporter; }

  private:
    const char* suite_name;
    R reporter;
    int passed = 0;
    int failed = 0;
    int total_checked = 0;
    int total_failed_checks = 0;
};

} // namespace umi::test
