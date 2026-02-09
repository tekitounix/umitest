// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief Minimal printf implementation for embedded systems.
/// @author Shota Moriguchi @tekitounix
///
/// Configurable via PrintConfig template: field width, precision, float,
/// binary format, etc. Supports snprintf, vsnprintf, printf, and
/// callback-based output.
#pragma once

#include <array>
#include <cstdarg>

#include "detail/write.hh"

#include "printf_convert.hh"

namespace rt {

/// @brief Format into a buffer (vsnprintf-style).
/// @tparam Config PrintConfig controlling enabled features.
/// @param buffer Destination buffer.
/// @param bufsz  Buffer size (includes NUL terminator).
/// @param format printf-compatible format string.
/// @param args   Variadic argument list.
/// @return Number of characters that would have been written.
template <typename Config = DefaultConfig>
[[gnu::format(printf, 3, 0)]]
int vsnprintf(char* buffer, std::size_t bufsz, const char* format, va_list args);

/// @brief Format into a buffer (snprintf-style, variadic).
/// @tparam Config PrintConfig controlling enabled features.
/// @param buffer Destination buffer.
/// @param bufsz  Buffer size (includes NUL terminator).
/// @param format printf-compatible format string.
/// @return Number of characters that would have been written.
template <typename Config = DefaultConfig>
[[gnu::format(printf, 3, 4)]]
int snprintf(char* buffer, std::size_t bufsz, const char* format, ...);

namespace detail {
template <typename Config = DefaultConfig>
[[gnu::format(printf, 3, 0)]]
int vpprintf(detail::PutcFunc pc, void* pc_ctx, const char* format, va_list args);

template <typename Config = DefaultConfig>
[[gnu::format(printf, 3, 4)]]
int pprintf(detail::PutcFunc pc, void* pc_ctx, const char* format, ...);
} // namespace detail

/// @brief Core formatting engine: iterate format string, dispatch each specifier.
/// @tparam Config PrintConfig.
/// @param pc     Character output callback.
/// @param pc_ctx Opaque context for pc.
/// @param format printf-compatible format string.
/// @param args   Variadic argument list.
/// @return Total number of characters emitted.
namespace detail {
template <typename Config>
[[gnu::noinline]]
int vpprintf_impl(PutcFunc pc, void* pc_ctx, const char* format, va_list args) {
    int n = 0;
    FormatSpec<Config> spec;

    while (*format) {
        if (*format != '%') {
            pc(*format++, pc_ctx);
            ++n;
            continue;
        }

        // Parse format specifier
        int const fs_len = parse_format_spec<Config>(format, spec);
        if (fs_len == 0) {
            pc(*format++, pc_ctx);
            ++n;
            continue;
        }

        format += fs_len;

        // Process based on Conversion type
        std::array<char, conversion_buffer_size> buf_array;
        char* buf = buf_array.data();
        const char* s = nullptr;
        int slen = 0;

        switch (spec.conv) {
        case Conversion::PERCENT:
            pc('%', pc_ctx);
            ++n;
            continue;

        case Conversion::CHARACTER: {
            int const c = va_arg(args, int);
            pc(c, pc_ctx);
            ++n;
            continue;
        }

        case Conversion::STRING: {
            s = va_arg(args, const char*);
            if (s == nullptr) {
                s = "(null)";
            }
            slen = static_cast<int>(std::strlen(s));

            // Apply precision for strings
            if constexpr (Config::use_precision) {
                if (spec.precision.opt == FormatOption::LITERAL && spec.precision.value >= 0) {
                    slen = min(slen, spec.precision.value);
                }
            }
            break;
        }

        case Conversion::SIGNED_INT:
        case Conversion::UNSIGNED_INT:
        case Conversion::OCTAL:
        case Conversion::HEX:
        case Conversion::BINARY:
        case Conversion::POINTER: {
            handle_integer_conversion<Config>(spec, args, buf, slen);
            s = buf;
            break;
        }

        case Conversion::FLOAT_DEC:
        case Conversion::FLOAT_SCI:
        case Conversion::FLOAT_SHORTEST:
        case Conversion::FLOAT_HEX:
            handle_float_conversion<Config>(spec, args, buf, slen);
            s = buf;
            break;

        case Conversion::WRITEBACK:
            if constexpr (Config::use_writeback) {
                int* p = va_arg(args, int*);
                if (p != nullptr) {
                    *p = n;
                }
            }
            continue;

        case Conversion::NONE:
        default:
            // Unsupported Conversion
            continue;
        }

        // Apply field width and padding
        apply_field_width<Config>(spec, pc, pc_ctx, s, slen, n);
    }

    return n;
}
} // namespace detail

// Public API implementations
template <typename Config>
int vsnprintf(char* buffer, std::size_t bufsz, const char* format, va_list args) {
    detail::BufferContext ctx{.dst = buffer, .len = bufsz, .cur = 0};
    int n = detail::vpprintf_impl<Config>(bufputc, &ctx, format, args);
    if (ctx.len > 0) {
        std::size_t const terminator_idx = min(ctx.cur, ctx.len - 1);
        ctx.dst[terminator_idx] = '\0';
    }
    return n;
}

template <typename Config>
int snprintf(char* buffer, std::size_t bufsz, const char* format, ...) {
    va_list args;
    va_start(args, format);
    int n = vsnprintf<Config>(buffer, bufsz, format, args);
    va_end(args);
    return n;
}

/// @brief Format and write to stdout.
/// @return Number of characters written.
inline int vprintf(const char* format, va_list args) {
    auto pc = [](int c, void*) {
        auto byte = static_cast<std::byte>(static_cast<unsigned char>(c));
        umi::rt::detail::write_bytes(std::span{&byte, 1});
    };
    return detail::vpprintf_impl<DefaultConfig>(pc, nullptr, format, args);
}

/// @brief Format and write to stdout (variadic).
inline int printf(const char* format, ...) {
    va_list args;
    va_start(args, format);
    int const n = vprintf(format, args);
    va_end(args);
    return n;
}

} // namespace rt
