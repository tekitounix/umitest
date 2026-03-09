#pragma once

// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief TestContext — check context passed to run() test functions.
/// @author Shota Moriguchi @tekitounix

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <source_location>
#include <span>
#include <string_view>
#include <type_traits>

#include <umitest/check.hh>
#include <umitest/failure.hh>
#include <umitest/format.hh>

namespace umi::test {

/// @brief Check context passed to run() test functions.
/// @details Non-copyable, non-movable. Constructed by BasicSuite::run() only.
class TestContext {
  public:
    using FailCallback = void (*)(const FailureView& failure, void* ctx);

    /// @brief RAII note guard. Pops note on destruction. Move-only.
    class NoteGuard {
      public:
        NoteGuard(const NoteGuard&) = delete;
        NoteGuard& operator=(const NoteGuard&) = delete;
        NoteGuard(NoteGuard&& other) noexcept : ctx(other.ctx), active(other.active) { other.active = false; }
        NoteGuard& operator=(NoteGuard&&) = delete;
        ~NoteGuard() {
            if (active) {
                ctx.pop_note();
            }
        }

      private:
        friend class TestContext;
        NoteGuard(TestContext& ctx) : ctx(ctx) {}
        TestContext& ctx;
        bool active = true;
    };

    TestContext(const TestContext&) = delete;
    TestContext& operator=(const TestContext&) = delete;
    TestContext(TestContext&&) = delete;
    TestContext& operator=(TestContext&&) = delete;
    ~TestContext() = default;

    /// @brief true if no check has failed.
    [[nodiscard]] bool ok() const { return !failed; }

    // -- Note --

    /// @brief Push a context note. Returns RAII guard that pops on destruction.
    /// @param msg null-terminated string that must outlive the NoteGuard scope.
    [[nodiscard]] NoteGuard note(const char* msg) {
        push_note(msg != nullptr ? msg : "(null)");
        return NoteGuard(*this);
    }

    // -- Soft checks --

    bool is_true(bool cond, std::source_location loc = std::source_location::current()) {
        ++checked;
        if (check_true(cond)) {
            return true;
        }
        report_bool_fail("true", loc);
        return false;
    }

    bool is_false(bool cond, std::source_location loc = std::source_location::current()) {
        ++checked;
        if (check_false(cond)) {
            return true;
        }
        report_bool_fail("false", loc);
        return false;
    }

    template <typename A, typename B>
        requires(std::equality_comparable_with<A, B> &&
                 !detail::excluded_char_pointer_v<std::remove_cvref_t<A>, std::remove_cvref_t<B>>)
    bool eq(const A& a, const B& b, std::source_location loc = std::source_location::current()) {
        ++checked;
        if (check_eq(a, b)) {
            return true;
        }
        report_compare_fail(a, "eq", b, loc);
        return false;
    }

    bool eq(const char* a, const char* b, std::source_location loc = std::source_location::current()) {
        ++checked;
        if (check_eq(a, b)) {
            return true;
        }
        report_compare_fail(a, "eq", b, loc);
        return false;
    }

    template <typename A, typename B>
        requires(std::equality_comparable_with<A, B> &&
                 !detail::excluded_char_pointer_v<std::remove_cvref_t<A>, std::remove_cvref_t<B>>)
    bool ne(const A& a, const B& b, std::source_location loc = std::source_location::current()) {
        ++checked;
        if (check_ne(a, b)) {
            return true;
        }
        report_compare_fail(a, "ne", b, loc);
        return false;
    }

    bool ne(const char* a, const char* b, std::source_location loc = std::source_location::current()) {
        ++checked;
        if (check_ne(a, b)) {
            return true;
        }
        report_compare_fail(a, "ne", b, loc);
        return false;
    }

