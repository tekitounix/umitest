// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief Test runner that collects pass/fail statistics and prints results.
/// @author Shota Moriguchi @tekitounix
#pragma once

#include <array>
#include <cmath>
#include <cstdio>
#include <source_location>
#include <umitest/context.hh>
#include <umitest/format.hh>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"

namespace umi::test {

// =============================================================================
// Suite — test runner and statistics
// =============================================================================

/// @brief Test runner that collects pass/fail statistics and prints results.
///
/// Supports two styles: structured tests via run() with TestContext,
/// and inline checks via check_*() methods.
class Suite {
  public:
    explicit Suite(const char* name) : name(name) {}

    // -- Section --

    /// @brief Print a visual section header to group related tests.
    static void section(const char* title) { std::printf("\n%s[%s]%s\n", cyan, title, reset); }

    // -- run() : structured test with TestContext --

    /// @brief Run a structured test with a TestContext.
    /// @tparam F Callable taking TestContext& and returning bool.
    /// @param test_name Name displayed in output.
    /// @param fn Test body; return false to indicate failure.
    template <typename F>
    void run(const char* test_name, F&& fn) {
        TestContext ctx;
        ctx.clear_failed();
        const bool returned_ok = fn(ctx);
        if (!returned_ok || ctx.has_failed()) {
            std::printf("  %s... %sFAIL%s\n", test_name, red, reset);
            failed++;
        } else {
            std::printf("  %s... %sOK%s\n", test_name, green, reset);
            passed++;
        }
    }

    // -- check() : inline checks (no TestContext needed) --

    /// @brief Inline boolean check (true expected). Increments pass or fail count.
    bool check(bool cond, const char* msg = nullptr, std::source_location loc = std::source_location::current()) {
        if (cond) {
            passed++;
            return true;
        }
        record_fail(loc, msg);
        failed++;
        return false;
    }

    /// @brief Inline boolean check (false expected). Increments pass or fail count.
    bool check_false(bool cond, const char* msg = nullptr, std::source_location loc = std::source_location::current()) {
        return check(!cond, msg, loc);
    }

    /// @brief Inline equality check.
    template <typename A, typename B>
    bool check_eq(const A& a, const B& b, std::source_location loc = std::source_location::current()) {
        return check_cmp(a, b, "==", [](auto& x, auto& y) { return x == y; }, loc);
    }

    /// @brief Inline inequality check.
    template <typename A, typename B>
    bool check_ne(const A& a, const B& b, std::source_location loc = std::source_location::current()) {
        return check_cmp(a, b, "!=", [](auto& x, auto& y) { return x != y; }, loc);
    }

    /// @brief Inline less-than check.
    template <typename A, typename B>
    bool check_lt(const A& a, const B& b, std::source_location loc = std::source_location::current()) {
        return check_cmp(a, b, "<", [](auto& x, auto& y) { return x < y; }, loc);
    }

    /// @brief Inline less-or-equal check.
    template <typename A, typename B>
    bool check_le(const A& a, const B& b, std::source_location loc = std::source_location::current()) {
        return check_cmp(a, b, "<=", [](auto& x, auto& y) { return x <= y; }, loc);
    }

    /// @brief Inline greater-than check.
    template <typename A, typename B>
    bool check_gt(const A& a, const B& b, std::source_location loc = std::source_location::current()) {
        return check_cmp(a, b, ">", [](auto& x, auto& y) { return x > y; }, loc);
    }

    /// @brief Inline greater-or-equal check.
    template <typename A, typename B>
    bool check_ge(const A& a, const B& b, std::source_location loc = std::source_location::current()) {
        return check_cmp(a, b, ">=", [](auto& x, auto& y) { return x >= y; }, loc);
    }

    /// @brief Inline approximate-equality check.
    template <typename A, typename B>
    bool
    check_near(const A& a, const B& b, double eps = 0.001, std::source_location loc = std::source_location::current()) {
        if (std::abs(static_cast<double>(a) - static_cast<double>(b)) < eps) {
            passed++;
            return true;
        }
        record_fail_near(a, b, eps, loc);
        failed++;
        return false;
    }

    // -- Summary --

    /// @brief Print final pass/fail summary and return exit code.
    /// @return 0 if all passed, 1 if any failed.
    int summary() {
        const int total = passed + failed;
        std::printf("\n%s=================================%s\n", cyan, reset);
        if (failed == 0) {
            std::printf("%s%s: %d/%d passed%s\n", green, name, passed, total, reset);
        } else {
            std::printf("%s%s: %d/%d passed, %d FAILED%s\n", red, name, passed, total, failed, reset);
        }
        std::printf("%s=================================%s\n", cyan, reset);
        return failed > 0 ? 1 : 0;
    }

