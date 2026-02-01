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
// Sparse layout grouped by 10 (see docs/umios-architecture/06-syscall.md)

namespace app_syscall {
    // --- Group 0: Process Control (0–9) ---
    inline constexpr uint32_t exit             = 0;
    inline constexpr uint32_t yield            = 1;
    inline constexpr uint32_t register_proc    = 2;
    inline constexpr uint32_t unregister_proc  = 3;

    // --- Group 1: Time / Scheduling (10–19) ---
    inline constexpr uint32_t wait_event       = 10;
    inline constexpr uint32_t get_time         = 11;
    inline constexpr uint32_t sleep            = 12;

    // --- Group 2: Configuration (20–29) ---
    inline constexpr uint32_t set_app_config     = 20;
    inline constexpr uint32_t set_route_table    = 21;
    inline constexpr uint32_t set_param_mapping  = 22;
    inline constexpr uint32_t set_input_mapping  = 23;
    inline constexpr uint32_t configure_input    = 24;
    inline constexpr uint32_t send_param_request = 25;

    // --- Group 4: Info (40–49) ---
    inline constexpr uint32_t get_shared       = 40;

    // --- Group 5: I/O (50–59) ---
    inline constexpr uint32_t log              = 50;
    inline constexpr uint32_t panic            = 51;

    // --- Group 6: Filesystem (60–69) ---
    inline constexpr uint32_t file_open        = 60;
    inline constexpr uint32_t file_read        = 61;
    inline constexpr uint32_t file_write       = 62;
    inline constexpr uint32_t file_close       = 63;
    inline constexpr uint32_t file_seek        = 64;
    inline constexpr uint32_t file_stat        = 65;
    inline constexpr uint32_t dir_open         = 66;
    inline constexpr uint32_t dir_read         = 67;
    inline constexpr uint32_t dir_close        = 68;
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
