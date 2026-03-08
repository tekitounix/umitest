#pragma once

// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief PlainReporter — colorless test output via <cstdio>.
/// @author Shota Moriguchi @tekitounix

#include <cstdio>
#include <string_view>

#include <umitest/failure.hh>
#include <umitest/reporter.hh>

namespace umi::test {

/// @brief Reporter that outputs test results to stdout without ANSI colors.
class PlainReporter {
  public:
    void section(const char* title) const { std::printf("\n[%s]\n", title); }

    void test_begin(const char* /*name*/) const {}

    void test_pass(const char* name) const { std::printf("  %s... OK\n", name); }

    void test_fail(const char* name) const { std::printf("  %s... FAIL\n", name); }

    void report_failure(const FailureView& fv) const {
        const char* op = op_for_kind(fv.kind);
        if (fv.lhs != nullptr && fv.rhs != nullptr) {
            std::printf("  FAIL [%s]: expected %s %s %s\n", fv.test_name, fv.lhs, op, fv.rhs);
        } else {
            std::printf("  FAIL [%s]: expected %s\n", fv.test_name, fv.kind);
        }
        std::printf("    at %s:%u\n", fv.loc.file_name(), static_cast<unsigned>(fv.loc.line()));
        if (fv.is_fatal) {
            std::printf("    (fatal: test may have been aborted early)\n");
        }
        for (const auto* note : fv.notes) {
            std::printf("    note: %s\n", note);
        }
        if (fv.extra != nullptr) {
            std::printf("    (%s)\n", fv.extra);
        }
    }

    void summary(const SummaryView& sv) const {
        const int total = sv.cases_passed + sv.cases_failed;
        std::printf("\n=================================\n");
        std::printf("cases: %d/%d passed\n", sv.cases_passed, total);
        std::printf("assertions: %d checked, %d failed\n", sv.assertions_checked, sv.assertions_failed);
        std::printf("=================================\n");
    }

  private:
    static constexpr const char* op_for_kind(const char* kind) {
        const std::string_view k(kind);
        if (k == "eq") {
            return "==";
        }
        if (k == "ne") {
            return "!=";
        }
        if (k == "lt") {
            return "<";
        }
        if (k == "le") {
            return "<=";
        }
        if (k == "gt") {
            return ">";
        }
        if (k == "ge") {
            return ">=";
        }
        if (k == "near") {
            return "~=";
        }
        return "?";
    }
};

static_assert(ReporterLike<PlainReporter>);

} // namespace umi::test