    template <typename A, typename B>
        requires(std::totally_ordered_with<A, B> && !std::is_pointer_v<std::decay_t<A>> &&
                 !std::is_pointer_v<std::decay_t<B>>)
    bool lt(const A& a, const B& b, std::source_location loc = std::source_location::current()) {
        ++checked;
        if (check_lt(a, b)) {
            return true;
        }
        report_compare_fail(a, "lt", b, loc);
        return false;
    }

    template <typename A, typename B>
        requires(std::totally_ordered_with<A, B> && !std::is_pointer_v<std::decay_t<A>> &&
                 !std::is_pointer_v<std::decay_t<B>>)
    bool le(const A& a, const B& b, std::source_location loc = std::source_location::current()) {
        ++checked;
        if (check_le(a, b)) {
            return true;
        }
        report_compare_fail(a, "le", b, loc);
        return false;
    }

    template <typename A, typename B>
        requires(std::totally_ordered_with<A, B> && !std::is_pointer_v<std::decay_t<A>> &&
                 !std::is_pointer_v<std::decay_t<B>>)
    bool gt(const A& a, const B& b, std::source_location loc = std::source_location::current()) {
        ++checked;
        if (check_gt(a, b)) {
            return true;
        }
        report_compare_fail(a, "gt", b, loc);
        return false;
    }

    template <typename A, typename B>
        requires(std::totally_ordered_with<A, B> && !std::is_pointer_v<std::decay_t<A>> &&
                 !std::is_pointer_v<std::decay_t<B>>)
    bool ge(const A& a, const B& b, std::source_location loc = std::source_location::current()) {
        ++checked;
        if (check_ge(a, b)) {
            return true;
        }
        report_compare_fail(a, "ge", b, loc);
        return false;
    }

    template <std::floating_point A, std::floating_point B>
    bool near(const A& a,
              const B& b,
              std::common_type_t<A, B> eps = static_cast<std::common_type_t<A, B>>(0.001),
              std::source_location loc = std::source_location::current()) {
        ++checked;
        if (check_near(a, b, eps)) {
            return true;
        }
        report_near_fail(a, b, eps, loc);
        return false;
    }

    // -- Exception checks (available only when exceptions are enabled) --

#if defined(__cpp_exceptions) || defined(__EXCEPTIONS)

    /// @brief Check that fn() throws an exception of type E.
    template <typename E, typename F>
        requires std::invocable<F>
    bool throws(F&& fn, std::source_location loc = std::source_location::current()) {
        ++checked;
        try {
            fn();
        } catch (const E&) {
            return true;
        } catch (...) {
            report_exception_fail("throws_as", "wrong exception type", loc);
            return false;
        }
        report_exception_fail("throws_as", "no exception thrown", loc);
        return false;
    }

    /// @brief Check that fn() throws any exception.
    template <typename F>
        requires std::invocable<F>
    bool throws(F&& fn, std::source_location loc = std::source_location::current()) {
        ++checked;
        try {
            fn();
        } catch (...) {
            return true;
        }
        report_exception_fail("throws", "no exception thrown", loc);
        return false;
    }

    /// @brief Check that fn() does not throw.
    template <typename F>
        requires std::invocable<F>
    bool nothrow(F&& fn, std::source_location loc = std::source_location::current()) {
        ++checked;
        try {
            fn();
            return true;
        } catch (...) {
            report_exception_fail("nothrow", "unexpected exception thrown", loc);
            return false;
        }
    }

#endif // __cpp_exceptions || __EXCEPTIONS

    // -- String checks --

    /// @brief Check that haystack contains needle.
    bool str_contains(std::string_view haystack,
                      std::string_view needle,
                      std::source_location loc = std::source_location::current()) {
        ++checked;
        if (check_str_contains(haystack, needle)) {
            return true;
        }
        report_string_fail("str_contains", haystack, needle, loc);
        return false;
    }

    /// @brief Check that s starts with prefix.
    bool str_starts_with(std::string_view s,
                         std::string_view prefix,
                         std::source_location loc = std::source_location::current()) {
        ++checked;
        if (check_str_starts_with(s, prefix)) {
            return true;
        }
        report_string_fail("str_starts_with", s, prefix, loc);
        return false;
    }

