#pragma once

// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief ANSI color constants and stdio-free value formatter.
/// @author Shota Moriguchi @tekitounix

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
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
// Stdio-free value formatting helpers
// =============================================================================

namespace detail {

/// @brief Copy a C-string into a buffer with bounds check.
/// @param buf Output buffer.
/// @param size Buffer size.
/// @param s Source C-string.
inline void copy_cstr(char* buf, std::size_t size, const char* s) {
    std::size_t len = std::strlen(s);
    if (len >= size) {
        len = size - 1;
    }
    std::memcpy(buf, s, len);
    buf[len] = '\0';
}

/// @brief Format unsigned integer to decimal string in buffer.
/// @param buf Output buffer.
/// @param size Buffer size.
/// @param v Value to format.
/// @return Pointer into buf where the formatted string starts.
inline const char* format_uint(char* buf, std::size_t size, std::uint64_t v) {
    if (size == 0) {
        return buf;
    }
    if (v == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return buf;
    }
    // Write digits backwards then reverse
    std::size_t pos = 0;
    while (v > 0 && (pos + 1) < size) {
        buf[pos++] = static_cast<char>('0' + (v % 10));
        v /= 10;
    }
    buf[pos] = '\0';
    // Reverse in-place
    for (std::size_t i = 0, j = pos - 1; i < j; ++i, --j) {
        const char tmp = buf[i];
        buf[i] = buf[j];
        buf[j] = tmp;
    }
    return buf;
}

/// @brief Format signed integer to decimal string in buffer.
/// @param buf Output buffer.
/// @param size Buffer size.
/// @param v Value to format.
/// @return Pointer into buf where the formatted string starts.
inline const char* format_int(char* buf, std::size_t size, std::int64_t v) {
    if (size < 2) {
        buf[0] = '\0';
        return buf;
    }
    if (v >= 0) {
        return format_uint(buf, size, static_cast<std::uint64_t>(v));
    }
    buf[0] = '-';
    // Handle INT64_MIN safely: -(v+1) + 1
    auto abs_val = static_cast<std::uint64_t>(-(v + 1)) + 1;
    format_uint(buf + 1, size - 1, abs_val);
    return buf;
}

/// @brief Format double with up to 6 significant digits (%.6g-like behavior).
/// @param buf Output buffer (should be >= 32 bytes).
/// @param size Buffer size.
/// @param v Value to format.
/// @return Pointer into buf.
inline const char* format_double(char* buf, std::size_t size, double v) {
    if (size < 16) {
        buf[0] = '\0';
        return buf;
    }
    std::size_t pos = 0;

    if (v < 0.0) {
        buf[pos++] = '-';
        v = -v;
    }

    auto integer_part = static_cast<std::uint64_t>(v);
    const double frac = v - static_cast<double>(integer_part);

    // Format integer part
    std::array<char, 24> ibuf{};
    format_uint(ibuf.data(), ibuf.size(), integer_part);
    const std::size_t ilen = std::strlen(ibuf.data());
    for (std::size_t i = 0; i < ilen && (pos + 1) < size; ++i) {
        buf[pos++] = ibuf[i];
    }

    // Significant digits: total 6, minus those used by integer part
    constexpr int total_sig = 6;
    const int sig_used = (integer_part == 0) ? 0 : static_cast<int>(ilen);
    const int frac_digits = total_sig - sig_used;

    if (frac_digits <= 0 || frac == 0.0) {
        buf[pos] = '\0';
        return buf;
    }

    buf[pos++] = '.';

    // Generate frac_digits decimal digits with rounding
    double mult = 1.0;
    for (int i = 0; i < frac_digits; ++i) {
        mult *= 10.0;
    }
    auto frac_int = static_cast<std::uint64_t>(std::llround(frac * mult));

    // Format with leading zeros
    std::array<char, 8> fbuf{};
    format_uint(fbuf.data(), fbuf.size(), frac_int);
    const std::size_t flen = std::strlen(fbuf.data());
    // Pad leading zeros
    for (int i = 0; i < frac_digits - static_cast<int>(flen); ++i) {
        if ((pos + 1) < size) {
            buf[pos++] = '0';
        }
    }
    for (std::size_t i = 0; i < flen && (pos + 1) < size; ++i) {
        buf[pos++] = fbuf[i];
    }

    // Trim trailing zeros
    while (pos > 0 && buf[pos - 1] == '0') {
        --pos;
    }
    // Trim trailing decimal point
    if (pos > 0 && buf[pos - 1] == '.') {
        --pos;
    }

    buf[pos] = '\0';
    return buf;
}

/// @brief Format pointer to hexadecimal string.
/// @param buf Output buffer (should be >= 20 bytes).
/// @param size Buffer size.
/// @param v Pointer value.
/// @return Pointer into buf.
inline const char* format_hex(char* buf, std::size_t size, std::uintptr_t v) {
    if (size < 4) {
        buf[0] = '\0';
        return buf;
    }
    buf[0] = '0';
    buf[1] = 'x';
    std::size_t pos = 2;

    if (v == 0) {
        buf[pos++] = '0';
        buf[pos] = '\0';
        return buf;
    }

    // Count hex digits
    std::size_t n = 0;
    auto tmp = v;
    while (tmp > 0) {
        ++n;
        tmp >>= 4;
    }

    if ((pos + n + 1) > size) {
        n = size - pos - 1;
    }

    for (std::size_t i = 0; i < n; ++i) {
        const int nibble = static_cast<int>((v >> ((n - 1 - i) * 4)) & 0xF);
        buf[pos++] = "0123456789abcdef"[nibble];
    }
    buf[pos] = '\0';
    return buf;
}

} // namespace detail

