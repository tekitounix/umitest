// SPDX-License-Identifier: MIT
// umidi Test Framework - Minimal standalone test framework
#pragma once

#include <cstdio>
#include <cstdint>
#include <cstring>

namespace umidi::test {

// =============================================================================
// Test Framework
// =============================================================================

struct TestStats {
    int passed = 0;
    int failed = 0;
    const char* current_section = "";
};

inline TestStats& stats() {
    static TestStats s;
    return s;
}

inline void section(const char* name) {
    stats().current_section = name;
    printf("\n%s:\n", name);
}

inline void pass(const char* name) {
    printf("  %s... OK\n", name);
    stats().passed++;
}

inline void fail(const char* name, const char* file, int line, const char* msg) {
    printf("  %s... FAIL\n    %s\n    at %s:%d\n", name, msg, file, line);
    stats().failed++;
}

inline int summary() {
    printf("\n=================================\n");
    printf("Passed: %d\n", stats().passed);
    printf("Failed: %d\n", stats().failed);
    printf("=================================\n");
    return stats().failed > 0 ? 1 : 0;
}

// =============================================================================
// Macros
// =============================================================================

#define SECTION(name) umidi::test::section(name)

#define TEST(name) static bool test_##name()

#define RUN_TEST(name) do { \
    if (test_##name()) { \
        umidi::test::pass(#name); \
    } \
} while(0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        umidi::test::fail(__func__, __FILE__, __LINE__, #cond); \
        return false; \
    } \
} while(0)

#define ASSERT_EQ(a, b) do { \
    if (!((a) == (b))) { \
        umidi::test::fail(__func__, __FILE__, __LINE__, #a " == " #b); \
        return false; \
    } \
} while(0)

#define ASSERT_NE(a, b) do { \
    if ((a) == (b)) { \
        umidi::test::fail(__func__, __FILE__, __LINE__, #a " != " #b); \
        return false; \
    } \
} while(0)

#define ASSERT_LT(a, b) do { \
    if (!((a) < (b))) { \
        umidi::test::fail(__func__, __FILE__, __LINE__, #a " < " #b); \
        return false; \
    } \
} while(0)

#define ASSERT_LE(a, b) do { \
    if (!((a) <= (b))) { \
        umidi::test::fail(__func__, __FILE__, __LINE__, #a " <= " #b); \
        return false; \
    } \
} while(0)

#define ASSERT_GT(a, b) do { \
    if (!((a) > (b))) { \
        umidi::test::fail(__func__, __FILE__, __LINE__, #a " > " #b); \
        return false; \
    } \
} while(0)

#define ASSERT_NEAR(a, b, eps) do { \
    auto _a = (a); auto _b = (b); auto _eps = (eps); \
    if (!((_a >= _b - _eps) && (_a <= _b + _eps))) { \
        umidi::test::fail(__func__, __FILE__, __LINE__, #a " ~= " #b); \
        return false; \
    } \
} while(0)

#define TEST_PASS() return true

} // namespace umidi::test
