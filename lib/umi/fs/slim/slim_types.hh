// SPDX-License-Identifier: MIT
// Copyright (c) 2025, tekitounix

#pragma once

#include <cstdint>
#include <span>
#include <type_traits>

namespace umi::fs {

// =====================================================================
// Constants
// =====================================================================

constexpr uint32_t SLIM_MAGIC = 0x534C494D;   // "SLIM"
constexpr uint32_t SLIM_VERSION = 0x00010000; // v1.0
constexpr uint32_t SLIM_BLOCK_NULL = UINT32_MAX;
constexpr uint32_t SLIM_BLOCK_INLINE = UINT32_MAX - 1;

constexpr uint32_t SLIM_NAME_MAX_DEFAULT = 255;
constexpr uint32_t SLIM_FILE_MAX_DEFAULT = 0x7FFFFFFF; // 2 GiB

// Superblock layout
constexpr uint32_t SLIM_SUPER_SIZE = 64;
constexpr uint32_t SLIM_SUPER_BLOCK_A = 0;
constexpr uint32_t SLIM_SUPER_BLOCK_B = 1;

// Metadata header size
constexpr uint32_t SLIM_META_HEADER_SIZE = 12;

// Entry fixed header size (before name)
constexpr uint32_t SLIM_ENTRY_HEADER_SIZE = 16;

// =====================================================================
// Error codes (negative, POSIX errno compatible)
// =====================================================================

enum class SlimError : int {
    OK = 0,
    IO = -5,
    CORRUPT = -84,
    NOENT = -2,
    EXIST = -17,
    NOTDIR = -20,
    ISDIR = -21,
    NOTEMPTY = -39,
    BADF = -9,
    FBIG = -27,
    INVAL = -22,
    NOSPC = -28,
    NOMEM = -12,
    NAMETOOLONG = -36,
};

// =====================================================================
// Type enumerations
// =====================================================================

enum class SlimType : uint8_t {
    REG = 1,
    DIR = 2,
    DEL = 0xFF,
};

enum class SlimOpenFlags : uint32_t {
    RDONLY = 1,
    WRONLY = 2,
    RDWR = 3,
    CREAT = 0x0100,
    EXCL = 0x0200,
    TRUNC = 0x0400,
    APPEND = 0x0800,
};

enum class SlimWhence : int {
    SET = 0,
    CUR = 1,
    END = 2,
};

// Bitwise operators for SlimOpenFlags
constexpr SlimOpenFlags operator|(SlimOpenFlags a, SlimOpenFlags b) {
    using U = std::underlying_type_t<SlimOpenFlags>;
    return static_cast<SlimOpenFlags>(static_cast<U>(a) | static_cast<U>(b));
}
constexpr SlimOpenFlags operator&(SlimOpenFlags a, SlimOpenFlags b) {
    using U = std::underlying_type_t<SlimOpenFlags>;
    return static_cast<SlimOpenFlags>(static_cast<U>(a) & static_cast<U>(b));
}
constexpr bool has_flag(SlimOpenFlags flags, SlimOpenFlags test) {
    using U = std::underlying_type_t<SlimOpenFlags>;
    return (static_cast<U>(flags) & static_cast<U>(test)) != 0;
}

// =====================================================================
// Data structures
// =====================================================================

/// File information (returned by stat/dir_read)
struct SlimInfo {
    SlimType type;
    uint32_t size;
    char name[256];
};

/// File handle
struct SlimFile {
    SlimOpenFlags flags{};
    uint32_t pos = 0;
    uint32_t size = 0;
    uint32_t head_block = SLIM_BLOCK_NULL;
    uint32_t cur_block = SLIM_BLOCK_NULL;
    uint32_t cur_off = 0;
    // Seek cache: block_index[i] = block at offset i*(block_size-4)
    uint32_t block_index[8]{};
    uint32_t block_index_count = 0;
    // Inline file support
    bool is_inline = false;
    uint32_t dir_block = SLIM_BLOCK_NULL;
    uint16_t entry_index = 0;
    // Dirty flag for write buffering
    bool dirty = false;
    // COW: old chain to free after sync
    uint32_t old_head_block = SLIM_BLOCK_NULL;
};

/// Directory handle
struct SlimDir {
    uint32_t block = SLIM_BLOCK_NULL;
    uint32_t pos = 0;
    uint32_t count = 0;
};

} // namespace umi::fs