    /// @brief Check that s ends with suffix.
    bool str_ends_with(std::string_view s,
                       std::string_view suffix,
                       std::source_location loc = std::source_location::current()) {
        ++checked;
        if (check_str_ends_with(s, suffix)) {
            return true;
        }
        report_string_fail("str_ends_with", s, suffix, loc);
        return false;
    }

    // -- Fatal checks --

    bool require_true(bool cond, std::source_location loc = std::source_location::current()) {
        ++checked;
        if (check_true(cond)) {
            return true;
        }
        report_bool_fail("true", loc, true);
        return false;
    }

    bool require_false(bool cond, std::source_location loc = std::source_location::current()) {
        ++checked;
        if (check_false(cond)) {
            return true;
        }
        report_bool_fail("false", loc, true);
        return false;
    }

    template <typename A, typename B>
        requires(std::equality_comparable_with<A, B> &&
                 !detail::excluded_char_pointer_v<std::remove_cvref_t<A>, std::remove_cvref_t<B>>)
    bool require_eq(const A& a, const B& b, std::source_location loc = std::source_location::current()) {
        ++checked;
        if (check_eq(a, b)) {
            return true;
        }
        report_compare_fail(a, "eq", b, loc, true);
        return false;
    }

    bool require_eq(const char* a, const char* b, std::source_location loc = std::source_location::current()) {
        ++checked;
        if (check_eq(a, b)) {
            return true;
        }
        report_compare_fail(a, "eq", b, loc, true);
        return false;
    }

    template <typename A, typename B>
        requires(std::equality_comparable_with<A, B> &&
                 !detail::excluded_char_pointer_v<std::remove_cvref_t<A>, std::remove_cvref_t<B>>)
    bool require_ne(const A& a, const B& b, std::source_location loc = std::source_location::current()) {
        ++checked;
        if (check_ne(a, b)) {
            return true;
        }
        report_compare_fail(a, "ne", b, loc, true);
        return false;
    }

    bool require_ne(const char* a, const char* b, std::source_location loc = std::source_location::current()) {
        ++checked;
        if (check_ne(a, b)) {
            return true;
        }
        report_compare_fail(a, "ne", b, loc, true);
        return false;
    }

    template <typename A, typename B>
        requires(std::totally_ordered_with<A, B> && !std::is_pointer_v<std::decay_t<A>> &&
                 !std::is_pointer_v<std::decay_t<B>>)
    bool require_lt(const A& a, const B& b, std::source_location loc = std::source_location::current()) {
        ++checked;
        if (check_lt(a, b)) {
            return true;
        }
        report_compare_fail(a, "lt", b, loc, true);
        return false;
    }

    template <typename A, typename B>
        requires(std::totally_ordered_with<A, B> && !std::is_pointer_v<std::decay_t<A>> &&
                 !std::is_pointer_v<std::decay_t<B>>)
    bool require_le(const A& a, const B& b, std::source_location loc = std::source_location::current()) {
        ++checked;
        if (check_le(a, b)) {
            return true;
        }
        report_compare_fail(a, "le", b, loc, true);
        return false;
    }

    template <typename A, typename B>
        requires(std::totally_ordered_with<A, B> && !std::is_pointer_v<std::decay_t<A>> &&
                 !std::is_pointer_v<std::decay_t<B>>)
    bool require_gt(const A& a, const B& b, std::source_location loc = std::source_location::current()) {
        ++checked;
        if (check_gt(a, b)) {
            return true;
        }
        report_compare_fail(a, "gt", b, loc, true);
        return false;
    }

    template <typename A, typename B>
        requires(std::totally_ordered_with<A, B> && !std::is_pointer_v<std::decay_t<A>> &&
                 !std::is_pointer_v<std::decay_t<B>>)
    bool require_ge(const A& a, const B& b, std::source_location loc = std::source_location::current()) {
        ++checked;
        if (check_ge(a, b)) {
            return true;
        }
        report_compare_fail(a, "ge", b, loc, true);
        return false;
    }

