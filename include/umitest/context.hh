// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief Assertion context passed to run() test functions.
/// @author Shota Moriguchi @tekitounix
#pragma once

#include <source_location>

namespace umi::test {

// =============================================================================
// TestContext — used inside run() test functions
// =============================================================================

/// @brief Assertion context passed to run() test functions.
///
/// Tracks per-test failure state. Each assert method returns false on failure
/// and marks the context as failed. Suite aggregates pass/fail counts.
class TestContext {
  public:
    TestContext() = default;

    /// @brief Check whether any assertion has failed in this context.
    [[nodiscard]] bool has_failed() const { return failed; }
    /// @brief Reset failure state (called before each test).
    void clear_failed() { failed = false; }

    /// @brief Assert that a condition is true.
    /// @param cond Condition to check.
    /// @param msg Optional failure message.
    /// @return true if the condition held.
    bool assert_true(bool cond, const char* msg = nullptr, std::source_location loc = std::source_location::current());

    /// @brief Assert that a condition is false.
    /// @param cond Condition to check.
    /// @param msg Optional failure message.
    /// @return true if the condition was false.
    bool assert_false(bool cond, const char* msg = nullptr, std::source_location loc = std::source_location::current());

    /// @brief Assert equality (a == b).
    template <typename A, typename B>
    bool assert_eq(const A& a, const B& b, std::source_location loc = std::source_location::current());

    /// @brief Assert inequality (a != b).
    template <typename A, typename B>
    bool assert_ne(const A& a, const B& b, std::source_location loc = std::source_location::current());

    /// @brief Assert less-than (a < b).
    template <typename A, typename B>
    bool assert_lt(const A& a, const B& b, std::source_location loc = std::source_location::current());

    /// @brief Assert less-or-equal (a <= b).
    template <typename A, typename B>
    bool assert_le(const A& a, const B& b, std::source_location loc = std::source_location::current());

    /// @brief Assert greater-than (a > b).
    template <typename A, typename B>
    bool assert_gt(const A& a, const B& b, std::source_location loc = std::source_location::current());

    /// @brief Assert greater-or-equal (a >= b).
    template <typename A, typename B>
    bool assert_ge(const A& a, const B& b, std::source_location loc = std::source_location::current());

    /// @brief Assert approximate equality within epsilon.
    /// @param eps Maximum allowed absolute difference.
    template <typename A, typename B>
    bool
    assert_near(const A& a, const B& b, double eps = 0.001, std::source_location loc = std::source_location::current());

  private:
    void mark_failed() { failed = true; }

    template <typename A, typename B, typename Cmp>
    bool assert_cmp(const A& a, const B& b, const char* op, Cmp cmp, std::source_location loc);

    bool failed = false;
};

} // namespace umi::test
