// SPDX-License-Identifier: MIT
// Copyright (c) 2025, tekitounix
// FatFs disk I/O adapter for BlockDeviceLike

#pragma once

#include <umios/kernel/block_device.hh>

#include "ff_types.hh"

namespace umi::fs {

// ============================================================================
// Disk status bits
// ============================================================================

constexpr uint8_t STA_NOINIT = 0x01;
constexpr uint8_t STA_NODISK = 0x02;
constexpr uint8_t STA_PROTECT = 0x04;

// ============================================================================
// Disk I/O result codes
// ============================================================================

enum class DiskResult : uint8_t {
    OK = 0,
    ERROR = 1,
    WRPRT = 2,
    NOTRDY = 3,
    PARERR = 4,
};

// ============================================================================
// ioctl command codes
// ============================================================================

constexpr uint8_t CTRL_SYNC = 0;
constexpr uint8_t GET_SECTOR_COUNT = 1;
constexpr uint8_t GET_SECTOR_SIZE = 2;
constexpr uint8_t GET_BLOCK_SIZE = 3;

// ============================================================================
// DiskIo — adapts BlockDeviceLike to FatFs disk I/O interface
// ============================================================================

/// Disk I/O function table for FatFs.
/// Holds function pointers that the FatFs core calls for disk access.
struct DiskIo {
    void* context = nullptr;

    uint8_t (*initialize)(void* ctx) = nullptr;
    uint8_t (*status)(void* ctx) = nullptr;
    DiskResult (*read)(void* ctx, uint8_t* buff, LBA_t sector, uint32_t count) = nullptr;
    DiskResult (*write)(void* ctx, const uint8_t* buff, LBA_t sector, uint32_t count) = nullptr;
    DiskResult (*ioctl)(void* ctx, uint8_t cmd, void* buff) = nullptr;
};

/// Create a DiskIo adapter from a BlockDeviceLike device.
/// The device must outlive the returned DiskIo.
///
/// Mapping:
///   disk_read(sector, count) → dev.read(sector, 0, buf, count * 512)
///   disk_write(sector, count) → dev.write(sector, 0, buf, count * 512)
///   CTRL_SYNC → no-op (sync not needed for block device)
///   GET_SECTOR_COUNT → dev.block_count() * dev.block_size() / 512
///   GET_BLOCK_SIZE → dev.block_size() / 512
template <umi::kernel::BlockDeviceLike Dev>
DiskIo make_diskio(Dev& dev) {
    DiskIo io{};
    io.context = &dev;

    io.initialize = [](void* /*ctx*/) -> uint8_t { return 0; };

    io.status = [](void* /*ctx*/) -> uint8_t { return 0; };

    io.read = [](void* ctx, uint8_t* buff, LBA_t sector, uint32_t count) -> DiskResult {
        auto* d = static_cast<Dev*>(ctx);
        // Map sector-based reads to block device reads
        // Each sector is 512 bytes; block device may have different block size
        uint32_t blk_size = d->block_size();
        for (uint32_t i = 0; i < count; i++) {
            LBA_t s = sector + i;
            uint32_t block = (s * SECTOR_SIZE) / blk_size;
            uint32_t offset = (s * SECTOR_SIZE) % blk_size;
            int err = d->read(block, offset, buff + i * SECTOR_SIZE, SECTOR_SIZE);
            if (err != 0) {
                return DiskResult::ERROR;
            }
        }
        return DiskResult::OK;
    };

    io.write = [](void* ctx, const uint8_t* buff, LBA_t sector, uint32_t count) -> DiskResult {
        auto* d = static_cast<Dev*>(ctx);
        uint32_t blk_size = d->block_size();
        for (uint32_t i = 0; i < count; i++) {
            LBA_t s = sector + i;
            uint32_t block = (s * SECTOR_SIZE) / blk_size;
            uint32_t offset = (s * SECTOR_SIZE) % blk_size;
            int err = d->write(block, offset, buff + i * SECTOR_SIZE, SECTOR_SIZE);
            if (err != 0) {
                return DiskResult::ERROR;
            }
        }
        return DiskResult::OK;
    };

    io.ioctl = [](void* ctx, uint8_t cmd, void* buff) -> DiskResult {
        auto* d = static_cast<Dev*>(ctx);
        switch (cmd) {
        case CTRL_SYNC:
            return DiskResult::OK;
        case GET_SECTOR_COUNT: {
            auto total_bytes = static_cast<uint64_t>(d->block_count()) * d->block_size();
            *static_cast<uint32_t*>(buff) = static_cast<uint32_t>(total_bytes / SECTOR_SIZE);
            return DiskResult::OK;
        }
        case GET_SECTOR_SIZE:
            *static_cast<uint32_t*>(buff) = SECTOR_SIZE;
            return DiskResult::OK;
        case GET_BLOCK_SIZE:
            *static_cast<uint32_t*>(buff) = d->block_size() / SECTOR_SIZE;
            return DiskResult::OK;
        default:
            return DiskResult::PARERR;
        }
    };

    return io;
}

} // namespace umi::fs
