// SPDX-License-Identifier: MIT
// UMI-OS MIDI Library - Result Type
#pragma once

#include <cstdint>
#include <expected>
#include <string_view>

namespace umidi {

// Error codes for MIDI operations
enum class ErrorCode : uint8_t {
    OK,
    INCOMPLETE_MESSAGE,
    INVALID_STATUS,
    BUFFER_OVERFLOW,
    INVALID_DATA,
    INVALID_MESSAGE_TYPE,
    CHANNEL_FILTERED,
    NOT_IMPLEMENTED,
    NOT_SUPPORTED
};

// Lightweight error type (no heap allocation)
struct Error {
    ErrorCode code = ErrorCode::INCOMPLETE_MESSAGE;
    uint8_t context = 0;  // Optional context byte

    constexpr Error() noexcept = default;
    constexpr Error(ErrorCode c) noexcept : code(c), context(0) {}
    constexpr Error(ErrorCode c, uint8_t ctx) noexcept : code(c), context(ctx) {}

    // Factory methods
    static constexpr Error incomplete() noexcept {
        return {ErrorCode::INCOMPLETE_MESSAGE};
    }

    static constexpr Error incomplete(uint8_t byte) noexcept {
        return {ErrorCode::INCOMPLETE_MESSAGE, byte};
    }

    static constexpr Error invalid_status(uint8_t status) noexcept {
        return {ErrorCode::INVALID_STATUS, status};
    }

    static constexpr Error buffer_overflow() noexcept {
        return {ErrorCode::BUFFER_OVERFLOW};
    }

    static constexpr Error invalid_data(uint8_t data) noexcept {
        return {ErrorCode::INVALID_DATA, data};
    }

    static constexpr Error channel_filtered(uint8_t ch) noexcept {
        return {ErrorCode::CHANNEL_FILTERED, ch};
    }

    static constexpr Error not_implemented() noexcept {
        return {ErrorCode::NOT_IMPLEMENTED};
    }
};

// Result type using std::expected
template <typename T>
using Result = std::expected<T, Error>;

// =============================================================================
// Helper functions for Result construction
// =============================================================================

/// Create a success result
template <typename T>
[[nodiscard]] constexpr Result<T> Ok(T value) noexcept {
    return Result<T>(std::move(value));
}

/// Create an error result
template <typename T = void>
[[nodiscard]] constexpr std::unexpected<Error> Err(Error error) noexcept {
    return std::unexpected<Error>(error);
}

/// Create an error result from ErrorCode
template <typename T = void>
[[nodiscard]] constexpr std::unexpected<Error> Err(ErrorCode code) noexcept {
    return std::unexpected<Error>(Error(code));
}

} // namespace umidi
