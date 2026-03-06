// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief Tests for format_value output across all supported types.
/// @author Shota Moriguchi @tekitounix

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
    bool ok = true;
    ok &= t.assert_eq(fmt('A'), std::string_view{"'A' (65)"});
    ok &= t.assert_eq(fmt('Z'), std::string_view{"'Z' (90)"});
    ok &= t.assert_eq(fmt(' '), std::string_view{"' ' (32)"});
    return ok;
}

bool test_format_char_special(TestContext& t) {
    bool ok = true;
    // NUL char: now escaped as '\0' so string_view works correctly
    ok &= t.assert_eq(fmt('\0'), std::string_view{"'\\0' (0)"});
    // Newline
    ok &= t.assert_eq(fmt('\n'), std::string_view{"'\\n' (10)"});
    // Tab
    ok &= t.assert_eq(fmt('\t'), std::string_view{"'\\t' (9)"});
    // Carriage return
    ok &= t.assert_eq(fmt('\r'), std::string_view{"'\\r' (13)"});
    // Backslash
    ok &= t.assert_eq(fmt('\\'), std::string_view{"'\\\\' (92)"});
    // Single-quote escape
    ok &= t.assert_eq(fmt('\''), std::string_view{"'\\'' (39)"});
    // Non-printable (BEL = 0x07)
    ok &= t.assert_eq(fmt('\x07'), std::string_view{"'\\x07' (7)"});
    // DEL (0x7F)
    ok &= t.assert_eq(fmt('\x7F'), std::string_view{"'\\x7f' (127)"});
    return ok;
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
    // INT64_MIN boundary: exercises -(v+1)+1 path in format_int
    ok &= t.assert_eq(fmt(INT64_MIN), std::string_view{"-9223372036854775808"});
    ok &= t.assert_eq(fmt(INT64_MAX), std::string_view{"9223372036854775807"});
    return ok;
}

// =============================================================================
// Floating-point formatting
// =============================================================================

bool test_format_float(TestContext& t) {
    bool ok = true;
    // format_value outputs ".0" suffix for whole-number floats to distinguish from integers
    ok &= t.assert_eq(fmt(0.0), std::string_view{"0.0"});
    ok &= t.assert_eq(fmt(1.0), std::string_view{"1.0"});
    ok &= t.assert_eq(fmt(-1.0), std::string_view{"-1.0"});
    ok &= t.assert_eq(fmt(3.14), std::string_view{"3.14"});
    ok &= t.assert_eq(fmt(-2.5), std::string_view{"-2.5"});
    return ok;
}

bool test_format_float_precision(TestContext& t) {
    bool ok = true;
    // 6 significant digits with rounding
    ok &= t.assert_eq(fmt(1.23456789), std::string_view{"1.23457"});
    // Small fractional value
    ok &= t.assert_eq(fmt(0.123456), std::string_view{"0.123456"});
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
    // Must start with "0x" prefix
    ok &= t.assert_true(sv.starts_with("0x"), "pointer has 0x prefix");
    ok &= t.assert_true(sv.size() > 2, "pointer has hex digits after 0x");

    // Null pointer → "0x0"
    format_value(buf.data(), buf.size(), static_cast<int*>(nullptr));
    sv = std::string_view{buf.data()};
    ok &= t.assert_eq(sv, std::string_view{"0x0"});
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

// =============================================================================
// string_view formatting
// =============================================================================

bool test_format_string_view(TestContext& t) {
    std::array<char, 128> buf{};
    format_value(buf.data(), buf.size(), std::string_view{"hello"});
    auto sv = std::string_view{buf.data()};
    bool ok = true;
    ok &= t.assert_eq(sv, std::string_view{"\"hello\""});

    // Empty string_view
    format_value(buf.data(), buf.size(), std::string_view{""});
    sv = std::string_view{buf.data()};
    ok &= t.assert_eq(sv, std::string_view{"\"\""});
    return ok;
}

// =============================================================================
// UINT64_MAX / nullptr formatting
// =============================================================================

bool test_format_uint64_max(TestContext& t) {
    return t.assert_eq(fmt(UINT64_MAX), std::string_view{"18446744073709551615"});
}

bool test_format_nullptr(TestContext& t) {
    std::array<char, 128> buf{};
    format_value(buf.data(), buf.size(), nullptr);
    auto sv = std::string_view{buf.data()};
    return t.assert_eq(sv, std::string_view{"nullptr"});
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

    umi::test::Suite::section("format_value: string_view");
    suite.run("basic / empty", test_format_string_view);

    umi::test::Suite::section("format_value: boundary");
    suite.run("UINT64_MAX", test_format_uint64_max);
    suite.run("nullptr", test_format_nullptr);

    umi::test::Suite::section("format_value: unknown");
    suite.run("opaque type", test_format_unknown_type);
}

} // namespace umitest::test
