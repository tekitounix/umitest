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
        explicit NoteGuard(TestContext& ctx) : ctx(ctx) {}
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

    /// @brief Soft: check boolean true.
    bool is_true(bool cond, std::source_location loc = std::source_location::current()) {
        return bool_check<false>(cond, "true", loc);
    }

    /// @brief Soft: check boolean false.
    bool is_false(bool cond, std::source_location loc = std::source_location::current()) {
        return bool_check<false>(!cond, "false", loc);
    }

    /// @brief Soft: check equality.
    template <typename A, typename B>
        requires(std::equality_comparable_with<A, B> &&
                 !detail::excluded_char_pointer_v<std::remove_cvref_t<A>, std::remove_cvref_t<B>>)
    bool eq(const A& a, const B& b, std::source_location loc = std::source_location::current()) {
        return compare_check<false>(check_eq(a, b), a, "eq", b, loc);
    }

    /// @brief Soft: check equality for C strings.
    bool eq(const char* a, const char* b, std::source_location loc = std::source_location::current()) {
        return compare_check<false>(check_eq(a, b), a, "eq", b, loc);
    }

    /// @brief Soft: check inequality.
    template <typename A, typename B>
        requires(std::equality_comparable_with<A, B> &&
                 !detail::excluded_char_pointer_v<std::remove_cvref_t<A>, std::remove_cvref_t<B>>)
    bool ne(const A& a, const B& b, std::source_location loc = std::source_location::current()) {
        return compare_check<false>(check_ne(a, b), a, "ne", b, loc);
    }

    /// @brief Soft: check inequality for C strings.
    bool ne(const char* a, const char* b, std::source_location loc = std::source_location::current()) {
        return compare_check<false>(check_ne(a, b), a, "ne", b, loc);
    }

    /// @brief Soft: check less-than.
    template <typename A, typename B>
        requires OrderableNonPointer<A, B>
    bool lt(const A& a, const B& b, std::source_location loc = std::source_location::current()) {
        return compare_check<false>(check_lt(a, b), a, "lt", b, loc);
    }

    /// @brief Soft: check less-or-equal.
    template <typename A, typename B>
        requires OrderableNonPointer<A, B>
    bool le(const A& a, const B& b, std::source_location loc = std::source_location::current()) {
        return compare_check<false>(check_le(a, b), a, "le", b, loc);
    }

    /// @brief Soft: check greater-than.
    template <typename A, typename B>
        requires OrderableNonPointer<A, B>
    bool gt(const A& a, const B& b, std::source_location loc = std::source_location::current()) {
        return compare_check<false>(check_gt(a, b), a, "gt", b, loc);
    }

    /// @brief Soft: check greater-or-equal.
    template <typename A, typename B>
        requires OrderableNonPointer<A, B>
    bool ge(const A& a, const B& b, std::source_location loc = std::source_location::current()) {
        return compare_check<false>(check_ge(a, b), a, "ge", b, loc);
    }

    /// @brief Soft: check approximate equality.
    template <std::floating_point A, std::floating_point B>
    bool near(const A& a,
              const B& b,
              std::common_type_t<A, B> eps = static_cast<std::common_type_t<A, B>>(0.001),
              std::source_location loc = std::source_location::current()) {
        return near_check<false>(a, b, eps, loc);
    }

    // -- Exception checks (available only when exceptions are enabled) --

#if defined(__cpp_exceptions) || defined(__EXCEPTIONS)

    /// @brief Soft: check that fn() throws an exception of type E.
    template <typename E, typename F>
        requires std::invocable<F>
    bool throws(F&& fn, std::source_location loc = std::source_location::current()) {
        return throws_as_check<false, E>(std::forward<F>(fn), loc);
    }

    /// @brief Soft: check that fn() throws any exception.
    template <typename F>
        requires std::invocable<F>
    bool throws(F&& fn, std::source_location loc = std::source_location::current()) {
        return throws_any_check<false>(std::forward<F>(fn), loc);
    }

    /// @brief Soft: check that fn() does not throw.
    template <typename F>
        requires std::invocable<F>
    bool nothrow(F&& fn, std::source_location loc = std::source_location::current()) {
        return nothrow_check<false>(std::forward<F>(fn), loc);
    }

