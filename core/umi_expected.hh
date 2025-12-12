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
    None = 0,
    
    // Resource errors
    OutOfMemory,        ///< Heap allocation failed
    OutOfTasks,         ///< No free task slots
    OutOfTimers,        ///< No free timer slots
    OutOfBuffers,       ///< No free buffer slots
    
    // State errors
    InvalidTask,        ///< TaskId is invalid or deleted
    InvalidState,       ///< Operation not allowed in current state
    AlreadyRunning,     ///< Already started
    NotRunning,         ///< Not yet started
    
    // Parameter errors
    InvalidParam,       ///< Invalid parameter value
    NullPointer,        ///< Required pointer is null
    BufferTooSmall,     ///< Buffer size insufficient
    
    // Timeout errors
    Timeout,            ///< Operation timed out
    WouldBlock,         ///< Non-blocking operation would block
    
    // Hardware errors
    HardwareFault,      ///< Hardware error detected
    DmaError,           ///< DMA transfer error
    
    // Audio-specific errors
    BufferOverrun,      ///< Audio buffer overrun (DSP too slow)
    BufferUnderrun,     ///< Audio buffer underrun
    SampleRateError,    ///< Unsupported sample rate
    
    // MIDI-specific errors
    MidiParseError,     ///< MIDI message parse error
    MidiBufferFull,     ///< MIDI event buffer full
};

/// Convert error to string (for debugging)
constexpr const char* error_to_string(Error e) {
    switch (e) {
        case Error::None:           return "None";
        case Error::OutOfMemory:    return "OutOfMemory";
        case Error::OutOfTasks:     return "OutOfTasks";
        case Error::OutOfTimers:    return "OutOfTimers";
        case Error::OutOfBuffers:   return "OutOfBuffers";
        case Error::InvalidTask:    return "InvalidTask";
        case Error::InvalidState:   return "InvalidState";
        case Error::AlreadyRunning: return "AlreadyRunning";
        case Error::NotRunning:     return "NotRunning";
        case Error::InvalidParam:   return "InvalidParam";
        case Error::NullPointer:    return "NullPointer";
        case Error::BufferTooSmall: return "BufferTooSmall";
        case Error::Timeout:        return "Timeout";
        case Error::WouldBlock:     return "WouldBlock";
        case Error::HardwareFault:  return "HardwareFault";
        case Error::DmaError:       return "DmaError";
        case Error::BufferOverrun:  return "BufferOverrun";
        case Error::BufferUnderrun: return "BufferUnderrun";
        case Error::SampleRateError: return "SampleRateError";
        case Error::MidiParseError: return "MidiParseError";
        case Error::MidiBufferFull: return "MidiBufferFull";
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
// Helper Macros
// =====================================================================

/// Early return if result is error (like Rust's ? operator)
/// Usage: TRY(kernel.try_create_task(cfg));
#define UMI_TRY(expr) \
    ({ \
        auto&& _result = (expr); \
        if (!_result) return std::unexpected(_result.error()); \
        std::move(*_result); \
    })

/// Early return with custom error type
#define UMI_TRY_OR(expr, err) \
    ({ \
        auto&& _result = (expr); \
        if (!_result) return std::unexpected(err); \
        std::move(*_result); \
    })

// =====================================================================
// Utility Functions
// =====================================================================

/// Check if error is recoverable (can retry)
constexpr bool is_recoverable(Error e) {
    switch (e) {
        case Error::Timeout:
        case Error::WouldBlock:
        case Error::BufferOverrun:
        case Error::BufferUnderrun:
            return true;
        default:
            return false;
    }
}

/// Check if error is fatal (should panic)
constexpr bool is_fatal(Error e) {
    switch (e) {
        case Error::OutOfMemory:
        case Error::HardwareFault:
        case Error::DmaError:
            return true;
        default:
            return false;
    }
}

} // namespace umi