    // -- Recording (used by TestContext) --

    /// @brief Record a simple failure with optional message.
    static void record_fail(std::source_location loc, const char* msg = nullptr) {
        if (msg != nullptr) {
            std::printf(
                "  %sFAIL: %s%s\n    at %s:%u\n", red, msg, reset, loc.file_name(), static_cast<unsigned>(loc.line()));
        } else {
            std::printf("  %sFAIL%s at %s:%u\n", red, reset, loc.file_name(), static_cast<unsigned>(loc.line()));
        }
    }

    /// @brief Record a comparison failure with formatted values.
    template <typename A, typename B>
    static void record_fail_cmp(const A& a, const char* op, const B& b, std::source_location loc) {
        std::array<char, 64> va{};
        std::array<char, 64> vb{};
        format_value(va.data(), va.size(), a);
        format_value(vb.data(), vb.size(), b);
        std::printf("  %sFAIL: %s %s %s (got %s, expected %s)%s\n    at %s:%u\n",
                    red, va.data(), op, vb.data(), va.data(), vb.data(), reset,
                    loc.file_name(), static_cast<unsigned>(loc.line()));
    }

    /// @brief Record an approximate-equality failure with formatted values.
    template <typename A, typename B>
    static void record_fail_near(const A& a, const B& b, double eps, std::source_location loc) {
        std::array<char, 64> va{};
        std::array<char, 64> vb{};
        format_value(va.data(), va.size(), a);
        format_value(vb.data(), vb.size(), b);
        std::printf("  %sFAIL: got %s, expected %s (eps=%.6g)%s\n    at %s:%u\n",
                    red, va.data(), vb.data(), eps, reset,
                    loc.file_name(), static_cast<unsigned>(loc.line()));
    }

  private:
    /// @brief Comparison helper for check_* methods.
    template <typename A, typename B, typename Cmp>
    bool check_cmp(const A& a, const B& b, const char* op, Cmp cmp, std::source_location loc) {
        if (cmp(a, b)) {
            passed++;
            return true;
        }
        record_fail_cmp(a, op, b, loc);
        failed++;
        return false;
    }

    const char* name;
    int passed = 0;
    int failed = 0;
};

// =============================================================================
// TestContext implementation (needs Suite to be complete)
// =============================================================================

inline bool TestContext::assert_true(bool cond, const char* msg, std::source_location loc) {
    if (!cond) {
        Suite::record_fail(loc, msg);
        mark_failed();
    }
    return cond;
}

inline bool TestContext::assert_false(bool cond, const char* msg, std::source_location loc) {
    if (cond) {
        Suite::record_fail(loc, (msg != nullptr) ? msg : "expected false");
        mark_failed();
    }
    return !cond;
}

template <typename A, typename B, typename Cmp>
bool TestContext::assert_cmp(const A& a, const B& b, const char* op, Cmp cmp, std::source_location loc) {
    if (cmp(a, b)) {
        return true;
    }
    Suite::record_fail_cmp(a, op, b, loc);
    mark_failed();
    return false;
}

template <typename A, typename B>
bool TestContext::assert_eq(const A& a, const B& b, std::source_location loc) {
    return assert_cmp(a, b, "==", [](auto& x, auto& y) { return x == y; }, loc);
}

template <typename A, typename B>
bool TestContext::assert_ne(const A& a, const B& b, std::source_location loc) {
    return assert_cmp(a, b, "!=", [](auto& x, auto& y) { return x != y; }, loc);
}

template <typename A, typename B>
bool TestContext::assert_lt(const A& a, const B& b, std::source_location loc) {
    return assert_cmp(a, b, "<", [](auto& x, auto& y) { return x < y; }, loc);
}

template <typename A, typename B>
bool TestContext::assert_le(const A& a, const B& b, std::source_location loc) {
    return assert_cmp(a, b, "<=", [](auto& x, auto& y) { return x <= y; }, loc);
}

template <typename A, typename B>
bool TestContext::assert_gt(const A& a, const B& b, std::source_location loc) {
    return assert_cmp(a, b, ">", [](auto& x, auto& y) { return x > y; }, loc);
}

template <typename A, typename B>
bool TestContext::assert_ge(const A& a, const B& b, std::source_location loc) {
    return assert_cmp(a, b, ">=", [](auto& x, auto& y) { return x >= y; }, loc);
}

template <typename A, typename B>
bool TestContext::assert_near(const A& a, const B& b, double eps, std::source_location loc) {
    if (std::abs(static_cast<double>(a) - static_cast<double>(b)) < eps) {
        return true;
    }
    Suite::record_fail_near(a, b, eps, loc);
    mark_failed();
    return false;
}

} // namespace umi::test

#pragma GCC diagnostic pop
