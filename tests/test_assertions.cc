// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief Comprehensive assertion tests with boundary values and edge cases.
/// @author Shota Moriguchi @tekitounix

#include <cstdint>
#include <numbers>

#include "test_fixture.hh"

namespace umitest::test {
namespace {

using umi::test::TestContext;

// =============================================================================
// assert_true — edge cases
// =============================================================================

bool test_assert_true_basic(TestContext& t) {
    bool ok = true;
    ok &= t.assert_true(true);
    ok &= t.assert_true(1 == 1U);
    ok &= t.assert_true(42 > 0, "positive number");
    ok &= t.assert_true(0 == 0U, "zero equals zero");
    ok &= t.assert_true(-1 < 0, "negative is less than zero");
    return ok;
}

bool test_assert_true_with_expressions(TestContext& t) {
    int const x = 10;
    bool ok = true;
    ok &= t.assert_true(x > 0);
    ok &= t.assert_true(x * x == 100);
    ok &= t.assert_true(static_cast<unsigned>(x) == 10U);
    return ok;
}

// =============================================================================
// assert_eq / assert_ne — various types and boundary values
// =============================================================================

bool test_assert_eq_integers(TestContext& t) {
    bool ok = true;
    ok &= t.assert_eq(0, 0);
    ok &= t.assert_eq(-1, -1);
    ok &= t.assert_eq(INT32_MAX, INT32_MAX);
    ok &= t.assert_eq(INT32_MIN, INT32_MIN);
    ok &= t.assert_eq(UINT32_MAX, UINT32_MAX);
    ok &= t.assert_eq(static_cast<int64_t>(INT64_MAX), INT64_MAX);
    return ok;
}

bool test_assert_eq_unsigned(TestContext& t) {
    bool ok = true;
    ok &= t.assert_eq(0U, 0U);
    ok &= t.assert_eq(255U, 255U);
    ok &= t.assert_eq(UINT16_MAX, UINT16_MAX);
    ok &= t.assert_eq(UINT64_MAX, UINT64_MAX);
    return ok;
}

bool test_assert_eq_chars(TestContext& t) {
    bool ok = true;
    ok &= t.assert_eq('a', 'a');
    ok &= t.assert_eq('\0', '\0');
    ok &= t.assert_eq('Z', 'Z');
    ok &= t.assert_eq('\n', '\n');
    return ok;
}

bool test_assert_ne_basic(TestContext& t) {
    bool ok = true;
    ok &= t.assert_ne(0, 1);
    ok &= t.assert_ne(-1, 1);
    ok &= t.assert_ne('a', 'b');
    ok &= t.assert_ne(INT32_MIN, INT32_MAX);
    ok &= t.assert_ne(0U, UINT32_MAX);
    return ok;
}

// =============================================================================
// Comparison operators — boundary values
// =============================================================================

bool test_comparisons_integers(TestContext& t) {
    bool ok = true;
    // lt
    ok &= t.assert_lt(INT32_MIN, INT32_MAX);
    ok &= t.assert_lt(-1, 0);
    ok &= t.assert_lt(0, 1);
    // le
    ok &= t.assert_le(0, 0);
    ok &= t.assert_le(-1, 0);
    ok &= t.assert_le(INT32_MIN, INT32_MIN);
    // gt
    ok &= t.assert_gt(INT32_MAX, INT32_MIN);
    ok &= t.assert_gt(1, 0);
    ok &= t.assert_gt(0, -1);
    // ge
    ok &= t.assert_ge(0, 0);
    ok &= t.assert_ge(1, 0);
    ok &= t.assert_ge(INT32_MAX, INT32_MAX);
    return ok;
}

bool test_comparisons_unsigned(TestContext& t) {
    bool ok = true;
    ok &= t.assert_lt(0U, 1U);
    ok &= t.assert_lt(0U, UINT32_MAX);
    ok &= t.assert_le(UINT32_MAX, UINT32_MAX);
    ok &= t.assert_gt(UINT32_MAX, 0U);
    ok &= t.assert_ge(UINT32_MAX, UINT32_MAX);
    return ok;
}

// =============================================================================
// assert_near — floating-point edge cases
// =============================================================================

bool test_assert_near_floats(TestContext& t) {
    bool ok = true;
    ok &= t.assert_near(0.0F, 0.0F);
    ok &= t.assert_near(1.0F, 1.0001F);
    ok &= t.assert_near(-1.0F, -1.0001F);
    ok &= t.assert_near(3.14, std::numbers::pi, 0.01);
    ok &= t.assert_near(1e10, 1e10 + 1.0, 10.0);
    return ok;
}

bool test_assert_near_custom_epsilon(TestContext& t) {
    bool ok = true;
    ok &= t.assert_near(100.0, 100.5, 1.0);
    ok &= t.assert_near(-50.0, -50.05, 0.1);
    ok &= t.assert_near(0.0, 0.0, 0.0001);
    return ok;
}

// =============================================================================
// Enums
// =============================================================================

enum class Priority : std::int8_t { LOW = -1, NORMAL = 0, HIGH = 1, CRITICAL = 100 };

bool test_enum_comparisons(TestContext& t) {
    bool ok = true;
    ok &= t.assert_eq(Priority::LOW, Priority::LOW);
    ok &= t.assert_ne(Priority::LOW, Priority::HIGH);
    ok &= t.assert_lt(Priority::LOW, Priority::NORMAL);
    ok &= t.assert_gt(Priority::CRITICAL, Priority::HIGH);
    ok &= t.assert_le(Priority::NORMAL, Priority::NORMAL);
    ok &= t.assert_ge(Priority::HIGH, Priority::NORMAL);
    return ok;
}

// =============================================================================
// Early return chaining pattern
// =============================================================================

bool test_early_return_chain(TestContext& t) {
    // This is the idiomatic pattern for dependent assertions
    if (!t.assert_true(true, "precondition")) {
        return false;
    }
    if (!t.assert_eq(1, 1)) {
        return false;
    }
    if (!t.assert_lt(0, 1)) {
        return false;
    }

    // Multi-step computation verification
    int const result = (2 * 3) + 4;
    if (!t.assert_eq(result, 10)) {
        return false;
    }
    if (!t.assert_gt(result, 0)) {
        return false;
    }
    return true;
}

bool test_mixed_inline_and_context(TestContext& t) {
    // Real-world pattern: compute and verify in sequence
    constexpr int buffer_size = 1024;
    constexpr int header_size = 16;
    constexpr int payload_size = buffer_size - header_size;

    bool ok = true;
    ok &= t.assert_eq(payload_size, 1008);
    ok &= t.assert_gt(payload_size, 0);
    ok &= t.assert_lt(header_size, buffer_size);
    ok &= t.assert_le(header_size + payload_size, buffer_size);
    return ok;
}

// =============================================================================
// Pointer comparison
// =============================================================================

bool test_pointer_assertions(TestContext& t) {
    int const a = 1;
    int const b = 2;
    int const* pa = &a;
    int const* pb = &b;

    bool ok = true;
    ok &= t.assert_eq(pa, pa);
    ok &= t.assert_ne(pa, pb);
    ok &= t.assert_ne(pa, nullptr);
    ok &= t.assert_eq(static_cast<int*>(nullptr), static_cast<int*>(nullptr));
    return ok;
}

// =============================================================================
// Boolean assertions
// =============================================================================

bool test_boolean_assertions(TestContext& t) {
    bool ok = true;
    ok &= t.assert_eq(true, true);
    ok &= t.assert_eq(false, false);
    ok &= t.assert_ne(true, false);
    ok &= t.assert_ne(false, true);
    return ok;
}

// =============================================================================
// assert_false
// =============================================================================

bool test_assert_false_basic(TestContext& t) {
    bool ok = true;
    ok &= t.assert_false(false);
    ok &= t.assert_false(1 == 2);
    ok &= t.assert_false(0 > 1, "zero not greater than one");
    return ok;
}

// =============================================================================
// Mixed signed/unsigned comparisons (std::cmp_* path)
// =============================================================================

bool test_mixed_sign_comparisons(TestContext& t) {
    bool ok = true;
    ok &= t.assert_eq(0, 0U);
    ok &= t.assert_eq(42, 42U);
    ok &= t.assert_ne(-1, 0U);
    ok &= t.assert_lt(0, 1U);
    ok &= t.assert_le(0, 0U);
    ok &= t.assert_gt(1U, 0);
    ok &= t.assert_ge(0U, 0);
    // Edge case: -1 vs UINT32_MAX — must not compare as equal
    ok &= t.assert_ne(-1, UINT32_MAX);
    return ok;
}

} // namespace

void run_assertion_tests(umi::test::Suite& suite) {
    umi::test::Suite::section("assert_true");
    suite.run("basic conditions", test_assert_true_basic);
    suite.run("complex expressions", test_assert_true_with_expressions);

    umi::test::Suite::section("assert_eq / assert_ne");
    suite.run("integers", test_assert_eq_integers);
    suite.run("unsigned integers", test_assert_eq_unsigned);
    suite.run("chars", test_assert_eq_chars);
    suite.run("inequality", test_assert_ne_basic);
    suite.run("booleans", test_boolean_assertions);
    suite.run("pointers", test_pointer_assertions);

    umi::test::Suite::section("Comparisons (lt/le/gt/ge)");
    suite.run("signed integers", test_comparisons_integers);
    suite.run("unsigned integers", test_comparisons_unsigned);

    umi::test::Suite::section("assert_near");
    suite.run("float basic", test_assert_near_floats);
    suite.run("custom epsilon", test_assert_near_custom_epsilon);

    umi::test::Suite::section("Enum types");
    suite.run("enum comparisons", test_enum_comparisons);

    umi::test::Suite::section("Real-world patterns");
    suite.run("early return chain", test_early_return_chain);
    suite.run("mixed assertions", test_mixed_inline_and_context);

    umi::test::Suite::section("assert_false");
    suite.run("basic false checks", test_assert_false_basic);

    umi::test::Suite::section("Mixed sign comparisons");
    suite.run("signed vs unsigned", test_mixed_sign_comparisons);
}

} // namespace umitest::test
