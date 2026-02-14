// SPDX-License-Identifier: MIT
/// @file
/// @brief Demonstration of all assertion methods in umitest.

#include <umitest/test.hh>

int main() {
    umi::test::Suite suite("assertions_demo");

    suite.run("equality and inequality", [](umi::test::TestContext& ctx) {
        ctx.assert_eq(42, 42);
        ctx.assert_ne(1, 2);
        return true;
    });

    suite.run("ordering comparisons", [](umi::test::TestContext& ctx) {
        ctx.assert_lt(1, 2);
        ctx.assert_le(2, 2);
        ctx.assert_gt(5, 3);
        ctx.assert_ge(3, 3);
        return true;
    });

    suite.run("approximate floating-point equality", [](umi::test::TestContext& ctx) {
        ctx.assert_near(3.14, 3.14159, 0.01);
        ctx.assert_near(0.1 + 0.2, 0.3, 0.001);
        return true;
    });

    suite.run("boolean assertion", [](umi::test::TestContext& ctx) {
        ctx.assert_true(true, "should be true");
        ctx.assert_true(!false, "not-false is true");
        return true;
    });

    return suite.summary();
}
