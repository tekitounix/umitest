// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief Structured test using sections and inline checks.

#include <umitest/test.hh>

int main() {
    umi::test::Suite suite("check_style_demo");

    // --- Sections visually group related checks ---

    umi::test::Suite::section("Arithmetic");

    suite.check_eq(2 + 3, 5);
    suite.check_ne(2 * 3, 5);
    suite.check_lt(1, 100);

    umi::test::Suite::section("String-like comparisons");

    const char* hello = "hello";
    suite.check(hello != nullptr, "pointer is non-null");
    suite.check_eq(hello[0], 'h');

    umi::test::Suite::section("Near checks");

    suite.check_near(1.0 / 3.0, 0.3333, 0.001);

    return suite.summary();
}
