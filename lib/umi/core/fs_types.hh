// SPDX-License-Identifier: MIT
// UMI-OS Filesystem Types — Shared between app and kernel
// See: docs/umios-architecture/19-storage-service.md

#pragma once

#include <cstdint>

namespace umi::syscall {

// ============================================================================
// File Open Flags (matches SlimOpenFlags values)
// ============================================================================

enum class OpenFlags : uint32_t {
    RDONLY = 1,
    WRONLY = 2,
    RDWR   = 3,
    CREAT  = 0x0100,
    EXCL   = 0x0200,
    TRUNC  = 0x0400,
    APPEND = 0x0800,
};

constexpr OpenFlags operator|(OpenFlags a, OpenFlags b) {
    return static_cast<OpenFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

// ============================================================================
// Seek Whence
// ============================================================================

enum class Whence : uint8_t {
    SET = 0,
    CUR = 1,
    END = 2,
};

// ============================================================================
// File/Directory Info (returned by stat/dir_read)
// ============================================================================

struct FsInfo {
    uint8_t type;       ///< 1=REG, 2=DIR
    uint32_t size;
    char name[64];      ///< Max 63 chars + NUL
};

// ============================================================================
// Filesystem Statistics (returned by fs_stat)
// ============================================================================

struct FsStatInfo {
    uint32_t block_size;
    uint32_t block_count;
    uint32_t blocks_used;
};

// ============================================================================
// FS Result Slot (in SharedMemory, written by StorageService)
// ============================================================================

struct FsResultSlot {
    int32_t value = 0;      ///< fd, byte count, position, 0, or error code
    bool ready = false;     ///< true when result is available

    void set(int32_t v) noexcept { value = v; ready = true; }
    int32_t consume() noexcept { ready = false; return value; }
};

} // namespace umi::syscall
