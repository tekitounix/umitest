// SPDX-License-Identifier: MIT
// umimock unit tests

#include <umimock/mock.hh>

#include "test_common.hh"

using namespace umi::mock;
using umi::test::check;
using umi::test::check_eq;
using umi::test::check_near;

// ============================================================================
// Concept verification
// ============================================================================

static void test_concept() {
    SECTION("Generatable concept");

    // MockSignal satisfies Generatable
    static_assert(Generatable<MockSignal>, "MockSignal must satisfy Generatable");

    // int does not satisfy Generatable
    static_assert(!Generatable<int>, "int must not satisfy Generatable");

    check(true, "static_assert passed");
}

// ============================================================================
// MockSignal basics
// ============================================================================

static void test_constant() {
    SECTION("Constant signal");

    MockSignal sig(Shape::CONSTANT, 0.5f);

    CHECK_EQ(static_cast<int>(sig.get_shape()), static_cast<int>(Shape::CONSTANT), "shape is CONSTANT");
    CHECK_NEAR(sig.get_value(), 0.5f, "value is 0.5");
    CHECK_NEAR(sig.generate(), 0.5f, "generate returns constant");
    CHECK_NEAR(sig.generate(), 0.5f, "generate returns constant again");
}

static void test_ramp() {
    SECTION("Ramp signal");

    MockSignal sig(Shape::RAMP, 1.0f);

    float first = sig.generate();
    float second = sig.generate();
    check(second > first, "ramp increases over time");
    CHECK_NEAR(first, 0.01f, "first ramp sample");
}

// ============================================================================
// set_value with this-> disambiguation
// ============================================================================

static void test_set_value() {
    SECTION("set_value (this-> disambiguation)");

    MockSignal sig;
    CHECK_NEAR(sig.get_value(), default_value, "default value");

    sig.set_value(0.75f);
    CHECK_NEAR(sig.get_value(), 0.75f, "value after set_value");
    CHECK_NEAR(sig.generate(), 0.75f, "generate after set_value");
}

// ============================================================================
// Reset
// ============================================================================

static void test_reset() {
    SECTION("reset");

    MockSignal sig(Shape::RAMP, 1.0f);
    sig.generate();
    sig.generate();
    sig.reset();

    CHECK_NEAR(sig.get_value(), default_value, "value reset to default");
}

// ============================================================================
// fill_buffer (concept-constrained template)
// ============================================================================

static void test_fill_buffer() {
    SECTION("fill_buffer");

    MockSignal sig(Shape::CONSTANT, 0.25f);
    float buf[4] = {};
    fill_buffer(sig, buf, 4);

    for (int i = 0; i < 4; ++i) {
        CHECK_NEAR(buf[i], 0.25f, "buffer sample");
    }
}

// ============================================================================
// constexpr verification
// ============================================================================

static void test_constexpr() {
    SECTION("constexpr");

    constexpr MockSignal sig(Shape::CONSTANT, 1.0f);
    check(sig.get_value() == 1.0f, "constexpr construction");
    check(default_value == 0.0f, "constexpr default_value");
    check(max_ramp_steps == 100, "constexpr max_ramp_steps");
}

// ============================================================================
// detail namespace
// ============================================================================

static void test_detail() {
    SECTION("detail::clamp01");

    CHECK_NEAR(detail::clamp01(-1.0f), 0.0f, "clamp below 0");
    CHECK_NEAR(detail::clamp01(0.5f), 0.5f, "clamp in range");
    CHECK_NEAR(detail::clamp01(2.0f), 1.0f, "clamp above 1");
}

// ============================================================================
// Main
// ============================================================================

int main() {
    test_concept();
    test_constant();
    test_ramp();
    test_set_value();
    test_reset();
    test_fill_buffer();
    test_constexpr();
    test_detail();

    TEST_SUMMARY();
}
