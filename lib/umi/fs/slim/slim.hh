// SPDX-License-Identifier: MIT
// Copyright (c) 2025, tekitounix

#pragma once

#include "slim_config.hh"
#include "slim_types.hh"

namespace umi::fs {

/// slimfs — a compact, power-loss safe embedded filesystem.
///
/// Internal state is intentionally public to keep implementation in a
/// single .cc file using free-standing helper functions (same pattern
/// as the littlefs and FATfs ports in this library).
class SlimFs {
  public:
    // =================================================================
    // Filesystem lifecycle
    // =================================================================

    [[nodiscard]] int format(const SlimConfig* config) noexcept;
    [[nodiscard]] int mount(const SlimConfig* config) noexcept;
    [[nodiscard]] int unmount() noexcept;

    // =================================================================
    // File operations
    // =================================================================

    [[nodiscard]] int file_open(SlimFile& file, const char* path, SlimOpenFlags flags) noexcept;
    [[nodiscard]] int file_close(SlimFile& file) noexcept;
    [[nodiscard]] int file_sync(SlimFile& file) noexcept;
    [[nodiscard]] int file_read(SlimFile& file, std::span<uint8_t> buf) noexcept;
    [[nodiscard]] int file_write(SlimFile& file, std::span<const uint8_t> buf) noexcept;
    [[nodiscard]] int file_seek(SlimFile& file, int32_t off, SlimWhence whence) noexcept;
    [[nodiscard]] int file_truncate(SlimFile& file, uint32_t size) noexcept;
    [[nodiscard]] int file_tell(const SlimFile& file) const noexcept;
    [[nodiscard]] int file_size(const SlimFile& file) const noexcept;

    // =================================================================
    // Directory operations
    // =================================================================

    [[nodiscard]] int mkdir(const char* path) noexcept;
    [[nodiscard]] int dir_open(SlimDir& dir, const char* path) noexcept;
    [[nodiscard]] int dir_close(SlimDir& dir) noexcept;
    [[nodiscard]] int dir_read(SlimDir& dir, SlimInfo& info) noexcept;
    [[nodiscard]] int dir_seek(SlimDir& dir, uint32_t off) noexcept;
    [[nodiscard]] int dir_tell(const SlimDir& dir) const noexcept;
    [[nodiscard]] int dir_rewind(SlimDir& dir) noexcept;

    // =================================================================
    // Path operations
    // =================================================================

    [[nodiscard]] int remove(const char* path) noexcept;
    [[nodiscard]] int rename(const char* oldpath, const char* newpath) noexcept;
    [[nodiscard]] int stat(const char* path, SlimInfo& info) noexcept;

    // =================================================================
    // Custom attributes
    // =================================================================

    [[nodiscard]] int getattr(const char* path, uint8_t type, std::span<uint8_t> buf) noexcept;
    [[nodiscard]] int setattr(const char* path, uint8_t type, std::span<const uint8_t> buf) noexcept;
    [[nodiscard]] int removeattr(const char* path, uint8_t type) noexcept;

    // =================================================================
    // Filesystem info
    // =================================================================

    [[nodiscard]] int fs_size() noexcept;
    [[nodiscard]] int fs_traverse(int (*cb)(void*, uint32_t), void* data) noexcept;
    [[nodiscard]] uint32_t block_size() const noexcept;
    [[nodiscard]] uint32_t block_count_total() const noexcept;
    [[nodiscard]] int fs_gc() noexcept;
    [[nodiscard]] int fs_grow(uint32_t new_block_count) noexcept;

    // =================================================================
    // Bulk operations (for StorageService auto-close)
    // =================================================================

    void close_all(std::span<SlimFile> files) noexcept;

    // =================================================================
    // Internal state — public for implementation access in slim_core.cc
    // =================================================================

    struct Cache {
        uint32_t block = SLIM_BLOCK_NULL;
        uint32_t off = 0;
        uint32_t size = 0;
        std::span<uint8_t> buffer;

        void drop() noexcept { block = SLIM_BLOCK_NULL; }
        [[nodiscard]] bool valid() const noexcept { return block != SLIM_BLOCK_NULL; }
    };

    struct PendingMove {
        uint32_t src_dir = SLIM_BLOCK_NULL;
        uint16_t src_id = 0;
        uint32_t dst_dir = SLIM_BLOCK_NULL;
        uint16_t dst_id = 0;

        [[nodiscard]] bool active() const noexcept { return src_dir != SLIM_BLOCK_NULL; }
        void clear() noexcept { src_dir = SLIM_BLOCK_NULL; }
    };

    struct Lookahead {
        uint32_t start = 0;
        uint32_t size = 0;
        uint32_t next = 0;
        std::span<uint8_t> buffer;
    };

    const SlimConfig* cfg = nullptr;
    Cache rcache{};
    Cache pcache{};

    uint32_t root_block = 0;
    uint32_t alloc_next = 0;
    uint32_t alloc_seed = 0;
    uint32_t block_count = 0;
    uint32_t name_max = SLIM_NAME_MAX_DEFAULT;
    uint32_t file_max = SLIM_FILE_MAX_DEFAULT;

    PendingMove pending_move{};
    Lookahead lookahead{};
};

} // namespace umi::fs
