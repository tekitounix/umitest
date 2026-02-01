// =====================================================================
// UMI-OS Error Handling with std::expected
// =====================================================================
//
// C++23 std::expected<T, E> wrapper and common error types.
// Provides Rust-like Result<T, E> pattern for embedded systems.
//
// Usage:
//   Expected<TaskId, Error> result = kernel.try_create_task(cfg);
//   if (result) {
//       auto id = *result;
//   } else {
//       handle_error(result.error());
//   }
//
// =====================================================================

#pragma once

#include <expected>
#include <cstdint>

namespace umi {

// =====================================================================
// Common Error Types
// =====================================================================

/// System-wide error codes
enum class Error : std::uint8_t {
    NONE = 0,

    // Resource errors
    OUT_OF_MEMORY,      ///< Heap allocation failed
    OUT_OF_TASKS,       ///< No free task slots
    OUT_OF_TIMERS,      ///< No free timer slots
    OUT_OF_BUFFERS,     ///< No free buffer slots

    // State errors
    INVALID_TASK,       ///< TaskId is invalid or deleted
    INVALID_STATE,      ///< Operation not allowed in current state
    ALREADY_RUNNING,    ///< Already started
    NOT_RUNNING,        ///< Not yet started

    // Parameter errors
    INVALID_PARAM,      ///< Invalid parameter value
    NULL_POINTER,       ///< Required pointer is null
    BUFFER_TOO_SMALL,   ///< Buffer size insufficient

    // Timeout errors
    TIMEOUT,            ///< Operation timed out
    WOULD_BLOCK,        ///< Non-blocking operation would block

    // Hardware errors
    HARDWARE_FAULT,     ///< Hardware error detected
    DMA_ERROR,          ///< DMA transfer error

    // Audio-specific errors
    BUFFER_OVERRUN,     ///< Audio buffer overrun (DSP too slow)
    BUFFER_UNDERRUN,    ///< Audio buffer underrun
    SAMPLE_RATE_ERROR,  ///< Unsupported sample rate

    // MIDI-specific errors
    MIDI_PARSE_ERROR,   ///< MIDI message parse error
    MIDI_BUFFER_FULL,   ///< MIDI event buffer full
};

/// Convert error to string (for debugging)
constexpr const char* error_to_string(Error e) {
    switch (e) {
        case Error::NONE:             return "None";
        case Error::OUT_OF_MEMORY:    return "OutOfMemory";
        case Error::OUT_OF_TASKS:     return "OutOfTasks";
        case Error::OUT_OF_TIMERS:    return "OutOfTimers";
        case Error::OUT_OF_BUFFERS:   return "OutOfBuffers";
        case Error::INVALID_TASK:     return "InvalidTask";
        case Error::INVALID_STATE:    return "InvalidState";
        case Error::ALREADY_RUNNING:  return "AlreadyRunning";
        case Error::NOT_RUNNING:      return "NotRunning";
        case Error::INVALID_PARAM:    return "InvalidParam";
        case Error::NULL_POINTER:     return "NullPointer";
        case Error::BUFFER_TOO_SMALL: return "BufferTooSmall";
        case Error::TIMEOUT:          return "Timeout";
        case Error::WOULD_BLOCK:      return "WouldBlock";
        case Error::HARDWARE_FAULT:   return "HardwareFault";
        case Error::DMA_ERROR:        return "DmaError";
        case Error::BUFFER_OVERRUN:   return "BufferOverrun";
        case Error::BUFFER_UNDERRUN:  return "BufferUnderrun";
        case Error::SAMPLE_RATE_ERROR: return "SampleRateError";
        case Error::MIDI_PARSE_ERROR: return "MidiParseError";
        case Error::MIDI_BUFFER_FULL: return "MidiBufferFull";
        default:                    return "Unknown";
    }
}

// =====================================================================
// Result Aliases (Rust-style naming)
// =====================================================================

/// Result type alias (Rust-style naming)
template<typename T, typename E = Error>
using Result = std::expected<T, E>;

/// Void result (for operations that don't return a value)
template<typename E = Error>
using ResultVoid = std::expected<void, E>;

/// Legacy alias for compatibility
template<typename T, typename E = Error>
using Expected = Result<T, E>;

/// Shorthand for success
template<typename T>
constexpr auto Ok(T&& value) {
    return Result<std::remove_cvref_t<T>>{std::forward<T>(value)};
}

/// Shorthand for error
template<typename E = Error>
constexpr auto Err(E error) {
    return std::unexpected(error);
}

// =====================================================================
// Utility Functions
// =====================================================================

/// Check if error is recoverable (can retry)
constexpr bool is_recoverable(Error e) {
    switch (e) {
        case Error::TIMEOUT:
        case Error::WOULD_BLOCK:
        case Error::BUFFER_OVERRUN:
        case Error::BUFFER_UNDERRUN:
            return true;
        default:
            return false;
    }
}

/// Check if error is fatal (should panic)
constexpr bool is_fatal(Error e) {
    switch (e) {
        case Error::OUT_OF_MEMORY:
        case Error::HARDWARE_FAULT:
        case Error::DMA_ERROR:
            return true;
        default:
            return false;
    }
}

} // namespace umi
