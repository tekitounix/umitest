// SPDX-License-Identifier: MIT
// UMI-OS Syscall Numbers — Single Source of Truth
// Shared between app (syscall.hh) and kernel (syscall_handler, svc_handler)
// See: docs/umios-architecture/06-syscall.md

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
//   60–89: Filesystem

namespace nr {

// --- Group 0: Process Control (0–9) ---
inline constexpr uint32_t exit             = 0;
inline constexpr uint32_t yield            = 1;
inline constexpr uint32_t register_proc    = 2;
inline constexpr uint32_t unregister_proc  = 3;
// 4–9: reserved

// --- Group 1: Time / Scheduling (10–19) ---
inline constexpr uint32_t wait_event       = 10;
inline constexpr uint32_t get_time         = 11;
inline constexpr uint32_t sleep            = 12;
// 13–19: reserved

// --- Group 2: Configuration (20–29) ---
inline constexpr uint32_t set_app_config     = 20;
// 21-24: reserved (formerly set_route_table, set_param_mapping,
//        set_input_mapping, configure_input — consolidated into set_app_config)
inline constexpr uint32_t send_param_request = 25;
// 26–29: reserved

// --- Group 4: Info (40–49) ---
inline constexpr uint32_t get_shared       = 40;
// 41–49: reserved

// --- Group 5: I/O (50–59) ---
inline constexpr uint32_t log              = 50;
inline constexpr uint32_t panic            = 51;
// 52–59: reserved

// --- Group 6: Filesystem (60–89) ---
// File operations (60–68)
inline constexpr uint32_t file_open        = 60;
inline constexpr uint32_t file_read        = 61;
inline constexpr uint32_t file_write       = 62;
inline constexpr uint32_t file_close       = 63;
inline constexpr uint32_t file_seek        = 64;
inline constexpr uint32_t file_tell        = 65;
inline constexpr uint32_t file_size        = 66;
inline constexpr uint32_t file_truncate    = 67;
inline constexpr uint32_t file_sync        = 68;
// 69: reserved
// Directory operations (70–74)
inline constexpr uint32_t dir_open         = 70;
inline constexpr uint32_t dir_read         = 71;
inline constexpr uint32_t dir_close        = 72;
inline constexpr uint32_t dir_seek         = 73;
inline constexpr uint32_t dir_tell         = 74;
// Path operations (75–79)
inline constexpr uint32_t stat             = 75;
inline constexpr uint32_t fstat            = 76;
inline constexpr uint32_t mkdir            = 77;
inline constexpr uint32_t remove           = 78;
inline constexpr uint32_t rename           = 79;
// Custom attributes (80–82)
inline constexpr uint32_t getattr          = 80;
inline constexpr uint32_t setattr          = 81;
inline constexpr uint32_t removeattr       = 82;
// FS info (83), FS result (84)
inline constexpr uint32_t fs_stat          = 83;
inline constexpr uint32_t fs_result        = 84;
// 85–89: reserved

} // namespace nr

// ============================================================================
// Syscall Range Helpers
// ============================================================================

/// Check if syscall number is in the FS range (60–84)
constexpr bool is_fs_syscall(uint32_t n) {
    return n >= 60 && n <= 84;
}

/// Check if syscall is an FS request (60–83, enqueued to StorageService)
constexpr bool is_fs_request(uint32_t n) {
    return n >= 60 && n <= 83;
}

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

// ============================================================================
// Event Bit Definitions
// ============================================================================

namespace event {
inline constexpr uint32_t audio    = (1 << 0);     ///< Audio buffer ready
inline constexpr uint32_t midi     = (1 << 1);     ///< MIDI data available
inline constexpr uint32_t vsync    = (1 << 2);     ///< Display refresh
inline constexpr uint32_t timer    = (1 << 3);     ///< Timer tick
inline constexpr uint32_t control  = (1 << 4);     ///< ControlEvent arrived
inline constexpr uint32_t fs       = (1 << 5);     ///< FS operation completed
inline constexpr uint32_t shutdown = (1u << 31);   ///< Shutdown requested
} // namespace event

} // namespace umi::syscall
