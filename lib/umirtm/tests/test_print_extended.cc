// SPDX-License-Identifier: MIT
/// @file
/// @brief Extended print/println tests: brace escaping, format overrides,
///        multiple arguments, println variants, printf/snprintf edge cases.

#include <array>
#include <cstring>
#include <string_view>

#include "test_fixture.hh"

namespace umirtm::test {
namespace {

using umi::test::TestContext;

// =============================================================================
// println() variants
// =============================================================================

bool test_println_no_args_returns_one(TestContext& t) {
    int n = rt::println();
    return t.assert_eq(n, 1);
}

bool test_println_string(TestContext& t) {
    int n = rt::println("hello");
    // "hello\n" = 6 chars
    return t.assert_ge(n, 6);
}

bool test_println_with_placeholder(TestContext& t) {
    int n = rt::println("val={}", 42);
    // "val=42\n" = 7 chars
    return t.assert_ge(n, 7);
}

bool test_println_multiple_args(TestContext& t) {
    int n = rt::println("{} {} {}", 1, 2, 3);
    // "1 2 3\n" = 6 chars
    return t.assert_ge(n, 6);
}

// =============================================================================
// printf() edge cases
// =============================================================================

bool test_printf_empty_string(TestContext& t) {
    int n = rt::printf("");
    return t.assert_eq(n, 0);
}

bool test_printf_just_newline(TestContext& t) {
    int n = rt::printf("\n");
    return t.assert_eq(n, 1);
}

bool test_printf_no_format_specifiers(TestContext& t) {
    int n = rt::printf("plain text");
    return t.assert_eq(n, 10);
}

bool test_printf_multiple_percent_d(TestContext& t) {
    int n = rt::printf("%d %d %d", 1, 2, 3);
    return t.assert_eq(n, 5); // "1 2 3"
}

bool test_printf_char_format(TestContext& t) {
    int n = rt::printf("%c%c%c", 'A', 'B', 'C');
    return t.assert_eq(n, 3);
}

// =============================================================================
// snprintf edge cases
// =============================================================================

bool test_snprintf_empty_format(TestContext& t) {
    std::array<char, 32> buf{};
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-zero-length"
    rt::snprintf<rt::DefaultConfig>(buf.data(), buf.size(), "");
#pragma GCC diagnostic pop
    return t.assert_eq(std::string_view{buf.data()}, std::string_view{""});
}

bool test_snprintf_just_text(TestContext& t) {
    std::array<char, 32> buf{};
    rt::snprintf<rt::DefaultConfig>(buf.data(), buf.size(), "hello");
    return t.assert_eq(std::string_view{buf.data()}, std::string_view{"hello"});
}

bool test_snprintf_multiple_percent_percent(TestContext& t) {
    std::array<char, 32> buf{};
    rt::snprintf<rt::DefaultConfig>(buf.data(), buf.size(), "%%%%");
    return t.assert_eq(std::string_view{buf.data()}, std::string_view{"%%"});
}

bool test_snprintf_char_format(TestContext& t) {
    std::array<char, 32> buf{};
    rt::snprintf<rt::DefaultConfig>(buf.data(), buf.size(), "%c", 'Z');
    return t.assert_eq(std::string_view{buf.data()}, std::string_view{"Z"});
}

bool test_snprintf_negative_int(TestContext& t) {
    std::array<char, 32> buf{};
    rt::snprintf<rt::DefaultConfig>(buf.data(), buf.size(), "%d", -1);
    return t.assert_eq(std::string_view{buf.data()}, std::string_view{"-1"});
}

bool test_snprintf_zero(TestContext& t) {
    std::array<char, 32> buf{};
    rt::snprintf<rt::DefaultConfig>(buf.data(), buf.size(), "%d", 0);
    return t.assert_eq(std::string_view{buf.data()}, std::string_view{"0"});
}

bool test_snprintf_left_align_int(TestContext& t) {
    std::array<char, 32> buf{};
    rt::snprintf<rt::DefaultConfig>(buf.data(), buf.size(), "%-5d!", 42);
    return t.assert_eq(std::string_view{buf.data()}, std::string_view{"42   !"});
}

bool test_snprintf_octal_zero(TestContext& t) {
    std::array<char, 32> buf{};
    rt::snprintf<rt::DefaultConfig>(buf.data(), buf.size(), "%o", 0u);
    return t.assert_eq(std::string_view{buf.data()}, std::string_view{"0"});
}

bool test_snprintf_pointer_null(TestContext& t) {
    std::array<char, 32> buf{};
    rt::snprintf<rt::DefaultConfig>(buf.data(), buf.size(), "%p", nullptr);
    // Null pointer representation varies, just verify non-empty output
    return t.assert_true(std::strlen(buf.data()) > 0, "null pointer has output");
}

bool test_snprintf_string_empty(TestContext& t) {
    std::array<char, 32> buf{};
    rt::snprintf<rt::DefaultConfig>(buf.data(), buf.size(), "%s", "");
    return t.assert_eq(std::string_view{buf.data()}, std::string_view{""});
}

} // namespace

void run_print_extended_tests(umi::test::Suite& suite);

void run_print_extended_tests(umi::test::Suite& suite) {
    umi::test::Suite::section("println: variants");
    suite.run("no args", test_println_no_args_returns_one);
    suite.run("string", test_println_string);
    suite.run("placeholder", test_println_with_placeholder);
    suite.run("multiple args", test_println_multiple_args);

    umi::test::Suite::section("printf: edge cases");
    suite.run("empty string", test_printf_empty_string);
    suite.run("just newline", test_printf_just_newline);
    suite.run("no specifiers", test_printf_no_format_specifiers);
    suite.run("multiple %d", test_printf_multiple_percent_d);
    suite.run("char %c", test_printf_char_format);

    umi::test::Suite::section("snprintf: additional");
    suite.run("empty format", test_snprintf_empty_format);
    suite.run("just text", test_snprintf_just_text);
    suite.run("multiple %%", test_snprintf_multiple_percent_percent);
    suite.run("char format", test_snprintf_char_format);
    suite.run("negative int", test_snprintf_negative_int);
    suite.run("zero", test_snprintf_zero);
    suite.run("left align", test_snprintf_left_align_int);
    suite.run("octal zero", test_snprintf_octal_zero);
    suite.run("null pointer", test_snprintf_pointer_null);
    suite.run("empty string", test_snprintf_string_empty);
}

} // namespace umirtm::test