#endif // __cpp_exceptions || __EXCEPTIONS

    // -- String checks --

    /// @brief Soft: check that haystack contains needle.
    bool str_contains(std::string_view haystack,
                      std::string_view needle,
                      std::source_location loc = std::source_location::current()) {
        return string_check<false>(check_str_contains(haystack, needle), "str_contains", haystack, needle, loc);
    }

    /// @brief Soft: check that s starts with prefix.
    bool str_starts_with(std::string_view s,
                         std::string_view prefix,
                         std::source_location loc = std::source_location::current()) {
        return string_check<false>(check_str_starts_with(s, prefix), "str_starts_with", s, prefix, loc);
    }

    /// @brief Soft: check that s ends with suffix.
    bool str_ends_with(std::string_view s,
                       std::string_view suffix,
                       std::source_location loc = std::source_location::current()) {
        return string_check<false>(check_str_ends_with(s, suffix), "str_ends_with", s, suffix, loc);
    }

    // -- Fatal checks --

    /// @brief Fatal: check boolean true.
    [[nodiscard]] bool require_true(bool cond, std::source_location loc = std::source_location::current()) {
        return bool_check<true>(cond, "true", loc);
    }

    /// @brief Fatal: check boolean false.
    [[nodiscard]] bool require_false(bool cond, std::source_location loc = std::source_location::current()) {
        return bool_check<true>(!cond, "false", loc);
    }

    /// @brief Fatal: check equality.
    template <typename A, typename B>
        requires(std::equality_comparable_with<A, B> &&
                 !detail::excluded_char_pointer_v<std::remove_cvref_t<A>, std::remove_cvref_t<B>>)
    [[nodiscard]] bool require_eq(const A& a, const B& b, std::source_location loc = std::source_location::current()) {
        return compare_check<true>(check_eq(a, b), a, "eq", b, loc);
    }

    /// @brief Fatal: check equality for C strings.
    [[nodiscard]] bool
    require_eq(const char* a, const char* b, std::source_location loc = std::source_location::current()) {
        return compare_check<true>(check_eq(a, b), a, "eq", b, loc);
    }

    /// @brief Fatal: check inequality.
    template <typename A, typename B>
        requires(std::equality_comparable_with<A, B> &&
                 !detail::excluded_char_pointer_v<std::remove_cvref_t<A>, std::remove_cvref_t<B>>)
    [[nodiscard]] bool require_ne(const A& a, const B& b, std::source_location loc = std::source_location::current()) {
        return compare_check<true>(check_ne(a, b), a, "ne", b, loc);
    }

    /// @brief Fatal: check inequality for C strings.
    [[nodiscard]] bool
    require_ne(const char* a, const char* b, std::source_location loc = std::source_location::current()) {
        return compare_check<true>(check_ne(a, b), a, "ne", b, loc);
    }

    /// @brief Fatal: check less-than.
    template <typename A, typename B>
        requires OrderableNonPointer<A, B>
    [[nodiscard]] bool require_lt(const A& a, const B& b, std::source_location loc = std::source_location::current()) {
        return compare_check<true>(check_lt(a, b), a, "lt", b, loc);
    }

    /// @brief Fatal: check less-or-equal.
    template <typename A, typename B>
        requires OrderableNonPointer<A, B>
    [[nodiscard]] bool require_le(const A& a, const B& b, std::source_location loc = std::source_location::current()) {
        return compare_check<true>(check_le(a, b), a, "le", b, loc);
    }

    /// @brief Fatal: check greater-than.
    template <typename A, typename B>
        requires OrderableNonPointer<A, B>
    [[nodiscard]] bool require_gt(const A& a, const B& b, std::source_location loc = std::source_location::current()) {
        return compare_check<true>(check_gt(a, b), a, "gt", b, loc);
    }

    /// @brief Fatal: check greater-or-equal.
    template <typename A, typename B>
        requires OrderableNonPointer<A, B>
    [[nodiscard]] bool require_ge(const A& a, const B& b, std::source_location loc = std::source_location::current()) {
        return compare_check<true>(check_ge(a, b), a, "ge", b, loc);
    }

    /// @brief Fatal: check approximate equality.
    template <std::floating_point A, std::floating_point B>
    [[nodiscard]] bool require_near(const A& a,
                                    const B& b,
                                    std::common_type_t<A, B> eps = static_cast<std::common_type_t<A, B>>(0.001),
                                    std::source_location loc = std::source_location::current()) {
        return near_check<true>(a, b, eps, loc);
    }

