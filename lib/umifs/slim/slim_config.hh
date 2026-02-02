// SPDX-License-Identifier: MIT
// Copyright (c) 2025, tekitounix

#pragma once

#include <cstdint>
#include <span>

namespace umi::fs {

/// Block device configuration for slimfs.
///
/// All buffers must be provided by the caller (no heap allocation).
/// Callback functions use the config pointer for context access.
struct SlimConfig {
    void* context = nullptr;

    // Block device callbacks
    int (*read)(const SlimConfig* cfg, uint32_t block, uint32_t off, void* buf, uint32_t size) = nullptr;
    int (*prog)(const SlimConfig* cfg, uint32_t block, uint32_t off, const void* buf, uint32_t size) = nullptr;
    int (*erase)(const SlimConfig* cfg, uint32_t block) = nullptr;
    int (*sync)(const SlimConfig* cfg) = nullptr;

    // Block device geometry
    uint32_t read_size = 0;
    uint32_t prog_size = 0;
    uint32_t block_size = 0;
    uint32_t block_count = 0;

    // Limits (0 = use defaults)
    uint32_t name_max = 0;
    uint32_t file_max = 0;

    // User-provided buffers (no heap)
    std::span<uint8_t> read_buffer;
    std::span<uint8_t> prog_buffer;
    std::span<uint8_t> lookahead_buffer;
};

} // namespace umi::fs
