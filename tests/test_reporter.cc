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

#include "test_fixture.hh"

namespace umitest::test {

void run_reporter_tests(umi::test::Suite& s) {
    s.section("reporter");

    s.run("NullReporter satisfies ReporterLike",
          [](auto& t) { t.is_true(umi::test::ReporterLike<umi::test::NullReporter>); });

    s.run("StdioReporter satisfies ReporterLike",
          [](auto& t) { t.is_true(umi::test::ReporterLike<umi::test::StdioReporter>); });

    s.run("PlainReporter satisfies ReporterLike",
          [](auto& t) { t.is_true(umi::test::ReporterLike<umi::test::PlainReporter>); });

    s.run("FailureView fields", [](auto& t) {
        auto loc = std::source_location::current();
        std::array<const char*, 1> notes_arr = {"test note"};
        umi::test::FailureView fv{
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

    s.run("SummaryView fields", [](auto& t) {
        umi::test::SummaryView sv{
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
