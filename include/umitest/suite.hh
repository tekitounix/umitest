#pragma once

// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief Stdio-free test runner using platform-resolved OutputLike backend.
/// @author Shota Moriguchi @tekitounix

#include <array>
#include <cmath>
#include <cstring>
#include <source_location>

#include <umiport/platform.hh> // IWYU pragma: keep (build-resolved Output)
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
/// Output is emitted via the build-resolved platform's Output backend (stdio-free).
class Suite {
    using Output = umi::port::Platform::Output;

  public:
    explicit Suite(const char* name) : name(name) {}

    // -- Section --

    /// @brief Print a visual section header to group related tests.
    static void section(const char* title) {
        Output::putc('\n');
        Output::puts(cyan);
        Output::putc('[');
        Output::puts(title);
        Output::putc(']');
        Output::puts(reset);
        Output::putc('\n');
    }

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
        Output::puts("  ");
        Output::puts(test_name);
        Output::puts("... ");
        if (!returned_ok || ctx.has_failed()) {
            Output::puts(red);
            Output::puts("FAIL");
            Output::puts(reset);
            Output::putc('\n');
            failed++;
        } else {
            Output::puts(green);
            Output::puts("OK");
            Output::puts(reset);
            Output::putc('\n');
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
        Output::putc('\n');
        Output::puts(cyan);
        Output::puts("=================================");
        Output::puts(reset);
        Output::putc('\n');
        if (failed == 0) {
            Output::puts(green);
            Output::puts(name);
            Output::puts(": ");
            Output::print_uint(static_cast<std::uint64_t>(passed));
            Output::putc('/');
            Output::print_uint(static_cast<std::uint64_t>(total));
            Output::puts(" passed");
            Output::puts(reset);
            Output::putc('\n');
        } else {
            Output::puts(red);
            Output::puts(name);
            Output::puts(": ");
            Output::print_uint(static_cast<std::uint64_t>(passed));
            Output::putc('/');
            Output::print_uint(static_cast<std::uint64_t>(total));
            Output::puts(" passed, ");
            Output::print_uint(static_cast<std::uint64_t>(failed));
            Output::puts(" FAILED");
            Output::puts(reset);
            Output::putc('\n');
        }
        Output::puts(cyan);
        Output::puts("=================================");
        Output::puts(reset);
        Output::putc('\n');
        return failed > 0 ? 1 : 0;
    }

    // -- Recording (used by TestContext) --

    /// @brief Record a simple failure with optional message.
    static void record_fail(std::source_location loc, const char* msg = nullptr) {
        Output::puts("  ");
        Output::puts(red);
        Output::puts("FAIL");
        if (msg != nullptr) {
            Output::puts(": ");
            Output::puts(msg);
        }
        Output::puts(reset);
        Output::puts("\n    at ");
        Output::puts(loc.file_name());
        Output::putc(':');
        Output::print_uint(static_cast<std::uint64_t>(loc.line()));
        Output::putc('\n');
    }

    /// @brief Record a comparison failure with formatted values.
    template <typename A, typename B>
    static void record_fail_cmp(const A& a, const char* op, const B& b, std::source_location loc) {
        std::array<char, 64> va{};
        std::array<char, 64> vb{};
        format_value(va.data(), va.size(), a);
        format_value(vb.data(), vb.size(), b);
        Output::puts("  ");
        Output::puts(red);
        Output::puts("FAIL: ");
        Output::puts(va.data());
        Output::putc(' ');
        Output::puts(op);
        Output::putc(' ');
        Output::puts(vb.data());
        Output::puts(" (got ");
        Output::puts(va.data());
        Output::puts(", expected ");
        Output::puts(vb.data());
        Output::putc(')');
        Output::puts(reset);
        Output::puts("\n    at ");
        Output::puts(loc.file_name());
        Output::putc(':');
        Output::print_uint(static_cast<std::uint64_t>(loc.line()));
        Output::putc('\n');
    }

    /// @brief Record an approximate-equality failure with formatted values.
    template <typename A, typename B>
    static void record_fail_near(const A& a, const B& b, double eps, std::source_location loc) {
        std::array<char, 64> va{};
        std::array<char, 64> vb{};
        std::array<char, 32> ve{};
        format_value(va.data(), va.size(), a);
        format_value(vb.data(), vb.size(), b);
        detail::format_double(ve.data(), ve.size(), eps);
        Output::puts("  ");
        Output::puts(red);
        Output::puts("FAIL: got ");
        Output::puts(va.data());
        Output::puts(", expected ");
        Output::puts(vb.data());
        Output::puts(" (eps=");
        Output::puts(ve.data());
        Output::putc(')');
        Output::puts(reset);
        Output::puts("\n    at ");
        Output::puts(loc.file_name());
        Output::putc(':');
        Output::print_uint(static_cast<std::uint64_t>(loc.line()));
        Output::putc('\n');
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
