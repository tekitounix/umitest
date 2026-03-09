// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief Compile-time checks using constexpr check_* free functions.
/// @author Shota Moriguchi @tekitounix

#include <umitest/check.hh>

// Compile-time verification — no runtime cost
static_assert(umi::test::check_true(1 + 1 == 2));
static_assert(umi::test::check_false(1 > 2));
static_assert(umi::test::check_eq(42, 42));
static_assert(umi::test::check_ne(1, 2));
static_assert(umi::test::check_lt(1, 2));
static_assert(umi::test::check_le(2, 2));
static_assert(umi::test::check_gt(5, 3));
static_assert(umi::test::check_ge(3, 3));
static_assert(umi::test::check_eq("hello", "hello")); // C string content comparison
static_assert(umi::test::check_str_contains("hello world", "world"));
static_assert(umi::test::check_str_starts_with("hello world", "hello"));
static_assert(umi::test::check_str_ends_with("hello world", "world"));

int main() {
    return 0;
}
