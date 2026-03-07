#pragma once

// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief BoundedWriter and stdio-free value formatter with constexpr support.
/// @author Shota Moriguchi @tekitounix

#include <array>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <type_traits>

namespace umi::test {

// =============================================================================
// BoundedWriter — safe buffer writer
// =============================================================================

/// @brief Safe buffer writer that never produces UB regardless of buffer size.
/// @details Guarantees:
///   - size==0: no writes, no access to buf
///   - size==1: writes only NUL terminator
///   - size>=2: normal operation with NUL always maintained
///   - truncation never causes UB; detectable via truncated()
class BoundedWriter {
  public:
    /// @pre buf != nullptr || size == 0
    constexpr BoundedWriter(char* buf, std::size_t size) : data(buf), cap(size > 0 ? size - 1 : 0), valid(size > 0) {
        if (valid) {
            data[0] = '\0';
        }
    }

    constexpr void put(char c) {
        if (!valid) {
            return;
        }
        if (pos < cap) {
            data[pos] = c;
        }
        ++pos;
        data[std::min(pos, cap)] = '\0';
    }

    constexpr void puts(const char* s) {
        while (*s != '\0') {
            put(*s++);
        }
    }

    constexpr void puts(std::string_view sv) {
        for (char c : sv) {
            put(c);
        }
    }

    [[nodiscard]] constexpr std::size_t written() const { return pos; }
    [[nodiscard]] constexpr bool truncated() const { return pos > cap; }

