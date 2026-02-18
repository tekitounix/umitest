#pragma once

// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief ANSI color constants and snprintf-based value formatter.
/// @author Shota Moriguchi @tekitounix

#include <cstddef>
#include <cstdio>
#include <string_view>
#include <type_traits>

namespace umi::test {

// =============================================================================
// ANSI colors
// =============================================================================

/// @name ANSI color constants
/// @brief Terminal color escape codes. Disabled when UMI_TEST_NO_COLOR is defined.
/// @{
#ifndef UMI_TEST_NO_COLOR
constexpr const char* green = "\033[32m";
constexpr const char* red = "\033[31m";
constexpr const char* cyan = "\033[36m";
constexpr const char* reset = "\033[0m";
#else
constexpr const char* green = "";
constexpr const char* red = "";
constexpr const char* cyan = "";
constexpr const char* reset = "";
#endif
/// @}

// =============================================================================
// Value formatting (snprintf-based, no <iostream>)
// =============================================================================

/// @brief Format a value into a human-readable string using snprintf.
/// @tparam T Value type (bool, char, integral, floating-point, enum, pointer).
/// @param buf Output buffer.
/// @param size Buffer size in bytes.
/// @param v Value to format.
/// @note Unknown types produce "(?)".
template <typename T>
void format_value(char* buf, std::size_t size, const T& v) {
    if constexpr (std::is_same_v<T, bool>) {
        std::snprintf(buf, size, "%s", v ? "true" : "false");
    } else if constexpr (std::is_same_v<T, std::nullptr_t>) {
        std::snprintf(buf, size, "nullptr");
    } else if constexpr (std::is_same_v<T, char>) {
        std::snprintf(buf, size, "'%c' (%d)", v, static_cast<int>(v));
    } else if constexpr (std::is_same_v<T, std::string_view>) {
        std::snprintf(buf, size, "\"%.*s\"", static_cast<int>(v.size()), v.data());
    } else if constexpr (std::is_unsigned_v<T>) {
        std::snprintf(buf, size, "%llu", static_cast<unsigned long long>(v));
    } else if constexpr (std::is_integral_v<T>) {
        std::snprintf(buf, size, "%lld", static_cast<long long>(v));
    } else if constexpr (std::is_floating_point_v<T>) {
        std::snprintf(buf, size, "%.6g", static_cast<double>(v));
    } else if constexpr (std::is_enum_v<T>) {
        std::snprintf(buf, size, "%lld", static_cast<long long>(static_cast<std::underlying_type_t<T>>(v)));
    } else if constexpr (std::is_pointer_v<T>) {
        std::snprintf(buf, size, "%p", static_cast<const void*>(v));
    } else {
        std::snprintf(buf, size, "(?)");
    }
}

} // namespace umi::test
