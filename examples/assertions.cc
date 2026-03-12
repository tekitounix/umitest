// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief Demonstration of all assertion methods in umitest.
/// @author Shota Moriguchi @tekitounix

#include <stdexcept>

#include <umitest/test.hh>

int main() {
    umi::test::Suite suite("assertions_demo");

    suite.run("equality and inequality", [](umi::test::TestContext& t) {
        t.eq(42, 42);
        t.ne(1, 2);
        t.eq("hello", "hello"); // C string content comparison
    });

    suite.run("ordering comparisons", [](umi::test::TestContext& t) {
        t.lt(1, 2);
        t.le(2, 2);
        t.gt(5, 3);
        t.ge(3, 3);
    });

    suite.run("approximate floating-point equality", [](umi::test::TestContext& t) {
        t.near(1.0 / 3.0, 0.3333, 0.001);
        t.near(0.1 + 0.2, 0.3, 0.001);
    });

    suite.run("boolean assertions", [](umi::test::TestContext& t) {
        t.is_true(true);
        t.is_false(false);
    });

    suite.run("fatal checks — abort test on failure", [](umi::test::TestContext& t) {
        const int* ptr = nullptr;
        if (!t.require_true(ptr == nullptr)) {
            return;
        }
        if (!t.require_eq(1 + 1, 2)) {
            return;
        }
    });

    suite.run("exception assertions", [](umi::test::TestContext& t) {
        t.template throws<std::runtime_error>([] { throw std::runtime_error("oops"); });
        t.throws([] { throw 42; });
        t.nothrow([] {});
    });

    suite.run("string assertions", [](umi::test::TestContext& t) {
        t.str_contains("hello world", "world");
        t.str_starts_with("hello world", "hello");
        t.str_ends_with("hello world", "world");
    });

    suite.run("context notes for debugging", [](umi::test::TestContext& t) {
        for (int i = 0; i < 3; ++i) {
            auto guard = t.note("iteration");
            t.lt(i, 10);
        }
    });

    return suite.summary();
}
