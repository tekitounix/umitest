// SPDX-License-Identifier: MIT
// UMI-OS Syscall Numbers (platform-independent)
// Must match lib/umios/app/syscall.hh nr::*
#pragma once

#include <cstdint>

namespace umi::syscall {

// ============================================================================
// Syscall Numbers
// ============================================================================
// Number layout (sparse, grouped by 10):
//   0– 9:  Process Control
//   10–19: Time / Scheduling
//   20–29: Configuration
//   30–39: MIDI / SysEx (reserved)
//   40–49: Info
//   50–59: I/O
//   60–69: Filesystem

// --- Group 0: Process Control (0–9) ---
inline constexpr uint8_t sys_exit             = 0;
inline constexpr uint8_t sys_yield            = 1;
inline constexpr uint8_t sys_register_proc    = 2;
inline constexpr uint8_t sys_unregister_proc  = 3;
// 4–9: reserved

// --- Group 1: Time / Scheduling (10–19) ---
inline constexpr uint8_t sys_wait_event       = 10;
inline constexpr uint8_t sys_get_time         = 11;
inline constexpr uint8_t sys_sleep            = 12;
// 13–19: reserved

// --- Group 2: Configuration (20–29) ---
inline constexpr uint8_t sys_set_app_config     = 20;
inline constexpr uint8_t sys_set_route_table    = 21;
inline constexpr uint8_t sys_set_param_mapping  = 22;
inline constexpr uint8_t sys_set_input_mapping  = 23;
inline constexpr uint8_t sys_configure_input    = 24;
inline constexpr uint8_t sys_send_param_request = 25;
// 26–29: reserved

// --- Group 4: Info (40–49) ---
inline constexpr uint8_t sys_get_shared       = 40;
// 41–49: reserved

// --- Group 5: I/O (50–59) ---
inline constexpr uint8_t sys_log              = 50;
inline constexpr uint8_t sys_panic            = 51;
// 52–59: reserved

// --- Group 6: Filesystem (60–69) ---
inline constexpr uint8_t sys_file_open        = 60;
inline constexpr uint8_t sys_file_read        = 61;
inline constexpr uint8_t sys_file_write       = 62;
inline constexpr uint8_t sys_file_close       = 63;
inline constexpr uint8_t sys_file_seek        = 64;
inline constexpr uint8_t sys_file_stat        = 65;
inline constexpr uint8_t sys_dir_open         = 66;
inline constexpr uint8_t sys_dir_read         = 67;
inline constexpr uint8_t sys_dir_close        = 68;
// 69: reserved

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