  private:
    char* data;
    std::size_t cap;
    std::size_t pos = 0;
    bool valid;
};

// =============================================================================
// Detail formatting helpers
// =============================================================================

namespace detail {

/// @brief Failure message buffer capacity.
constexpr std::size_t fail_message_capacity = 256;

/// @brief Format unsigned integer to decimal.
constexpr void format_uint(BoundedWriter& w, std::uint64_t v) {
    if (v == 0) {
        w.put('0');
        return;
    }
    std::array<char, 20> tmp{};
    int n = 0;
    while (v > 0) {
        tmp[static_cast<std::size_t>(n++)] = static_cast<char>('0' + (v % 10));
        v /= 10;
    }
    for (int i = n - 1; i >= 0; --i) {
        w.put(tmp[static_cast<std::size_t>(i)]);
    }
}

/// @brief Format signed integer to decimal.
constexpr void format_int(BoundedWriter& w, std::int64_t v) {
    if (v >= 0) {
        format_uint(w, static_cast<std::uint64_t>(v));
        return;
    }
    w.put('-');
    auto abs_val = static_cast<std::uint64_t>(-(v + 1)) + 1;
    format_uint(w, abs_val);
}

/// @brief Reverse chars in-place within an array range [0, len).
constexpr void reverse_chars(char* buf, int len) {
    for (int i = 0, j = len - 1; i < j; ++i, --j) {
        char t = buf[i];
        buf[i] = buf[j];
        buf[j] = t;
    }
}

/// @brief Format the fractional part of a double.
constexpr void format_frac_part(BoundedWriter& w, double frac, int frac_digits) {
    double mult = 1.0;
    for (int i = 0; i < frac_digits; ++i) {
        mult *= 10.0;
    }
    auto frac_int = static_cast<std::uint64_t>(std::llround(frac * mult));

    std::array<char, 8> fbuf{};
    int flen = 0;
    if (frac_int == 0) {
        fbuf[0] = '0';
        flen = 1;
    } else {
        std::uint64_t tmp = frac_int;
        while (tmp > 0) {
            fbuf[static_cast<std::size_t>(flen++)] = static_cast<char>('0' + (tmp % 10));
            tmp /= 10;
        }
        reverse_chars(fbuf.data(), flen);
    }

    for (int i = 0; i < frac_digits - flen; ++i) {
        w.put('0');
    }
    int last_nonzero = flen - 1;
    while (last_nonzero > 0 && fbuf[static_cast<std::size_t>(last_nonzero)] == '0') {
        --last_nonzero;
    }
    for (int i = 0; i <= last_nonzero; ++i) {
        w.put(fbuf[static_cast<std::size_t>(i)]);
    }
}

/// @brief Format the normal (non-special) part of a double.
constexpr void format_double_normal(BoundedWriter& w, double v) {
    auto integer_part = static_cast<std::uint64_t>(v);
    double frac = v - static_cast<double>(integer_part);

    std::array<char, 24> ibuf{};
    int ilen = 0;
    if (integer_part == 0) {
        ibuf[0] = '0';
        ilen = 1;
    } else {
        std::uint64_t tmp = integer_part;
        while (tmp > 0) {
            ibuf[static_cast<std::size_t>(ilen++)] = static_cast<char>('0' + (tmp % 10));
            tmp /= 10;
        }
        reverse_chars(ibuf.data(), ilen);
    }
    for (int i = 0; i < ilen; ++i) {
        w.put(ibuf[static_cast<std::size_t>(i)]);
    }

    constexpr int total_sig = 6;
    int sig_used = (integer_part == 0) ? 0 : ilen;
    int frac_digits = total_sig - sig_used;

    if (frac_digits <= 0 || frac == 0.0) {
        w.put('.');
        w.put('0');
        return;
    }

    w.put('.');
    format_frac_part(w, frac, frac_digits);
}

/// @brief Format double with up to 6 significant digits.
constexpr void format_double(BoundedWriter& w, double v) {
    if (std::isnan(v)) {
        w.puts("nan");
        return;
    }
    if (std::isinf(v)) {
        if (v < 0) {
            w.put('-');
        }
        w.puts("inf");
        return;
    }
    if (v == 0.0 && std::signbit(v)) {
        w.puts("-0.0");
        return;
    }

    if (v < 0.0) {
        w.put('-');
        v = -v;
    }

    if (v >= 1e19) {
        int exp = 0;
        double scaled = v;
        while (scaled >= 10.0) {
            scaled /= 10.0;
            ++exp;
        }
        format_double_normal(w, scaled);
        w.put('e');
        format_uint(w, static_cast<std::uint64_t>(exp));
        return;
    }

    format_double_normal(w, v);
}

/// @brief Format pointer as hex.
constexpr void format_hex(BoundedWriter& w, std::uintptr_t v) {
    w.put('0');
    w.put('x');
    if (v == 0) {
        w.put('0');
        return;
    }
    int n = 0;
    auto tmp = v;
    while (tmp > 0) {
        ++n;
        tmp >>= 4;
    }
    for (int i = n - 1; i >= 0; --i) {
        int nibble = static_cast<int>((v >> (i * 4)) & 0xF);
        w.put("0123456789abcdef"[nibble]);
    }
}

/// @brief Write escaped representation of a char.
constexpr void escape_char(BoundedWriter& w, char v) {
    if (v == '\0') {
        w.put('\\');
        w.put('0');
    } else if (v == '\n') {
        w.put('\\');
        w.put('n');
    } else if (v == '\r') {
        w.put('\\');
        w.put('r');
    } else if (v == '\t') {
        w.put('\\');
        w.put('t');
    } else if (v == '\\') {
        w.put('\\');
        w.put('\\');
    } else if (v == '\'') {
        w.put('\\');
        w.put('\'');
    } else if (v == '"') {
        w.put('\\');
        w.put('"');
    } else if (static_cast<unsigned char>(v) < 0x20 || v == 0x7F) {
        w.put('\\');
        w.put('x');
        w.put("0123456789abcdef"[(static_cast<unsigned char>(v) >> 4) & 0xF]);
        w.put("0123456789abcdef"[static_cast<unsigned char>(v) & 0xF]);
    } else {
        w.put(v);
    }
}

/// @brief Write a quoted and escaped string from string_view.
constexpr void format_string(BoundedWriter& w, std::string_view sv) {
    w.put('"');
    for (char c : sv) {
        escape_char(w, c);
    }
    w.put('"');
}

} // namespace detail

// =============================================================================
// Value formatting (stdio-free, constexpr where possible)
// =============================================================================

/// @brief Format a value into a human-readable string.
/// @details constexpr for bool, integers, const char*, nullptr_t.
///          Non-constexpr for floating-point, pointers, std::string, std::string_view.
/// @pre size > 0
/// @post buf is null-terminated.
template <typename T>
constexpr void format_value(BoundedWriter& w, const T& v) {
    using Raw = std::remove_cv_t<std::remove_reference_t<T>>;
    if constexpr (std::is_same_v<Raw, bool>) {
        w.puts(v ? "true" : "false");
    } else if constexpr (std::is_same_v<Raw, std::nullptr_t>) {
        w.puts("nullptr");
    } else if constexpr (std::is_same_v<Raw, char>) {
        w.put('\'');
        detail::escape_char(w, v);
        w.put('\'');
        w.put(' ');
        w.put('(');
        detail::format_int(w, static_cast<int>(v));
        w.put(')');
    } else if constexpr (std::is_same_v<Raw, std::string>) {
        detail::format_string(w, std::string_view(v.data(), v.size()));
    } else if constexpr (std::is_same_v<Raw, std::string_view>) {
        detail::format_string(w, v);
    } else if constexpr (std::is_same_v<std::decay_t<Raw>, const char*> || std::is_same_v<std::decay_t<Raw>, char*>) {
        if (v == nullptr) {
            w.puts("(null)");
            return;
        }
        detail::format_string(w, std::string_view(v));
    } else if constexpr (std::is_array_v<Raw> && (std::is_same_v<std::remove_extent_t<Raw>, char> ||
                                                  std::is_same_v<std::remove_extent_t<Raw>, const char>)) {
        detail::format_string(w, std::string_view(v));
    } else if constexpr (std::is_unsigned_v<Raw>) {
        detail::format_uint(w, static_cast<std::uint64_t>(v));
    } else if constexpr (std::is_integral_v<Raw>) {
        detail::format_int(w, static_cast<std::int64_t>(v));
    } else if constexpr (std::is_floating_point_v<Raw>) {
        detail::format_double(w, static_cast<double>(v));
    } else if constexpr (std::is_enum_v<Raw>) {
        using U = std::underlying_type_t<Raw>;
        if constexpr (std::is_unsigned_v<U>) {
            detail::format_uint(w, static_cast<std::uint64_t>(static_cast<U>(v)));
        } else {
            detail::format_int(w, static_cast<std::int64_t>(static_cast<U>(v)));
        }
    } else if constexpr (std::is_pointer_v<Raw>) {
        detail::format_hex(w, reinterpret_cast<std::uintptr_t>(v));
    } else {
        w.puts("(?)");
    }
}

namespace detail {

/// @brief Format a value into a fixed buffer.
/// @pre size > 0
/// @post buf is null-terminated.
template <typename T>
constexpr void format_value(char* buf, std::size_t size, const T& value) {
    BoundedWriter w(buf, size);
    umi::test::format_value(w, value);
}

/// @brief Format near-comparison extra info: "eps=..., diff=..."
template <std::floating_point A, std::floating_point B>
void format_near_extra(char* buf, std::size_t size, const A& a, const B& b, std::common_type_t<A, B> eps) {
    using C = std::common_type_t<A, B>;
    auto diff = std::abs(static_cast<C>(a) - static_cast<C>(b));
    BoundedWriter w(buf, size);
    w.puts("eps=");
    format_double(w, static_cast<double>(eps));
    w.puts(", diff=");
    format_double(w, static_cast<double>(diff));
}

} // namespace detail

} // namespace umi::test