#if defined(__cpp_exceptions) || defined(__EXCEPTIONS)

    /// @brief Fatal: check that fn() throws an exception of type E.
    template <typename E, typename F>
        requires std::invocable<F>
    [[nodiscard]] bool require_throws(F&& fn, std::source_location loc = std::source_location::current()) {
        return throws_as_check<true, E>(std::forward<F>(fn), loc);
    }

    /// @brief Fatal: check that fn() throws any exception.
    template <typename F>
        requires std::invocable<F>
    [[nodiscard]] bool require_throws(F&& fn, std::source_location loc = std::source_location::current()) {
        return throws_any_check<true>(std::forward<F>(fn), loc);
    }

    /// @brief Fatal: check that fn() does not throw.
    template <typename F>
        requires std::invocable<F>
    [[nodiscard]] bool require_nothrow(F&& fn, std::source_location loc = std::source_location::current()) {
        return nothrow_check<true>(std::forward<F>(fn), loc);
    }

#endif // __cpp_exceptions || __EXCEPTIONS

    /// @brief Fatal: check that haystack contains needle.
    [[nodiscard]] bool require_str_contains(std::string_view haystack,
                                            std::string_view needle,
                                            std::source_location loc = std::source_location::current()) {
        return string_check<true>(check_str_contains(haystack, needle), "str_contains", haystack, needle, loc);
    }

    /// @brief Fatal: check that s starts with prefix.
    [[nodiscard]] bool require_str_starts_with(std::string_view s,
                                               std::string_view prefix,
                                               std::source_location loc = std::source_location::current()) {
        return string_check<true>(check_str_starts_with(s, prefix), "str_starts_with", s, prefix, loc);
    }

    /// @brief Fatal: check that s ends with suffix.
    [[nodiscard]] bool require_str_ends_with(std::string_view s,
                                             std::string_view suffix,
                                             std::source_location loc = std::source_location::current()) {
        return string_check<true>(check_str_ends_with(s, suffix), "str_ends_with", s, suffix, loc);
    }

    // -- Construction and stats (BasicSuite::run() use only, not user API) --

    /// @brief Collect test results.
    struct Result {
        int checked;
        int failed;
        bool passed;
    };
    [[nodiscard]] Result result() { return {.checked = checked, .failed = fail_count, .passed = !failed}; }

    /// @pre name != nullptr
    /// @pre cb != nullptr
    /// @note Internal API — intended for BasicSuite::run() only, not user code.
    explicit TestContext(const char* name, FailCallback cb, void* ctx) : test_name(name), fail_cb(cb), fail_ctx(ctx) {}

  private:
    // -- Note stack --

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

    // -- Unified check implementations --

    void record_failure() {
        failed = true;
        ++fail_count;
    }

    template <bool Fatal>
    bool bool_check(bool passed, const char* kind, std::source_location loc) {
        ++checked;
        if (passed) {
            return true;
        }
        const FailureView fv{.test_name = test_name,
                             .loc = loc,
                             .is_fatal = Fatal,
                             .kind = kind,
                             .lhs = nullptr,
                             .rhs = nullptr,
                             .extra = nullptr,
                             .notes = active_notes()};
        fail_cb(fv, fail_ctx);
        record_failure();
        return false;
    }

    template <bool Fatal, typename A, typename B>
    bool compare_check(bool passed, const A& a, const char* kind, const B& b, std::source_location loc) {
        ++checked;
        if (passed) {
            return true;
        }
        std::array<char, detail::fail_message_capacity> lhs_buf{};
        std::array<char, detail::fail_message_capacity> rhs_buf{};
        detail::format_value(lhs_buf.data(), lhs_buf.size(), a);
        detail::format_value(rhs_buf.data(), rhs_buf.size(), b);
        const FailureView fv{.test_name = test_name,
                             .loc = loc,
                             .is_fatal = Fatal,
                             .kind = kind,
                             .lhs = lhs_buf.data(),
                             .rhs = rhs_buf.data(),
                             .extra = nullptr,
                             .notes = active_notes()};
        fail_cb(fv, fail_ctx);
        record_failure();
        return false;
    }

    template <bool Fatal, std::floating_point A, std::floating_point B>
    bool near_check(const A& a, const B& b, std::common_type_t<A, B> eps, std::source_location loc) {
        ++checked;
        if (check_near(a, b, eps)) {
            return true;
        }
        std::array<char, detail::fail_message_capacity> lhs_buf{};
        std::array<char, detail::fail_message_capacity> rhs_buf{};
        std::array<char, 64> extra_buf{};
        detail::format_value(lhs_buf.data(), lhs_buf.size(), a);
        detail::format_value(rhs_buf.data(), rhs_buf.size(), b);
        detail::format_near_extra(extra_buf.data(), extra_buf.size(), a, b, eps);
        const FailureView fv{.test_name = test_name,
                             .loc = loc,
                             .is_fatal = Fatal,
                             .kind = "near",
                             .lhs = lhs_buf.data(),
                             .rhs = rhs_buf.data(),
                             .extra = extra_buf.data(),
                             .notes = active_notes()};
        fail_cb(fv, fail_ctx);
        record_failure();
        return false;
    }

