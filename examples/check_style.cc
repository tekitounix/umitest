// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief Structured test using sections.
/// @author Shota Moriguchi @tekitounix

#include <umitest/test.hh>

int main() {
    umi::test::Suite suite("sections_demo");

    suite.section("Arithmetic");
    suite.run("addition", [](umi::test::TestContext& t) { t.eq(2 + 3, 5); });
    suite.run("multiplication", [](umi::test::TestContext& t) { t.eq(3 * 4, 12); });

    suite.section("Floating point");
    suite.run("one third", [](umi::test::TestContext& t) { t.near(1.0 / 3.0, 0.3333, 0.001); });

    return suite.summary();
}
