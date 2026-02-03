// SPDX-License-Identifier: MIT
// UMI-OS - Layer-specific assertion policies

#pragma once

#include <cstdint>

// Thread-safe globals on platforms with atomic support
#if __has_include(<atomic>) && !defined(UMI_NO_ATOMIC)
#include <atomic>
#define UMI_ATOMIC_ASSERT_PTR(T) std::atomic<T>
#define UMI_ATOMIC_ASSERT_LOAD(x) (x).load(std::memory_order_relaxed)
#define UMI_ATOMIC_ASSERT_STORE(x, v) (x).store(v, std::memory_order_relaxed)
#else
#define UMI_ATOMIC_ASSERT_PTR(T) T
#define UMI_ATOMIC_ASSERT_LOAD(x) (x)
#define UMI_ATOMIC_ASSERT_STORE(x, v) ((x) = (v))
#endif

namespace umi {

// ============================================================================
// Assert Policies
// ============================================================================
//
// Different layers have different assert behaviors:
//   - DSP: fast-fail (infinite loop in debug, no-op in release)
//   - Application: recoverable (return error, continue)
//   - Kernel: system halt with diagnostics
//
// ============================================================================

/// Assert action to take on failure
enum class AssertAction : uint8_t {
    IGNORE,     ///< Continue execution (dangerous, release only)
    LOG,        ///< Log and continue
    TRAP,       ///< Breakpoint/trap (debug)
    HALT,       ///< System halt with diagnostics
};

/// Assert handler function type
using AssertHandler = void (*)(const char* file, int line, const char* expr);

/// Default handler (halt)
inline void default_assert_handler(const char* file, int line, const char* expr) {
    (void)file;
    (void)line;
    (void)expr;
#if defined(__arm__) || defined(__aarch64__)
    __asm volatile("bkpt #0");
#else
    __builtin_trap();
#endif
    while (true) {} // Never reached
}

/// Global assert handler (thread-safe on supported platforms)
inline UMI_ATOMIC_ASSERT_PTR(AssertHandler) g_assert_handler{default_assert_handler};

/// Set custom assert handler
inline void set_assert_handler(AssertHandler handler) {
    UMI_ATOMIC_ASSERT_STORE(g_assert_handler, handler);
}

// ============================================================================
// Assert Macros
// ============================================================================

#if defined(NDEBUG) || defined(UMI_RELEASE)
    // Release: no-op
    #define UMI_ASSERT(expr) ((void)0)
    #define UMI_ASSERT_MSG(expr, msg) ((void)0)
#else
    // Debug: check and call handler
    #define UMI_ASSERT(expr) \
        do { \
            if (!(expr)) { \
                auto handler = UMI_ATOMIC_ASSERT_LOAD(::umi::g_assert_handler); \
                if (handler) handler(__FILE__, __LINE__, #expr); \
            } \
        } while (false)

    #define UMI_ASSERT_MSG(expr, msg) \
        do { \
            if (!(expr)) { \
                auto handler = UMI_ATOMIC_ASSERT_LOAD(::umi::g_assert_handler); \
                if (handler) handler(__FILE__, __LINE__, msg); \
            } \
        } while (false)
#endif

// ============================================================================
// Layer-specific Asserts
// ============================================================================

/// DSP layer: fast, no side effects
#define UMI_DSP_ASSERT(expr) UMI_ASSERT(expr)

/// Application layer: can log
#define UMI_APP_ASSERT(expr) UMI_ASSERT(expr)

/// Kernel layer: full diagnostics
#define UMI_KERNEL_ASSERT(expr) UMI_ASSERT(expr)

// ============================================================================
// Precondition/Postcondition (always checked, even in release)
// ============================================================================

#define UMI_REQUIRE(expr) \
    do { \
        if (!(expr)) { \
            auto handler = UMI_ATOMIC_ASSERT_LOAD(::umi::g_assert_handler); \
            if (handler) handler(__FILE__, __LINE__, "REQUIRE: " #expr); \
        } \
    } while (false)

#define UMI_ENSURE(expr) \
    do { \
        if (!(expr)) { \
            auto handler = UMI_ATOMIC_ASSERT_LOAD(::umi::g_assert_handler); \
            if (handler) handler(__FILE__, __LINE__, "ENSURE: " #expr); \
        } \
    } while (false)

} // namespace umi
