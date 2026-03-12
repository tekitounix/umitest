#pragma once

// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief Unit tests for check.hh — constexpr bool pure functions.
/// @author Shota Moriguchi @tekitounix

#include <limits>
#include <string>

#include <umitest/check.hh>
#include <umitest/test.hh>

// Compile-time verification
static_assert(umi::test::check_true(true));
static_assert(!umi::test::check_true(false));
static_assert(umi::test::check_false(false));
static_assert(!umi::test::check_false(true));

static_assert(umi::test::check_eq(1, 1));
static_assert(!umi::test::check_eq(1, 2));
static_assert(umi::test::check_eq("hello", "hello"));
static_assert(!umi::test::check_eq("hello", "world"));
static_assert(umi::test::check_ne(1, 2));
static_assert(!umi::test::check_ne(1, 1));

static_assert(umi::test::check_lt(1, 2));
static_assert(!umi::test::check_lt(2, 1));
static_assert(!umi::test::check_lt(1, 1));
static_assert(umi::test::check_le(1, 2));
static_assert(umi::test::check_le(1, 1));
static_assert(!umi::test::check_le(2, 1));
static_assert(umi::test::check_gt(2, 1));
static_assert(!umi::test::check_gt(1, 2));
static_assert(umi::test::check_ge(2, 1));
static_assert(umi::test::check_ge(1, 1));
static_assert(!umi::test::check_ge(0, 1));

// Mixed-sign integer safety
static_assert(!umi::test::check_eq(-1, 4294967295U));
static_assert(umi::test::check_ne(-1, 4294967295U));
static_assert(umi::test::check_lt(-1, 1U));
static_assert(!umi::test::check_gt(-1, 1U));

// Bool comparison (excluded from std::cmp_*)
static_assert(umi::test::check_eq(true, true));
static_assert(!umi::test::check_eq(true, false));

// C string content comparison
static_assert(umi::test::check_eq(static_cast<const char*>(nullptr), static_cast<const char*>(nullptr)));
static_assert(!umi::test::check_eq(static_cast<const char*>(nullptr), "hello"));
static_assert(!umi::test::check_eq("hello", static_cast<const char*>(nullptr)));
static_assert(umi::test::check_ne(static_cast<const char*>(nullptr), "hello"));

namespace umitest::test {
inline void run_check_tests(umi::test::Suite& suite) {
    suite.section("check functions");

    suite.run("check_eq runtime", [](auto& t) {
        t.is_true(umi::test::check_eq(1, 1));
        t.is_true(!umi::test::check_eq(1, 2));

        // C string content comparison at runtime (distinct addresses, same content)
        const char* a = "hello";
        const char* b = "hello";
        // Force distinct addresses by copying to mutable storage
        const std::string sa = "hello";
        const std::string sb = "hello";
        t.is_true(umi::test::check_eq(sa.c_str(), sb.c_str()));
        t.is_true(umi::test::check_eq(a, "hello"));
        t.is_true(umi::test::check_eq("hello", b));
    });

    suite.run("check_eq string vs const char*", [](auto& t) {
        const std::string str = "hello";
        const char* ptr = "hello";
        t.is_true(umi::test::check_eq(str, ptr));
        t.is_true(umi::test::check_eq(ptr, str));

        const char* null = nullptr;
        t.is_false(umi::test::check_eq(str, null));
        t.is_false(umi::test::check_eq(null, str));
    });

    suite.run("check_eq nullptr_t vs const char*", [](auto& t) {
        const char* null_ptr = nullptr;
        t.is_true(umi::test::check_eq(nullptr, null_ptr));
        t.is_false(umi::test::check_eq(nullptr, "hello"));
    });

    suite.run("check_ne C strings", [](auto& t) {
        t.is_true(umi::test::check_ne("hello", "world"));
        t.is_false(umi::test::check_ne("hello", "hello"));
    });

    suite.run("check_lt/le/gt/ge integers", [](auto& t) {
        t.is_true(umi::test::check_lt(1, 2));
        t.is_false(umi::test::check_lt(2, 1));
        t.is_true(umi::test::check_le(1, 1));
        t.is_true(umi::test::check_gt(2, 1));
        t.is_true(umi::test::check_ge(1, 1));
    });

    suite.run("check_near basic", [](auto& t) {
        t.is_true(umi::test::check_near(1.0, 1.0005));
        t.is_false(umi::test::check_near(1.0, 2.0));
    });

    suite.run("check_near inf", [](auto& t) {
        auto inf = std::numeric_limits<double>::infinity();
        t.is_true(umi::test::check_near(inf, inf));
        t.is_false(umi::test::check_near(inf, -inf));
    });

    suite.run("check_near nan", [](auto& t) {
        auto nan = std::numeric_limits<double>::quiet_NaN();
        t.is_false(umi::test::check_near(nan, nan));
        t.is_false(umi::test::check_near(nan, 1.0));
    });

    suite.run("check_near negative eps", [](auto& t) { t.is_false(umi::test::check_near(1.0, 1.0, -0.1)); });

    suite.run("check_near exact equality independent of eps",
              [](auto& t) { t.is_true(umi::test::check_near(1.0, 1.0, 0.0)); });
}

} // namespace umitest::test
