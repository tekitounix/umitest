#pragma once

// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief Tests for string check functions and TestContext string methods.
/// @author Shota Moriguchi @tekitounix

#include <umitest/reporters/null.hh>
#include <umitest/test.hh>

namespace umitest::test {

inline void run_string_tests(umi::test::Suite& suite) {
    using umi::test::BasicSuite;
    using umi::test::check_str_contains;
    using umi::test::check_str_ends_with;
    using umi::test::check_str_starts_with;
    using umi::test::NullReporter;

    suite.section("string checks (free functions)");

    // -- check_str_contains --

    suite.run("check_str_contains_found", [](auto& t) {
        t.is_true(check_str_contains("hello world", "world"));
        t.is_true(check_str_contains("hello world", "hello"));
        t.is_true(check_str_contains("hello world", "lo wo"));
    });

    suite.run("check_str_contains_not_found", [](auto& t) {
        t.is_false(check_str_contains("hello world", "xyz"));
        t.is_false(check_str_contains("", "x"));
    });

    suite.run("check_str_contains_empty_needle", [](auto& t) {
        t.is_true(check_str_contains("hello", ""));
        t.is_true(check_str_contains("", ""));
    });

    // -- check_str_starts_with --

    suite.run("check_str_starts_with_true", [](auto& t) {
        t.is_true(check_str_starts_with("hello world", "hello"));
        t.is_true(check_str_starts_with("hello", ""));
    });

    suite.run("check_str_starts_with_false", [](auto& t) {
        t.is_false(check_str_starts_with("hello world", "world"));
        t.is_false(check_str_starts_with("", "x"));
    });

    // -- check_str_ends_with --

    suite.run("check_str_ends_with_true", [](auto& t) {
        t.is_true(check_str_ends_with("hello world", "world"));
        t.is_true(check_str_ends_with("hello", ""));
    });

    suite.run("check_str_ends_with_false", [](auto& t) {
        t.is_false(check_str_ends_with("hello world", "hello"));
        t.is_false(check_str_ends_with("", "x"));
    });

    // -- constexpr verification --

    suite.run("string_checks_constexpr", [](auto& t) {
        static_assert(check_str_contains("abcdef", "cde"));
        static_assert(!check_str_contains("abcdef", "xyz"));
        static_assert(check_str_starts_with("abcdef", "abc"));
        static_assert(!check_str_starts_with("abcdef", "bcd"));
        static_assert(check_str_ends_with("abcdef", "def"));
        static_assert(!check_str_ends_with("abcdef", "cde"));
        t.is_true(true); // reached without compile error
    });

    suite.section("string checks (context methods)");

    // -- str_contains --

    suite.run("str_contains_pass", [](auto& t) {
        t.str_contains("hello world", "world");
        t.is_true(t.ok());
    });

    suite.run("str_contains_fail", [](auto& t) {
        BasicSuite<NullReporter> inner("inner");
        inner.run("x", [](auto& ctx) { ctx.str_contains("hello", "xyz"); });
        t.eq(inner.summary(), 1);
    });

    // -- str_starts_with --

    suite.run("str_starts_with_pass", [](auto& t) {
        t.str_starts_with("hello world", "hello");
        t.is_true(t.ok());
    });

    suite.run("str_starts_with_fail", [](auto& t) {
        BasicSuite<NullReporter> inner("inner");
        inner.run("x", [](auto& ctx) { ctx.str_starts_with("hello", "world"); });
        t.eq(inner.summary(), 1);
    });

    // -- str_ends_with --

    suite.run("str_ends_with_pass", [](auto& t) {
        t.str_ends_with("hello world", "world");
        t.is_true(t.ok());
    });

    suite.run("str_ends_with_fail", [](auto& t) {
        BasicSuite<NullReporter> inner("inner");
        inner.run("x", [](auto& ctx) { ctx.str_ends_with("hello", "world"); });
        t.eq(inner.summary(), 1);
    });

    // -- require_str_* --

    suite.run("require_str_contains_pass", [](auto& t) {
        const bool result = t.require_str_contains("hello world", "world");
        t.is_true(result);
    });

    suite.run("require_str_contains_fail", [](auto& t) {
        BasicSuite<NullReporter> inner("inner");
        inner.run("x", [](auto& ctx) { ctx.require_str_contains("hello", "xyz"); });
        t.eq(inner.summary(), 1);
    });

    suite.run("require_str_starts_with_pass", [](auto& t) {
        const bool result = t.require_str_starts_with("hello world", "hello");
        t.is_true(result);
    });

    suite.run("require_str_starts_with_fail", [](auto& t) {
        BasicSuite<NullReporter> inner("inner");
        inner.run("x", [](auto& ctx) { ctx.require_str_starts_with("hello", "world"); });
        t.eq(inner.summary(), 1);
    });

    suite.run("require_str_ends_with_pass", [](auto& t) {
        const bool result = t.require_str_ends_with("hello world", "world");
        t.is_true(result);
    });

    suite.run("require_str_ends_with_fail", [](auto& t) {
        BasicSuite<NullReporter> inner("inner");
        inner.run("x", [](auto& ctx) { ctx.require_str_ends_with("hello", "world"); });
        t.eq(inner.summary(), 1);
    });
}

} // namespace umitest::test
