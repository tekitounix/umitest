#pragma once

// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief BoundedWriter and stdio-free value formatter with constexpr support.
/// @author Shota Moriguchi @tekitounix

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <system_error>
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
        for (const char c : sv) {
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

/// @brief Format double using shortest round-trip decimal form.
inline void format_double(BoundedWriter& w, double v) {
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

    std::array<char, 64> buf{};
    auto* begin = buf.data();
    auto* end = buf.data() + buf.size();
    auto [ptr, ec] = std::to_chars(begin, end, v);
    if (ec != std::errc{}) {
        w.puts("(float)");
        return;
    }

    const std::string_view sv(begin, static_cast<std::size_t>(ptr - begin));
    w.puts(sv);

    // Preserve the existing ".0" style for whole-number finite values.
    if (sv.find_first_of(".eE") == std::string_view::npos) {
        w.put('.');
        w.put('0');
    }
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
        const int nibble = static_cast<int>((v >> (i * 4)) & 0xF);
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
    for (const char c : sv) {
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
    using Raw = std::remove_cvref_t<T>;
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
        auto underlying = std::to_underlying(v);
        if constexpr (std::is_unsigned_v<decltype(underlying)>) {
            detail::format_uint(w, static_cast<std::uint64_t>(underlying));
        } else {
            detail::format_int(w, static_cast<std::int64_t>(underlying));
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
