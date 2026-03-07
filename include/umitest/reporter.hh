#pragma once

// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief ReporterLike concept — interface contract for test reporters.
/// @author Shota Moriguchi @tekitounix

#include <concepts>

#include <umitest/failure.hh>

namespace umi::test {

/// @brief Concept for test reporter types.
/// @details Reporters receive structured FailureView/SummaryView data and render output freely.
template <typename R>
concept ReporterLike =
    std::move_constructible<R> && requires(R r, const char* s, const FailureView& fv, const SummaryView& sv) {
        r.section(s);
        r.test_begin(s);
        r.test_pass(s);
        r.test_fail(s);
        r.report_failure(fv);
        r.summary(sv);
    };

} // namespace umi::test
