#pragma once

// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief FailureView and SummaryView — structured data for reporters.
/// @author Shota Moriguchi @tekitounix

#include <source_location>
#include <span>

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

} // namespace umi::test