// =============================================================================
// Value formatting (stdio-free, no <iostream>)
// =============================================================================

/// @brief Format a value into a human-readable string without stdio.
/// @tparam T Value type (bool, char, integral, floating-point, enum, pointer).
/// @param buf Output buffer.
/// @param size Buffer size in bytes.
/// @param v Value to format.
/// @note Unknown types produce "(?)".
template <typename T>
void format_value(char* buf, std::size_t size, const T& v) {
    if (size == 0) {
        return;
    }
    if constexpr (std::is_same_v<T, bool>) {
        detail::copy_cstr(buf, size, v ? "true" : "false");
    } else if constexpr (std::is_same_v<T, std::nullptr_t>) {
        detail::copy_cstr(buf, size, "nullptr");
    } else if constexpr (std::is_same_v<T, char>) {
        // "'c' (d)"
        if (size < 8) {
            buf[0] = '\0';
            return;
        }
        std::size_t pos = 0;
        buf[pos++] = '\'';
        buf[pos++] = v;
        buf[pos++] = '\'';
        buf[pos++] = ' ';
        buf[pos++] = '(';
        std::array<char, 8> ibuf{};
        detail::format_int(ibuf.data(), ibuf.size(), static_cast<int>(v));
        const std::size_t ilen = std::strlen(ibuf.data());
        for (std::size_t i = 0; i < ilen && (pos + 1) < size; ++i) {
            buf[pos++] = ibuf[i];
        }
        buf[pos++] = ')';
        buf[pos] = '\0';
    } else if constexpr (std::is_same_v<T, std::string_view>) {
        // "\"str\""
        std::size_t pos = 0;
        buf[pos++] = '"';
        for (std::size_t i = 0; i < v.size() && (pos + 2) < size; ++i) {
            buf[pos++] = v[i];
        }
        buf[pos++] = '"';
        buf[pos] = '\0';
    } else if constexpr (std::is_unsigned_v<T>) {
        detail::format_uint(buf, size, static_cast<std::uint64_t>(v));
    } else if constexpr (std::is_integral_v<T>) {
        detail::format_int(buf, size, static_cast<std::int64_t>(v));
    } else if constexpr (std::is_floating_point_v<T>) {
        detail::format_double(buf, size, static_cast<double>(v));
    } else if constexpr (std::is_enum_v<T>) {
        detail::format_int(buf, size, static_cast<std::int64_t>(static_cast<std::underlying_type_t<T>>(v)));
    } else if constexpr (std::is_pointer_v<T>) {
        detail::format_hex(buf, size, reinterpret_cast<std::uintptr_t>(v));
    } else {
        detail::copy_cstr(buf, size, "(?)");
    }
}

} // namespace umi::test
