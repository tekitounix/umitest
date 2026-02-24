// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief Tests for format_value output across all supported types.

#include <array>
#include <cstdint>
#include <cstring>
#include <string_view>

#include "test_fixture.hh"

namespace umitest::test {
namespace {

using umi::test::format_value;
using umi::test::TestContext;

/// Helper: format a value and return as string_view (backed by static buffer per call site).
template <typename T>
std::string_view fmt(const T& v) {
    static std::array<char, 128> buf{};
    std::memset(buf.data(), 0, buf.size());
    format_value(buf.data(), buf.size(), v);
    return {buf.data()};
}

// =============================================================================
// Boolean formatting
// =============================================================================

bool test_format_bool(TestContext& t) {
    bool ok = true;
    ok &= t.assert_eq(fmt(true), std::string_view{"true"});
    ok &= t.assert_eq(fmt(false), std::string_view{"false"});
    return ok;
}

// =============================================================================
// Character formatting
// =============================================================================

bool test_format_char(TestContext& t) {
    std::array<char, 128> buf{};
    format_value(buf.data(), buf.size(), 'A');
    // Should contain 'A' and (65)
    auto sv = std::string_view{buf.data()};
    bool ok = true;
    ok &= t.assert_true(sv.find('A') != std::string_view::npos, "contains char A");
    ok &= t.assert_true(sv.find("65") != std::string_view::npos, "contains decimal 65");
    return ok;
}

bool test_format_char_special(TestContext& t) {
    std::array<char, 128> buf{};
    format_value(buf.data(), buf.size(), '\0');
    // NUL char: snprintf "'%c' (%d)" produces "'\0' (0)" — embedded NUL truncates string_view.
    // Verify the buffer has '(0)' after the embedded NUL by scanning the raw buffer.
    bool found_zero = false;
    for (std::size_t i = 0; i < buf.size() - 1; ++i) {
        if (buf[i] == '(' && buf[i + 1] == '0') {
            found_zero = true;
            break;
        }
    }
    return t.assert_true(found_zero, "contains (0) for NUL char in raw buffer");
}

// =============================================================================
// Unsigned integer formatting
// =============================================================================

bool test_format_unsigned(TestContext& t) {
    bool ok = true;
    ok &= t.assert_eq(fmt(0U), std::string_view{"0"});
    ok &= t.assert_eq(fmt(42U), std::string_view{"42"});
    ok &= t.assert_eq(fmt(255U), std::string_view{"255"});
    ok &= t.assert_eq(fmt(static_cast<uint16_t>(65535)), std::string_view{"65535"});
    return ok;
}

// =============================================================================
// Signed integer formatting
// =============================================================================

bool test_format_signed(TestContext& t) {
    bool ok = true;
    ok &= t.assert_eq(fmt(0), std::string_view{"0"});
    ok &= t.assert_eq(fmt(-1), std::string_view{"-1"});
    ok &= t.assert_eq(fmt(42), std::string_view{"42"});
    ok &= t.assert_eq(fmt(-100), std::string_view{"-100"});
    return ok;
}

// =============================================================================
// Floating-point formatting
// =============================================================================

bool test_format_float(TestContext& t) {
    bool ok = true;
    // format_value uses "%.6g" — check a few known values
    ok &= t.assert_eq(fmt(0.0), std::string_view{"0"});
    ok &= t.assert_eq(fmt(1.0), std::string_view{"1"});
    ok &= t.assert_eq(fmt(3.14), std::string_view{"3.14"});
    ok &= t.assert_eq(fmt(-2.5), std::string_view{"-2.5"});
    return ok;
}

bool test_format_float_precision(TestContext& t) {
    // %.6g shows up to 6 significant digits
    auto sv = fmt(1.23456789);
    bool ok = true;
    ok &= t.assert_true(sv.find("1.23457") != std::string_view::npos, "rounded to 6 sig figs");
    return ok;
}

// =============================================================================
// Enum formatting
// =============================================================================

enum class Color : std::uint8_t { RED = 0, GREEN = 42, BLUE = 255 };

bool test_format_enum(TestContext& t) {
    bool ok = true;
    ok &= t.assert_eq(fmt(Color::RED), std::string_view{"0"});
    ok &= t.assert_eq(fmt(Color::GREEN), std::string_view{"42"});
    ok &= t.assert_eq(fmt(Color::BLUE), std::string_view{"255"});
    return ok;
}

enum class BigEnum : std::int32_t { NEG = -1000, ZERO = 0, BIG = 100000 };

bool test_format_enum_wide(TestContext& t) {
    bool ok = true;
    ok &= t.assert_eq(fmt(BigEnum::NEG), std::string_view{"-1000"});
    ok &= t.assert_eq(fmt(BigEnum::ZERO), std::string_view{"0"});
    ok &= t.assert_eq(fmt(BigEnum::BIG), std::string_view{"100000"});
    return ok;
}

// =============================================================================
// Pointer formatting
// =============================================================================

bool test_format_pointer(TestContext& t) {
    int const x = 0;
    std::array<char, 128> buf{};
    format_value(buf.data(), buf.size(), &x);
    auto sv = std::string_view{buf.data()};

    bool ok = true;
    // Should produce a hex address like "0x..." or similar
    ok &= t.assert_true(!sv.empty(), "non-empty pointer format");

    // Null pointer
    format_value(buf.data(), buf.size(), static_cast<int*>(nullptr));
    sv = std::string_view{buf.data()};
    ok &= t.assert_true(!sv.empty(), "null pointer formatted");
    return ok;
}

// =============================================================================
// Unknown type formatting
// =============================================================================

struct Opaque {
    int x;
};

bool test_format_unknown_type(TestContext& t) {
    Opaque const obj{42};
    std::array<char, 128> buf{};
    format_value(buf.data(), buf.size(), obj);
    auto sv = std::string_view{buf.data()};

    // Unknown types should format as "(?)"
    return t.assert_eq(sv, std::string_view{"(?)"});
}

} // namespace

void run_format_tests(umi::test::Suite& suite) {
    umi::test::Suite::section("format_value: bool");
    suite.run("true/false", test_format_bool);

    umi::test::Suite::section("format_value: char");
    suite.run("printable char", test_format_char);
    suite.run("NUL char", test_format_char_special);

    umi::test::Suite::section("format_value: unsigned");
    suite.run("basic values", test_format_unsigned);

    umi::test::Suite::section("format_value: signed");
    suite.run("basic values", test_format_signed);

    umi::test::Suite::section("format_value: float");
    suite.run("basic values", test_format_float);
    suite.run("precision", test_format_float_precision);

    umi::test::Suite::section("format_value: enum");
    suite.run("basic enum", test_format_enum);
    suite.run("wide enum", test_format_enum_wide);

    umi::test::Suite::section("format_value: pointer");
    suite.run("non-null / null", test_format_pointer);

    umi::test::Suite::section("format_value: unknown");
    suite.run("opaque type", test_format_unknown_type);
}

} // namespace umitest::test