    template <std::floating_point A, std::floating_point B>
    bool require_near(const A& a,
                      const B& b,
                      std::common_type_t<A, B> eps = static_cast<std::common_type_t<A, B>>(0.001),
                      std::source_location loc = std::source_location::current()) {
        ++checked;
        if (check_near(a, b, eps)) {
            return true;
        }
        report_near_fail(a, b, eps, loc, true);
        return false;
    }

#if defined(__cpp_exceptions) || defined(__EXCEPTIONS)

    /// @brief Fatal: check that fn() throws an exception of type E.
    template <typename E, typename F>
        requires std::invocable<F>
    bool require_throws(F&& fn, std::source_location loc = std::source_location::current()) {
        ++checked;
        try {
            fn();
        } catch (const E&) {
            return true;
        } catch (...) {
            report_exception_fail("throws_as", "wrong exception type", loc, true);
            return false;
        }
        report_exception_fail("throws_as", "no exception thrown", loc, true);
        return false;
    }

    /// @brief Fatal: check that fn() throws any exception.
    template <typename F>
        requires std::invocable<F>
    bool require_throws(F&& fn, std::source_location loc = std::source_location::current()) {
        ++checked;
        try {
            fn();
        } catch (...) {
            return true;
        }
        report_exception_fail("throws", "no exception thrown", loc, true);
        return false;
    }

    /// @brief Fatal: check that fn() does not throw.
    template <typename F>
        requires std::invocable<F>
    bool require_nothrow(F&& fn, std::source_location loc = std::source_location::current()) {
        ++checked;
        try {
            fn();
            return true;
        } catch (...) {
            report_exception_fail("nothrow", "unexpected exception thrown", loc, true);
            return false;
        }
    }

#endif // __cpp_exceptions || __EXCEPTIONS

    /// @brief Fatal: check that haystack contains needle.
    bool require_str_contains(std::string_view haystack,
                              std::string_view needle,
                              std::source_location loc = std::source_location::current()) {
        ++checked;
        if (check_str_contains(haystack, needle)) {
            return true;
        }
        report_string_fail("str_contains", haystack, needle, loc, true);
        return false;
    }

    /// @brief Fatal: check that s starts with prefix.
    bool require_str_starts_with(std::string_view s,
                                 std::string_view prefix,
                                 std::source_location loc = std::source_location::current()) {
        ++checked;
        if (check_str_starts_with(s, prefix)) {
            return true;
        }
        report_string_fail("str_starts_with", s, prefix, loc, true);
        return false;
    }

    /// @brief Fatal: check that s ends with suffix.
    bool require_str_ends_with(std::string_view s,
                               std::string_view suffix,
                               std::source_location loc = std::source_location::current()) {
        ++checked;
        if (check_str_ends_with(s, suffix)) {
            return true;
        }
        report_string_fail("str_ends_with", s, suffix, loc, true);
        return false;
    }

    // -- Construction and stats (BasicSuite::run() use only, not user API) --

    /// @pre name != nullptr
    /// @pre cb != nullptr
    explicit TestContext(const char* name, FailCallback cb, void* ctx) : test_name(name), fail_cb(cb), fail_ctx(ctx) {}

    [[nodiscard]] int checked_count() const { return checked; }
    [[nodiscard]] int failed_count() const { return fail_count; }

  private:
    static constexpr int max_notes = 4;
    std::array<const char*, max_notes> note_stack{};
    int note_depth = 0;

    void push_note(const char* msg) {
        if (note_depth < max_notes) {
            note_stack[static_cast<std::size_t>(note_depth)] = msg;
        }
        ++note_depth;
    }

    void pop_note() { --note_depth; }

