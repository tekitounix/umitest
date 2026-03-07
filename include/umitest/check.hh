#pragma once

// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief Constexpr bool pure check functions — single source of truth for all comparisons.
/// @author Shota Moriguchi @tekitounix

#include <cmath>
#include <concepts>
#include <string_view>
#include <type_traits>
#include <utility>

namespace umi::test {

namespace detail {

/// @brief Detect char-related pointer types (char*, const char*, char[N], const char[N], char8_t* variants).
/// decay_t captures array types that would otherwise be missed by is_pointer_v.
template <typename T>
constexpr bool is_char_pointer_v =
    std::is_same_v<std::decay_t<T>, char*> || std::is_same_v<std::decay_t<T>, const char*> ||
    std::is_same_v<std::decay_t<T>, char8_t*> || std::is_same_v<std::decay_t<T>, const char8_t*>;

/// @brief Detect char8_t pointer types specifically.
/// char8_t* is unsupported; any side triggers compile error (D42).
template <typename T>
constexpr bool is_char8_pointer_v =
    std::is_same_v<std::decay_t<T>, char8_t*> || std::is_same_v<std::decay_t<T>, const char8_t*>;

/// @brief Template exclusion predicate for check_eq/ne/lt etc.
/// Excludes when: (1) both sides are char-related, or (2) either side is char8_t*.
template <typename A, typename B>
constexpr bool excluded_char_pointer_v =
    (is_char_pointer_v<A> && is_char_pointer_v<B>) || is_char8_pointer_v<A> || is_char8_pointer_v<B>;

/// @brief True if T is a character type (not safe for std::cmp_equal).
template <typename T>
constexpr bool is_char_type_v = std::same_as<T, char> || std::same_as<T, wchar_t> || std::same_as<T, char8_t> ||
                                std::same_as<T, char16_t> || std::same_as<T, char32_t>;

/// @brief True if T is a non-bool, non-char integer (safe for std::cmp_equal).
template <typename T>
concept SafeCmpInteger = std::integral<T> && !std::same_as<T, bool> && !is_char_type_v<T>;

/// @brief Compare values for equality. Mixed-sign integer safe.
template <typename A, typename B>
constexpr bool safe_eq(const A& a, const B& b) {
    if constexpr (SafeCmpInteger<A> && SafeCmpInteger<B>) {
        return std::cmp_equal(a, b);
    } else if constexpr (is_char_pointer_v<A> || is_char_pointer_v<B>) {
        if constexpr (is_char_pointer_v<A>) {
            if constexpr (!std::is_null_pointer_v<B>) {
                if (a == nullptr) {
                    return false;
                }
            }
        }
        if constexpr (is_char_pointer_v<B>) {
            if constexpr (!std::is_null_pointer_v<A>) {
                if (b == nullptr) {
                    return false;
                }
            }
        }
        return a == b;
    } else {
        return a == b;
    }
}

/// @brief Compare C strings by content. constexpr in C++23.
/// @pre a and b are either nullptr or pointers to null-terminated strings.
constexpr bool safe_eq(const char* a, const char* b) {
    if (a == nullptr || b == nullptr) {
        return a == b;
    }
    return std::string_view(a) == std::string_view(b);
}

} // namespace detail

// =============================================================================
// Public check functions — constexpr bool pure functions
// =============================================================================

/// @brief Check boolean condition (true expected). constexpr.
constexpr bool check_true(bool cond) {
    return cond;
}

/// @brief Check boolean condition (false expected). constexpr.
constexpr bool check_false(bool cond) {
    return !cond;
}

/// @brief Check equality. constexpr for constexpr-comparable types.
/// Excludes char pointer types to route them to the non-template overload.
template <typename A, typename B>
    requires(std::equality_comparable_with<A, B> &&
             !detail::excluded_char_pointer_v<std::remove_cvref_t<A>, std::remove_cvref_t<B>>)
constexpr bool check_eq(const A& a, const B& b) {
    return detail::safe_eq(a, b);
}

/// @brief Check equality for C strings. constexpr content comparison.
constexpr bool check_eq(const char* a, const char* b) {
    return detail::safe_eq(a, b);
}

/// @brief Check inequality.
template <typename A, typename B>
    requires(std::equality_comparable_with<A, B> &&
             !detail::excluded_char_pointer_v<std::remove_cvref_t<A>, std::remove_cvref_t<B>>)
constexpr bool check_ne(const A& a, const B& b) {
    return !detail::safe_eq(a, b);
}

/// @brief Check inequality for C strings.
constexpr bool check_ne(const char* a, const char* b) {
    return !detail::safe_eq(a, b);
}

/// @brief Check less-than. Uses std::cmp_less for mixed-sign integer safety.
/// Pointers excluded via decay_t to prevent unspecified ordering comparison.
template <typename A, typename B>
    requires(std::totally_ordered_with<A, B> && !std::is_pointer_v<std::decay_t<A>> &&
             !std::is_pointer_v<std::decay_t<B>>)
constexpr bool check_lt(const A& a, const B& b) {
    if constexpr (detail::SafeCmpInteger<A> && detail::SafeCmpInteger<B>) {
        return std::cmp_less(a, b);
    } else {
        return a < b;
    }
}

/// @brief Check less-or-equal.
template <typename A, typename B>
    requires(std::totally_ordered_with<A, B> && !std::is_pointer_v<std::decay_t<A>> &&
             !std::is_pointer_v<std::decay_t<B>>)
constexpr bool check_le(const A& a, const B& b) {
    if constexpr (detail::SafeCmpInteger<A> && detail::SafeCmpInteger<B>) {
        return std::cmp_less_equal(a, b);
    } else {
        return a <= b;
    }
}

/// @brief Check greater-than.
template <typename A, typename B>
    requires(std::totally_ordered_with<A, B> && !std::is_pointer_v<std::decay_t<A>> &&
             !std::is_pointer_v<std::decay_t<B>>)
constexpr bool check_gt(const A& a, const B& b) {
    if constexpr (detail::SafeCmpInteger<A> && detail::SafeCmpInteger<B>) {
        return std::cmp_greater(a, b);
    } else {
        return a > b;
    }
}

/// @brief Check greater-or-equal.
template <typename A, typename B>
    requires(std::totally_ordered_with<A, B> && !std::is_pointer_v<std::decay_t<A>> &&
             !std::is_pointer_v<std::decay_t<B>>)
constexpr bool check_ge(const A& a, const B& b) {
    if constexpr (detail::SafeCmpInteger<A> && detail::SafeCmpInteger<B>) {
        return std::cmp_greater_equal(a, b);
    } else {
        return a >= b;
    }
}

/// @brief Check approximate equality. Floating-point only.
/// @pre eps >= 0. Negative eps unconditionally returns false.
template <std::floating_point A, std::floating_point B>
bool check_near(const A& a, const B& b, std::common_type_t<A, B> eps = static_cast<std::common_type_t<A, B>>(0.001)) {
    using C = std::common_type_t<A, B>;
    auto ca = static_cast<C>(a);
    auto cb = static_cast<C>(b);
    if (std::isnan(ca) || std::isnan(cb) || std::isnan(eps)) {
        return false;
    }
    if (eps < C{0}) {
        return false;
    }
    if (ca == cb) {
        return true;
    }
    return std::abs(ca - cb) <= eps;
}

} // namespace umi::test
