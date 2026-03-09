// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief Test runner entry point for umitest self-tests.
/// @author Shota Moriguchi @tekitounix

#include "test_check.hh"
#include "test_context.hh"
#include "test_exception.hh"
#include "test_format.hh"
#include "test_reporter.hh"
#include "test_string.hh"
#include "test_suite.hh"

int main() {
    umi::test::Suite suite("umitest");

    umitest::test::run_check_tests(suite);
    umitest::test::run_format_tests(suite);
    umitest::test::run_context_tests(suite);
    umitest::test::run_suite_tests(suite);
    umitest::test::run_reporter_tests(suite);
    umitest::test::run_exception_tests(suite);
    umitest::test::run_string_tests(suite);

    return suite.summary();
}
