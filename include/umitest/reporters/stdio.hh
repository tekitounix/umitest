#pragma once

// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief StdioReporter — ANSI-colored test output via <cstdio>.
/// @author Shota Moriguchi @tekitounix

#include <cstdio>
#include <string_view>

#include <umitest/failure.hh>
#include <umitest/reporter.hh>

namespace umi::test {

/// @brief Reporter that outputs ANSI-colored test results to stdout via <cstdio>.
class StdioReporter {
    static constexpr const char* green = "\033[32m";
    static constexpr const char* red = "\033[31m";
    static constexpr const char* cyan = "\033[36m";
    static constexpr const char* reset_code = "\033[0m";

  public:
    void section(const char* title) const { std::printf("\n%s[%s]%s\n", cyan, title, reset_code); }

    void test_begin(const char* /*name*/) const {}

    void test_pass(const char* name) const { std::printf("  %s... %sOK%s\n", name, green, reset_code); }

    void test_fail(const char* name) const { std::printf("  %s... %sFAIL%s\n", name, red, reset_code); }

    void report_failure(const FailureView& fv) const {
        const char* op = op_for_kind(fv.kind);
        if (fv.lhs != nullptr && fv.rhs != nullptr) {
            std::printf("  %sFAIL [%s]: expected %s %s %s%s\n", red, fv.test_name, fv.lhs, op, fv.rhs, reset_code);
        } else {
            std::printf("  %sFAIL [%s]: expected %s%s\n", red, fv.test_name, fv.kind, reset_code);
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
        std::printf("\n%s=================================%s\n", cyan, reset_code);
        if (sv.cases_failed == 0) {
            std::printf("%scases: %d/%d passed%s\n", green, sv.cases_passed, total, reset_code);
        } else {
            std::printf(
                "%scases: %d/%d passed, %d FAILED%s\n", red, sv.cases_passed, total, sv.cases_failed, reset_code);
        }
        std::printf("assertions: %d checked, %d failed\n", sv.assertions_checked, sv.assertions_failed);
        std::printf("%s=================================%s\n", cyan, reset_code);
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

static_assert(ReporterLike<StdioReporter>);

} // namespace umi::test
