#pragma once

// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief Tests for FailureView / SummaryView structure and reporter concept.
/// @author Shota Moriguchi @tekitounix

#include <array>
#include <source_location>

#include <umitest/failure.hh>
#include <umitest/reporter.hh>
#include <umitest/reporters/null.hh>
#include <umitest/reporters/plain.hh>
#include <umitest/reporters/stdio.hh>
#include <umitest/test.hh>

namespace umitest::test {
using umi::test::TestContext;

inline void run_reporter_tests(umi::test::Suite& suite) {
    suite.section("reporter");

    suite.run("NullReporter satisfies ReporterLike",
              [](TestContext& t) { t.is_true(umi::test::ReporterLike<umi::test::NullReporter>); });

    suite.run("StdioReporter satisfies ReporterLike",
              [](TestContext& t) { t.is_true(umi::test::ReporterLike<umi::test::StdioReporter>); });

    suite.run("PlainReporter satisfies ReporterLike",
              [](TestContext& t) { t.is_true(umi::test::ReporterLike<umi::test::PlainReporter>); });

    suite.run("FailureView fields", [](TestContext& t) {
        auto loc = std::source_location::current();
        std::array<const char*, 1> notes_arr = {"test note"};
        const umi::test::FailureView fv{
            .test_name = "test1",
            .loc = loc,
            .is_fatal = true,
            .kind = "eq",
            .lhs = "1",
            .rhs = "2",
            .extra = nullptr,
            .notes = {notes_arr.data(), notes_arr.size()},
        };

        t.eq(fv.test_name, "test1");
        t.is_true(fv.is_fatal);
        t.eq(fv.kind, "eq");
        t.eq(fv.lhs, "1");
        t.eq(fv.rhs, "2");
        t.eq(fv.extra, static_cast<const char*>(nullptr));
        t.eq(static_cast<int>(fv.notes.size()), 1);
        t.eq(fv.notes[0], "test note");
    });

    suite.run("op_for_kind maps all check kinds", [](TestContext& t) {
        using umi::test::op_for_kind;
        t.eq(op_for_kind("eq"), "==");
        t.eq(op_for_kind("ne"), "!=");
        t.eq(op_for_kind("lt"), "<");
        t.eq(op_for_kind("le"), "<=");
        t.eq(op_for_kind("gt"), ">");
        t.eq(op_for_kind("ge"), ">=");
        t.eq(op_for_kind("near"), "~=");
        t.eq(op_for_kind("true"), "is true");
        t.eq(op_for_kind("false"), "is false");
        t.eq(op_for_kind("throws"), "throws");
        t.eq(op_for_kind("throws_as"), "throws_as");
        t.eq(op_for_kind("nothrow"), "nothrow");
        t.eq(op_for_kind("str_contains"), "contains");
        t.eq(op_for_kind("str_starts_with"), "starts_with");
        t.eq(op_for_kind("str_ends_with"), "ends_with");
        t.eq(op_for_kind("unknown"), "?");
    });

    suite.run("SummaryView fields", [](TestContext& t) {
        const umi::test::SummaryView sv{
            .suite_name = "test_suite",
            .cases_passed = 10,
            .cases_failed = 2,
            .assertions_checked = 50,
            .assertions_failed = 3,
        };

        t.eq(sv.suite_name, "test_suite");
        t.eq(sv.cases_passed, 10);
        t.eq(sv.cases_failed, 2);
        t.eq(sv.assertions_checked, 50);
        t.eq(sv.assertions_failed, 3);
    });
}

} // namespace umitest::test
