// SPDX-License-Identifier: MIT
// UMI-OS Syscall Numbers (platform-independent)
// Must match lib/umios/app/syscall.hh nr::*
#pragma once

#include <cstdint>

namespace umi::syscall {

// ============================================================================
// Syscall Numbers
// ============================================================================
// Number layout:
//   0–15:   Core API (process control, scheduling, info)
//   16–31:  Reserved (core API expansion)
//   32–47:  Filesystem
//   48–63:  Reserved (storage expansion)
//   64–255: Reserved

// --- Core API (0–15) ---
inline constexpr uint8_t sys_exit             = 0;
inline constexpr uint8_t sys_yield            = 1;
inline constexpr uint8_t sys_wait_event       = 2;
inline constexpr uint8_t sys_get_time         = 3;
inline constexpr uint8_t sys_get_shared       = 4;
inline constexpr uint8_t sys_register_proc    = 5;
inline constexpr uint8_t sys_unregister_proc  = 6;
// 7–15: reserved

// --- Filesystem (32–47) ---
inline constexpr uint8_t sys_file_open        = 32;
inline constexpr uint8_t sys_file_read        = 33;
inline constexpr uint8_t sys_file_write       = 34;
inline constexpr uint8_t sys_file_close       = 35;
inline constexpr uint8_t sys_file_seek        = 36;
inline constexpr uint8_t sys_file_stat        = 37;
inline constexpr uint8_t sys_dir_open         = 38;
inline constexpr uint8_t sys_dir_read         = 39;
inline constexpr uint8_t sys_dir_close        = 40;
// 41–47: reserved

// ============================================================================
// Syscall Error Codes
// ============================================================================

enum class SyscallError : int32_t {
    OK              = 0,
    INVALID_SYSCALL = -1,
    INVALID_PARAM   = -2,
    ACCESS_DENIED   = -3,
    NOT_FOUND       = -4,
    TIMEOUT         = -5,
    BUSY            = -6,
};

}  // namespace umi::syscall
