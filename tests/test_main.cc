// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief Test runner entry point for umitest self-tests.
/// @author Shota Moriguchi @tekitounix

#include "test_fixture.hh"

int main() {
    umi::test::Suite suite("umitest");

    umitest::test::run_check_tests(suite);
    umitest::test::run_format_tests(suite);
    umitest::test::run_context_tests(suite);
    umitest::test::run_suite_tests(suite);
    umitest::test::run_reporter_tests(suite);

    return suite.summary();
}
