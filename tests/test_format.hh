#pragma once

// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief Tests for format_value output across all supported types.
/// @author Shota Moriguchi @tekitounix

#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>

#include <umitest/format.hh>
#include <umitest/test.hh>

namespace umitest::test {
using umi::test::TestContext;
namespace detail_test {

using umi::test::BoundedWriter;

// Compile-time static_assert tests for BoundedWriter
static_assert([] {
    BoundedWriter w(nullptr, 0);
    w.put('x');
    w.puts("hello");
    return w.written() == 0 && !w.truncated();
}());

static_assert([] {
    std::array<char, 1> buf{};
    BoundedWriter w(buf.data(), buf.size());
    w.put('x');
    return buf[0] == '\0' && w.written() == 1 && w.truncated();
}());

static_assert([] {
    std::array<char, 2> buf{};
    BoundedWriter w(buf.data(), buf.size());
    w.put('A');
    return buf[0] == 'A' && buf[1] == '\0' && w.written() == 1 && !w.truncated();
}());

static_assert([] {
    std::array<char, 8> buf{};
    BoundedWriter w(buf.data(), buf.size());
    umi::test::detail::format_int(w, -42);
    return buf[0] == '-' && buf[1] == '4' && buf[2] == '2' && buf[3] == '\0';
}());

/// Helper: format a value into a thread-local buffer and return string_view.
template <typename T>
inline std::string_view fmt(const T& v) {
    thread_local std::array<char, 128> buf{};
    std::memset(buf.data(), 0, buf.size());
    umi::test::detail::format_value(buf.data(), buf.size(), v);
    return {buf.data()};
}

} // namespace detail_test

inline void run_format_tests(umi::test::Suite& suite) {
    using detail_test::fmt;
    using umi::test::BoundedWriter;

    suite.section("format_value");

    suite.run("bool", [](TestContext& t) {
        t.eq(fmt(true), std::string_view{"true"});
        t.eq(fmt(false), std::string_view{"false"});
    });

    suite.run("char printable", [](TestContext& t) {
        t.eq(fmt('A'), std::string_view{"'A' (65)"});
        t.eq(fmt('Z'), std::string_view{"'Z' (90)"});
        t.eq(fmt(' '), std::string_view{"' ' (32)"});
    });

    suite.run("char special", [](TestContext& t) {
        t.eq(fmt('\0'), std::string_view{"'\\0' (0)"});
        t.eq(fmt('\n'), std::string_view{"'\\n' (10)"});
        t.eq(fmt('\t'), std::string_view{"'\\t' (9)"});
        t.eq(fmt('\r'), std::string_view{"'\\r' (13)"});
        t.eq(fmt('\\'), std::string_view{"'\\\\' (92)"});
        t.eq(fmt('\''), std::string_view{"'\\'' (39)"});
        t.eq(fmt('\x07'), std::string_view{"'\\x07' (7)"});
        t.eq(fmt('\x7F'), std::string_view{"'\\x7f' (127)"});
    });

    suite.run("unsigned int", [](TestContext& t) {
        t.eq(fmt(0U), std::string_view{"0"});
        t.eq(fmt(42U), std::string_view{"42"});
        t.eq(fmt(255U), std::string_view{"255"});
        t.eq(fmt(UINT64_MAX), std::string_view{"18446744073709551615"});
    });

    suite.run("signed int", [](TestContext& t) {
        t.eq(fmt(0), std::string_view{"0"});
        t.eq(fmt(-1), std::string_view{"-1"});
        t.eq(fmt(42), std::string_view{"42"});
        t.eq(fmt(-100), std::string_view{"-100"});
        t.eq(fmt(INT64_MIN), std::string_view{"-9223372036854775808"});
        t.eq(fmt(INT64_MAX), std::string_view{"9223372036854775807"});
    });

    suite.run("float basic", [](TestContext& t) {
        t.eq(fmt(0.0), std::string_view{"0.0"});
        t.eq(fmt(1.0), std::string_view{"1.0"});
        t.eq(fmt(-1.0), std::string_view{"-1.0"});
        t.eq(fmt(3.14), std::string_view{"3.14"});
    });

    suite.run("float special", [](TestContext& t) {
        t.eq(fmt(std::numeric_limits<double>::quiet_NaN()), std::string_view{"nan"});
        t.eq(fmt(std::numeric_limits<double>::infinity()), std::string_view{"inf"});
        t.eq(fmt(-std::numeric_limits<double>::infinity()), std::string_view{"-inf"});
        t.eq(fmt(-0.0), std::string_view{"-0.0"});
    });

    suite.run("float diagnostic edge cases", [](TestContext& t) {
        t.eq(fmt(999999.9), std::string_view{"999999.9"});
        t.eq(fmt(1234567.89), std::string_view{"1234567.89"});
        t.eq(fmt(99999.99), std::string_view{"99999.99"});
        t.is_true(fmt(1e-7).find('e') != std::string_view::npos);
        t.is_true(fmt(1e-10).find('e') != std::string_view::npos);
    });

    suite.run("enum", [](TestContext& t) {
        enum class Color : std::uint8_t { RED = 0, GREEN = 42, BLUE = 255 };
        t.eq(fmt(Color::RED), std::string_view{"0"});
        t.eq(fmt(Color::GREEN), std::string_view{"42"});
        t.eq(fmt(Color::BLUE), std::string_view{"255"});
    });

    suite.run("pointer", [](TestContext& t) {
        int const x = 0;
        std::array<char, 128> buf{};
        umi::test::detail::format_value(buf.data(), buf.size(), &x);
        auto sv = std::string_view{buf.data()};
        t.is_true(sv.starts_with("0x"));
        t.is_true(sv.size() > 2);

        umi::test::detail::format_value(buf.data(), buf.size(), static_cast<int*>(nullptr));
        sv = std::string_view{buf.data()};
        t.eq(sv, std::string_view{"0x0"});
    });

    suite.run("nullptr_t", [](TestContext& t) {
        std::array<char, 128> buf{};
        umi::test::detail::format_value(buf.data(), buf.size(), nullptr);
        t.eq(std::string_view{buf.data()}, std::string_view{"nullptr"});
    });

    suite.run("const char*", [](TestContext& t) {
        std::array<char, 128> buf{};
        const char* str = "test";
        umi::test::detail::format_value(buf.data(), buf.size(), str);
        t.eq(std::string_view{buf.data()}, std::string_view{"\"test\""});

        const char* null = nullptr;
        umi::test::detail::format_value(buf.data(), buf.size(), null);
        t.eq(std::string_view{buf.data()}, std::string_view{"(null)"});
    });

    suite.run("std::string", [](TestContext& t) {
        std::array<char, 128> buf{};
        const std::string str = "hello";
        umi::test::detail::format_value(buf.data(), buf.size(), str);
        t.eq(std::string_view{buf.data()}, std::string_view{"\"hello\""});
    });

    suite.run("std::string_view", [](TestContext& t) {
        std::array<char, 128> buf{};
        umi::test::detail::format_value(buf.data(), buf.size(), std::string_view{"hello"});
        t.eq(std::string_view{buf.data()}, std::string_view{"\"hello\""});
    });

    suite.run("unknown type", [](TestContext& t) {
        struct Opaque {
            int x;
        };
        Opaque const obj{42};
        std::array<char, 128> buf{};
        umi::test::detail::format_value(buf.data(), buf.size(), obj);
        t.eq(std::string_view{buf.data()}, std::string_view{"(?)"});
    });

    suite.run("near extra keeps tiny magnitudes", [](TestContext& t) {
        std::array<char, 128> buf{};
        umi::test::detail::format_near_extra(buf.data(), buf.size(), 1e-10, 0.0, 1e-12);
        t.eq(std::string_view{buf.data()}, std::string_view{"eps=1e-12, diff=1e-10"});
    });

    suite.section("BoundedWriter");

    suite.run("size==0 safety", [](TestContext& t) {
        BoundedWriter w(nullptr, 0);
        w.put('x');
        w.puts("hello");
        t.eq(w.written(), std::size_t{0});
        t.is_false(w.truncated());
    });

    suite.run("size==1 safety", [](TestContext& t) {
        std::array<char, 1> buf{};
        BoundedWriter w(buf.data(), buf.size());
        w.put('x');
        t.eq(buf[0], '\0');
        t.eq(w.written(), std::size_t{1});
        t.is_true(w.truncated());
    });

    suite.run("truncation", [](TestContext& t) {
        std::array<char, 4> buf{};
        BoundedWriter w(buf.data(), buf.size());
        w.puts("hello");
        t.eq(std::string_view{buf.data()}, std::string_view{"hel"});
        t.is_true(w.truncated());
    });
}

} // namespace umitest::test