    [[nodiscard]] std::span<const char* const> active_notes() const {
        const auto n = std::min(note_depth, max_notes);
        return {note_stack.data(), static_cast<std::size_t>(n)};
    }

    void report_bool_fail(const char* kind, std::source_location loc, bool fatal = false) {
        const FailureView fv{.test_name = test_name,
                             .loc = loc,
                             .is_fatal = fatal,
                             .kind = kind,
                             .lhs = nullptr,
                             .rhs = nullptr,
                             .extra = nullptr,
                             .notes = active_notes()};
        fail_cb(fv, fail_ctx);
        failed = true;
        ++fail_count;
    }

    template <typename A, typename B>
    void report_compare_fail(const A& a, const char* kind, const B& b, std::source_location loc, bool fatal = false) {
        std::array<char, detail::fail_message_capacity> lhs_buf{};
        std::array<char, detail::fail_message_capacity> rhs_buf{};
        detail::format_value(lhs_buf.data(), lhs_buf.size(), a);
        detail::format_value(rhs_buf.data(), rhs_buf.size(), b);
        const FailureView fv{.test_name = test_name,
                             .loc = loc,
                             .is_fatal = fatal,
                             .kind = kind,
                             .lhs = lhs_buf.data(),
                             .rhs = rhs_buf.data(),
                             .extra = nullptr,
                             .notes = active_notes()};
        fail_cb(fv, fail_ctx);
        failed = true;
        ++fail_count;
    }

    template <std::floating_point A, std::floating_point B>
    void report_near_fail(
        const A& a, const B& b, std::common_type_t<A, B> eps, std::source_location loc, bool fatal = false) {
        std::array<char, detail::fail_message_capacity> lhs_buf{};
        std::array<char, detail::fail_message_capacity> rhs_buf{};
        std::array<char, 64> extra_buf{};
        detail::format_value(lhs_buf.data(), lhs_buf.size(), a);
        detail::format_value(rhs_buf.data(), rhs_buf.size(), b);
        detail::format_near_extra(extra_buf.data(), extra_buf.size(), a, b, eps);
        const FailureView fv{.test_name = test_name,
                             .loc = loc,
                             .is_fatal = fatal,
                             .kind = "near",
                             .lhs = lhs_buf.data(),
                             .rhs = rhs_buf.data(),
                             .extra = extra_buf.data(),
                             .notes = active_notes()};
        fail_cb(fv, fail_ctx);
        failed = true;
        ++fail_count;
    }

    void report_exception_fail(const char* kind, const char* detail, std::source_location loc, bool fatal = false) {
        const FailureView fv{.test_name = test_name,
                             .loc = loc,
                             .is_fatal = fatal,
                             .kind = kind,
                             .lhs = detail,
                             .rhs = nullptr,
                             .extra = nullptr,
                             .notes = active_notes()};
        fail_cb(fv, fail_ctx);
        failed = true;
        ++fail_count;
    }

    void report_string_fail(const char* kind,
                            std::string_view haystack,
                            std::string_view needle,
                            std::source_location loc,
                            bool fatal = false) {
        std::array<char, detail::fail_message_capacity> lhs_buf{};
        std::array<char, detail::fail_message_capacity> rhs_buf{};
        detail::format_value(lhs_buf.data(), lhs_buf.size(), haystack);
        detail::format_value(rhs_buf.data(), rhs_buf.size(), needle);
        const FailureView fv{.test_name = test_name,
                             .loc = loc,
                             .is_fatal = fatal,
                             .kind = kind,
                             .lhs = lhs_buf.data(),
                             .rhs = rhs_buf.data(),
                             .extra = nullptr,
                             .notes = active_notes()};
        fail_cb(fv, fail_ctx);
        failed = true;
        ++fail_count;
    }

    const char* test_name;
    FailCallback fail_cb;
    void* fail_ctx;
    bool failed = false;
    int checked = 0;
    int fail_count = 0;
};

} // namespace umi::test
