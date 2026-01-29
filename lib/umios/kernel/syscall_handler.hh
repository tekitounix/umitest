// SPDX-License-Identifier: MIT
// UMI-OS Syscall Handler
// Handles SVC exceptions and dispatches syscalls to kernel services

#pragma once

#include "loader.hh"
#include <cstdint>

namespace umi::kernel {

// ============================================================================
// Syscall Numbers (Application ABI)
// ============================================================================
// Must match lib/umios/app/syscall.hh nr::*
//
// Number layout:
//   0–15:   Core API (process control, scheduling, info)
//   16–31:  Reserved (core API expansion)
//   32–47:  Filesystem
//   48–63:  Reserved (storage expansion)
//   64–255: Reserved

namespace app_syscall {
    // --- Core API (0–15) ---
    inline constexpr uint32_t exit             = 0;
    inline constexpr uint32_t yield            = 1;
    inline constexpr uint32_t wait_event       = 2;
    inline constexpr uint32_t get_time         = 3;
    inline constexpr uint32_t get_shared       = 4;
    inline constexpr uint32_t register_proc    = 5;
    inline constexpr uint32_t unregister_proc  = 6;
    // 7–15: reserved

    // --- Filesystem (32–47) ---
    inline constexpr uint32_t file_open        = 32;
    inline constexpr uint32_t file_read        = 33;
    inline constexpr uint32_t file_write       = 34;
    inline constexpr uint32_t file_close       = 35;
    inline constexpr uint32_t file_seek        = 36;
    inline constexpr uint32_t file_stat        = 37;
    inline constexpr uint32_t dir_open         = 38;
    inline constexpr uint32_t dir_read         = 39;
    inline constexpr uint32_t dir_close        = 40;
    // 41–47: reserved
}

// ============================================================================
// Syscall Context
// ============================================================================

/// Context passed to syscall handler
struct SyscallContext {
    uint32_t syscall_nr;    ///< Syscall number (r0)
    uint32_t arg0;          ///< First argument (r1)
    uint32_t arg1;          ///< Second argument (r2)
    uint32_t arg2;          ///< Third argument (r3)
    uint32_t arg3;          ///< Fourth argument (r4, if needed)

    /// Set return value (written back to r0)
    int32_t result = 0;
};

// ============================================================================
// Syscall Handler Interface
// ============================================================================

/// Syscall handler - processes syscalls from applications
///
/// This is the generic/reference implementation.
/// Platform-specific kernels (e.g. stm32f4_kernel) may implement their
/// own handler directly in svc_handler_impl() for efficiency.
template <class KernelType>
class SyscallHandler {
public:
    SyscallHandler(KernelType& kernel, AppLoader& loader, SharedMemory& shared) noexcept
        : kernel_(kernel), loader_(loader), shared_(shared) {}

    int32_t handle(SyscallContext& ctx) noexcept {
        switch (ctx.syscall_nr) {
        case app_syscall::exit:
            loader_.terminate(static_cast<int>(ctx.arg0));
            kernel_.yield();
            return 0;

        case app_syscall::yield:
            kernel_.yield();
            return 0;

        case app_syscall::register_proc:
            if (ctx.arg1 != 0) {
                return loader_.register_processor(
                    reinterpret_cast<void*>(ctx.arg0),
                    reinterpret_cast<ProcessFn>(ctx.arg1));
            }
            return loader_.register_processor(reinterpret_cast<void*>(ctx.arg0));

        default:
            return -1;
        }
    }

private:
    KernelType& kernel_;
    AppLoader& loader_;
    SharedMemory& shared_;
};

} // namespace umi::kernel
