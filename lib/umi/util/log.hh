// SPDX-License-Identifier: MIT
// UMI-OS - Layer-specific logging

#pragma once

#include <cstdint>

// Thread-safe globals on platforms with atomic support
#if __has_include(<atomic>) && !defined(UMI_NO_ATOMIC)
    #include <atomic>
    #define UMI_ATOMIC_PTR(T)      std::atomic<T>
    #define UMI_ATOMIC_LOAD(x)     (x).load(std::memory_order_relaxed)
    #define UMI_ATOMIC_STORE(x, v) (x).store(v, std::memory_order_relaxed)
#else
    #define UMI_ATOMIC_PTR(T)      T
    #define UMI_ATOMIC_LOAD(x)     (x)
    #define UMI_ATOMIC_STORE(x, v) ((x) = (v))
#endif

namespace umi {

// ============================================================================
// Log Levels
// ============================================================================

enum class LogLevel : uint8_t {
    TRACE = 0, ///< Verbose debug (DSP sample values, etc.)
    DEBUG = 1, ///< Debug information
    INFO = 2,  ///< Normal operation info
    WARN = 3,  ///< Warnings (recoverable)
    ERROR = 4, ///< Errors (may affect operation)
    FATAL = 5, ///< Fatal errors (system halt)
    OFF = 6,   ///< Logging disabled
};

// ============================================================================
// Log Handler
// ============================================================================

/// Log handler function type
using LogHandler = void (*)(LogLevel level, const char* tag, const char* msg);

/// Default handler (no-op for embedded, can be replaced)
inline void default_log_handler(LogLevel level, const char* tag, const char* msg) {
    (void)level;
    (void)tag;
    (void)msg;
}

/// Global log handler (thread-safe on supported platforms)
inline UMI_ATOMIC_PTR(LogHandler) g_log_handler{default_log_handler};

/// Global minimum log level (thread-safe on supported platforms)
inline UMI_ATOMIC_PTR(LogLevel) g_log_level{LogLevel::INFO};

/// Set log handler
inline void set_log_handler(LogHandler handler) {
    UMI_ATOMIC_STORE(g_log_handler, handler);
}

/// Set minimum log level
inline void set_log_level(LogLevel level) {
    UMI_ATOMIC_STORE(g_log_level, level);
}

// ============================================================================
// Log Functions
// ============================================================================

namespace util {

inline void log(LogLevel level, const char* tag, const char* msg) {
    auto handler = UMI_ATOMIC_LOAD(g_log_handler);
    auto min_level = UMI_ATOMIC_LOAD(g_log_level);
    if (level >= min_level && handler) {
        handler(level, tag, msg);
    }
}

} // namespace util

// ============================================================================
// Log Macros
// ============================================================================

#if defined(NDEBUG) || defined(UMI_RELEASE) || defined(UMI_LOG_MINIMAL)
  // Release: minimal logging (warn and above)
    #define UMI_LOG_TRACE(tag, msg) ((void)0)
    #define UMI_LOG_DEBUG(tag, msg) ((void)0)
    #define UMI_LOG_INFO(tag, msg)  ((void)0)
    #define UMI_LOG_WARN(tag, msg)  ::umi::util::log(::umi::LogLevel::WARN, tag, msg)
    #define UMI_LOG_ERROR(tag, msg) ::umi::util::log(::umi::LogLevel::ERROR, tag, msg)
    #define UMI_LOG_FATAL(tag, msg) ::umi::util::log(::umi::LogLevel::FATAL, tag, msg)
#else
  // Debug: full logging
    #define UMI_LOG_TRACE(tag, msg) ::umi::util::log(::umi::LogLevel::TRACE, tag, msg)
    #define UMI_LOG_DEBUG(tag, msg) ::umi::util::log(::umi::LogLevel::DEBUG, tag, msg)
    #define UMI_LOG_INFO(tag, msg)  ::umi::util::log(::umi::LogLevel::INFO, tag, msg)
    #define UMI_LOG_WARN(tag, msg)  ::umi::util::log(::umi::LogLevel::WARN, tag, msg)
    #define UMI_LOG_ERROR(tag, msg) ::umi::util::log(::umi::LogLevel::ERROR, tag, msg)
    #define UMI_LOG_FATAL(tag, msg) ::umi::util::log(::umi::LogLevel::FATAL, tag, msg)
#endif

// ============================================================================
// Layer-specific Log Tags (convention)
// ============================================================================

// Use these as tag arguments:
//   UMI_LOG_INFO("dsp", "processing buffer");
//   UMI_LOG_WARN("kernel", "task overrun");
//   UMI_LOG_ERROR("midi", "invalid message");

} // namespace umi
