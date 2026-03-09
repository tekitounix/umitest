#pragma once

// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief FailureView and SummaryView — structured data for reporters.
/// @author Shota Moriguchi @tekitounix

#include <source_location>
#include <span>
#include <string_view>

namespace umi::test {

/// @brief Structured failure information passed to reporter.
/// @details Lifetime: valid only during the report_failure() call.
///          Reporter must copy fields if it needs to retain them.
struct FailureView {
    const char* test_name;
    std::source_location loc;
    bool is_fatal;
    const char* kind;
    const char* lhs;
    const char* rhs;
    const char* extra;
    std::span<const char* const> notes;
};

/// @brief Structured summary passed to reporter.
struct SummaryView {
    const char* suite_name;
    int cases_passed;
    int cases_failed;
    int assertions_checked;
    int assertions_failed;
};

/// @brief Convert check kind string to operator symbol for diagnostic output.
constexpr const char* op_for_kind(const char* kind) {
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
    if (k == "true") {
        return "is true";
    }
    if (k == "false") {
        return "is false";
    }
    if (k == "throws") {
        return "throws";
    }
    if (k == "throws_as") {
        return "throws_as";
    }
    if (k == "nothrow") {
        return "nothrow";
    }
    if (k == "str_contains") {
        return "contains";
    }
    if (k == "str_starts_with") {
        return "starts_with";
    }
    if (k == "str_ends_with") {
        return "ends_with";
    }
    return "?";
}

} // namespace umi::test