#if defined(__cpp_exceptions) || defined(__EXCEPTIONS)

    template <bool Fatal, typename E, typename F>
    bool throws_as_check(F&& fn, std::source_location loc) {
        ++checked;
        try {
            fn();
        } catch (const E&) {
            return true;
        } catch (...) {
            report_exception_fail("throws_as", "wrong exception type", loc, Fatal);
            return false;
        }
        report_exception_fail("throws_as", "no exception thrown", loc, Fatal);
        return false;
    }

    template <bool Fatal, typename F>
    bool throws_any_check(F&& fn, std::source_location loc) {
        ++checked;
        try {
            fn();
        } catch (...) {
            return true;
        }
        report_exception_fail("throws", "no exception thrown", loc, Fatal);
        return false;
    }

    template <bool Fatal, typename F>
    bool nothrow_check(F&& fn, std::source_location loc) {
        ++checked;
        try {
            fn();
            return true;
        } catch (...) {
            report_exception_fail("nothrow", "unexpected exception thrown", loc, Fatal);
            return false;
        }
    }

    void report_exception_fail(const char* kind, const char* detail, std::source_location loc, bool fatal) {
        const FailureView fv{.test_name = test_name,
                             .loc = loc,
                             .is_fatal = fatal,
                             .kind = kind,
                             .lhs = detail,
                             .rhs = nullptr,
                             .extra = nullptr,
                             .notes = active_notes()};
        fail_cb(fv, fail_ctx);
        record_failure();
    }

#endif // __cpp_exceptions || __EXCEPTIONS

    template <bool Fatal>
    bool string_check(
        bool passed, const char* kind, std::string_view haystack, std::string_view needle, std::source_location loc) {
        ++checked;
        if (passed) {
            return true;
        }
        std::array<char, detail::fail_message_capacity> lhs_buf{};
        std::array<char, detail::fail_message_capacity> rhs_buf{};
        detail::format_value(lhs_buf.data(), lhs_buf.size(), haystack);
        detail::format_value(rhs_buf.data(), rhs_buf.size(), needle);
        const FailureView fv{.test_name = test_name,
                             .loc = loc,
                             .is_fatal = Fatal,
                             .kind = kind,
                             .lhs = lhs_buf.data(),
                             .rhs = rhs_buf.data(),
                             .extra = nullptr,
                             .notes = active_notes()};
        fail_cb(fv, fail_ctx);
        record_failure();
        return false;
    }

    const char* test_name;
    FailCallback fail_cb;
    void* fail_ctx;
    bool failed = false;
    int checked = 0;
    int fail_count = 0;
};

} // namespace umi::test
