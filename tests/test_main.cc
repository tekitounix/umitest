// SPDX-License-Identifier: MIT
/// @file
/// @brief Test runner entry point for umitest self-tests.

#include "test_fixture.hh"

int main() {
    umi::test::Suite suite("umitest");

    umitest::test::run_assertion_tests(suite);
    umitest::test::run_suite_workflow_tests(suite);
    umitest::test::run_format_tests(suite);

    return suite.summary();
}
