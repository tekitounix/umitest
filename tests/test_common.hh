// SPDX-License-Identifier: MIT
// UMI-OS Common Test Utilities
// Minimal test framework for embedded (no exceptions, no RTTI)

#pragma once

#include <cstdio>
#include <cstdlib>
#include <cmath>

namespace umi::test {

// ============================================================================
// Test Statistics
// ============================================================================

struct Stats {
    int total = 0;
    int passed = 0;
    int failed = 0;
    const char* current_section = nullptr;
};

inline Stats& stats() {
    static Stats s;
    return s;
}

// ============================================================================
// ANSI Colors (disabled on embedded if needed)
// ============================================================================

#ifndef UMI_TEST_NO_COLOR
    #define UMI_GREEN  "\033[32m"
    #define UMI_RED    "\033[31m"
    #define UMI_YELLOW "\033[33m"
    #define UMI_CYAN   "\033[36m"
    #define UMI_RESET  "\033[0m"
#else
    #define UMI_GREEN  ""
    #define UMI_RED    ""
    #define UMI_YELLOW ""
    #define UMI_CYAN   ""
    #define UMI_RESET  ""
#endif

// ============================================================================
// Test Macros
// ============================================================================

/// Start a test section
inline void section(const char* name) {
    stats().current_section = name;
    std::printf("\n" UMI_CYAN "[%s]" UMI_RESET "\n", name);
}

/// Check a condition
inline void check(bool cond, const char* msg) {
    stats().total++;
    if (cond) {
        stats().passed++;
        // Verbose mode: std::printf(UMI_GREEN "  ✓ %s" UMI_RESET "\n", msg);
    } else {
        stats().failed++;
        std::printf(UMI_RED "  ✗ FAIL: %s" UMI_RESET "\n", msg);
    }
}

/// Check with custom message format
template<typename... Args>
inline void checkf(bool cond, const char* fmt, Args... args) {
    char buf[256];
    std::snprintf(buf, sizeof(buf), fmt, args...);
    check(cond, buf);
}

/// Check floating point approximately equal
inline bool near(float a, float b, float eps = 0.001f) {
    return std::abs(a - b) < eps;
}

inline void check_near(float actual, float expected, const char* msg, float eps = 0.001f) {
    char buf[256];
    std::snprintf(buf, sizeof(buf), "%s (got %.4f, expected %.4f)", msg, actual, expected);
    check(near(actual, expected, eps), buf);
}

/// Expect a value to be equal
template<typename T>
inline void check_eq(T actual, T expected, const char* msg) {
    char buf[256];
    if constexpr (std::is_integral_v<T>) {
        std::snprintf(buf, sizeof(buf), "%s (got %d, expected %d)", msg,
                      static_cast<int>(actual), static_cast<int>(expected));
    } else {
        std::snprintf(buf, sizeof(buf), "%s", msg);
    }
    check(actual == expected, buf);
}

// ============================================================================
// Test Runner
// ============================================================================

/// Print summary and return exit code
inline int summary() {
    std::printf("\n" UMI_CYAN "=================================" UMI_RESET "\n");
    if (stats().failed == 0) {
        std::printf(UMI_GREEN "Tests: %d/%d passed" UMI_RESET "\n",
                    stats().passed, stats().total);
    } else {
        std::printf(UMI_RED "Tests: %d/%d passed, %d FAILED" UMI_RESET "\n",
                    stats().passed, stats().total, stats().failed);
    }
    std::printf(UMI_CYAN "=================================" UMI_RESET "\n");
    return stats().failed > 0 ? 1 : 0;
}

/// Reset stats (for multiple test files)
inline void reset() {
    stats() = Stats{};
}

} // namespace umi::test

// Convenience macros
#define SECTION(name) umi::test::section(name)
#define CHECK(cond, msg) umi::test::check(cond, msg)
#define CHECK_EQ(a, b, msg) umi::test::check_eq(a, b, msg)
#define CHECK_NEAR(a, b, msg) umi::test::check_near(a, b, msg)
#define TEST_SUMMARY() return umi::test::summary()
