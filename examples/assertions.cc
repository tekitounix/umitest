// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief Demonstration of all assertion methods in umitest.
/// @author Shota Moriguchi @tekitounix

#include <umitest/test.hh>

int main() {
    umi::test::Suite suite("assertions_demo");

    suite.run("equality and inequality", [](auto& t) {
        t.eq(42, 42);
        t.ne(1, 2);
        t.eq("hello", "hello"); // C string content comparison
    });

    suite.run("ordering comparisons", [](auto& t) {
        t.lt(1, 2);
        t.le(2, 2);
        t.gt(5, 3);
        t.ge(3, 3);
    });

    suite.run("approximate floating-point equality", [](auto& t) {
        t.near(1.0 / 3.0, 0.3333, 0.001);
        t.near(0.1 + 0.2, 0.3, 0.001);
    });

    suite.run("boolean assertions", [](auto& t) {
        t.is_true(true);
        t.is_false(false);
    });

    suite.run("fatal checks — abort test on failure", [](auto& t) {
        int* ptr = nullptr;
        t.require_true(ptr == nullptr);
        t.require_eq(1 + 1, 2);
    });

    suite.run("context notes for debugging", [](auto& t) {
        for (int i = 0; i < 3; ++i) {
            auto guard = t.note("iteration");
            t.lt(i, 10);
        }
    });

    return suite.summary();
}
