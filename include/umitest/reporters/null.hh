#pragma once

// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief NullReporter — discards all output. Zero dependency.
/// @author Shota Moriguchi @tekitounix

#include <umitest/failure.hh>
#include <umitest/reporter.hh>

namespace umi::test {

/// @brief Reporter that discards all output. Useful for testing Suite semantics.
class NullReporter {
  public:
    void section(const char* /*title*/) const {}
    void test_begin(const char* /*name*/) const {}
    void test_pass(const char* /*name*/) const {}
    void test_fail(const char* /*name*/) const {}
    void report_failure(const FailureView& /*fv*/) const {}
    void summary(const SummaryView& /*sv*/) const {}
};

static_assert(ReporterLike<NullReporter>);

} // namespace umi::test
