// SPDX-License-Identifier: MIT
// UMI-OS Storage Service
// Async file system access service running on SystemTask
// See: docs/umios-architecture/19-storage-service.md

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <umi/fs/fat/ff.hh>
#include <umi/fs/fat/ff_types.hh>
#include <umi/fs/slim/slim.hh>
#include <umi/fs/slim/slim_config.hh>

#include "../core/fs_types.hh"
#include "../core/syscall_nr.hh"
#include "block_device.hh"

namespace umi::kernel {

// Types from core: umi::syscall::FsResultSlot, FsInfo, FsStatInfo, OpenFlags, Whence
using umi::syscall::FsInfo;
using umi::syscall::FsResultSlot;
using umi::syscall::FsStatInfo;

// ============================================================================
// FS Request (enqueued from syscall handler)
// ============================================================================

/// Raw FS request — stores syscall number + up to 4 register args
struct FsRequest {
    uint8_t syscall_nr; ///< 60–83
    uint32_t arg0;      ///< r0
    uint32_t arg1;      ///< r1
    uint32_t arg2;      ///< r2
    uint32_t arg3;      ///< r3
};

// ============================================================================
// StorageService Configuration
// ============================================================================

inline constexpr size_t MAX_OPEN_FILES = 4;
inline constexpr size_t MAX_OPEN_DIRS = 2;

/// POSIX errno values used by FS operations
namespace fs_errno {
inline constexpr int32_t ENOENT = -2;
inline constexpr int32_t EIO = -5;
inline constexpr int32_t EBADF = -9;
inline constexpr int32_t EAGAIN = -11;
inline constexpr int32_t ENOMEM = -12;
inline constexpr int32_t EEXIST = -17;
inline constexpr int32_t ENODEV = -19;
inline constexpr int32_t ENOTDIR = -20;
inline constexpr int32_t EISDIR = -21;
inline constexpr int32_t EINVAL = -22;
inline constexpr int32_t EFBIG = -27;
inline constexpr int32_t ENOSPC = -28;
inline constexpr int32_t ENAMETOOLONG = -36;
inline constexpr int32_t ENOSYS = -38;
inline constexpr int32_t ENOTEMPTY = -39;
inline constexpr int32_t EBUSY = -16;
inline constexpr int32_t ECORRUPT = -84;
} // namespace fs_errno

// ============================================================================
// StorageService
// ============================================================================

/// Storage service running on SystemTask.
///
/// Receives FS requests from syscall handler (one at a time),
/// dispatches to slimfs (/flash/) or FATfs (/sd/),
/// writes result to SharedMemory::FsResultSlot, and notifies ControlTask via event::fs.
///
/// Template parameters allow injecting BlockDevice implementations.
template <BlockDeviceLike FlashDev, BlockDeviceLike SdDev>
class StorageService {
  public:
    StorageService(FlashDev& flash, SdDev& sd, FsResultSlot& result_slot) noexcept
        : flash_(flash), sd_(sd), result_slot_(result_slot) {}

    /// Initialize filesystems (called during SystemTask init)
    int init() noexcept {
        // Configure slimfs with flash device callbacks
        slim_cfg_.read =
            [](const umi::fs::SlimConfig* cfg, uint32_t block, uint32_t off, void* buf, uint32_t size) -> int {
            auto* self = static_cast<StorageService*>(cfg->context);
            return self->flash_.read(block, off, buf, size);
        };
        slim_cfg_.prog =
            [](const umi::fs::SlimConfig* cfg, uint32_t block, uint32_t off, const void* buf, uint32_t size) -> int {
            auto* self = static_cast<StorageService*>(cfg->context);
            return self->flash_.write(block, off, buf, size);
        };
        slim_cfg_.erase = [](const umi::fs::SlimConfig* cfg, uint32_t block) -> int {
            auto* self = static_cast<StorageService*>(cfg->context);
            return self->flash_.erase(block);
        };
        slim_cfg_.sync = [](const umi::fs::SlimConfig*) -> int { return 0; };
        slim_cfg_.context = this;
        slim_cfg_.block_size = flash_.block_size();
        slim_cfg_.block_count = flash_.block_count();
        slim_cfg_.read_size = 1;
        slim_cfg_.prog_size = 1;
        slim_cfg_.read_buffer = {slim_rd_buf_, flash_.block_size()};
        slim_cfg_.prog_buffer = {slim_pr_buf_, flash_.block_size()};
        slim_cfg_.lookahead_buffer = {slim_la_buf_, sizeof(slim_la_buf_)};

        int rc = slim_.mount(&slim_cfg_);
        if (rc != 0) {
            // Try format + mount on first use
            rc = slim_.format(&slim_cfg_);
            if (rc != 0)
                return rc;
            rc = slim_.mount(&slim_cfg_);
        }
        flash_mounted_ = (rc == 0);

        // SD mount is deferred until card insertion
        return rc;
    }

    /// Enqueue a request (called from SVC handler context).
    /// Returns true if accepted, false if busy.
    bool enqueue(const FsRequest& req) noexcept {
        if (pending_)
            return false;
        request_ = req;
        pending_ = true;
        return true;
    }

    /// Check if there is a pending request
    [[nodiscard]] bool has_pending() const noexcept { return pending_; }

    /// Process the pending request (called from SystemTask loop).
    /// Returns true if a request was processed.
    bool process_one() noexcept {
        if (!pending_)
            return false;
        pending_ = false;

        int32_t result = dispatch(request_);
        result_slot_.set(result);
        return true;
    }

    /// Handle FsResult syscall (Nr=84).
    /// Returns result value and clears the slot, or EAGAIN if not ready.
    int32_t consume_result() noexcept {
        if (!result_slot_.ready)
            return fs_errno::EAGAIN;
        return result_slot_.consume();
    }

    /// Close all open files/dirs (called on app exit/fault)
    void close_all() noexcept {
        slim_.close_all({slim_files_, MAX_OPEN_FILES});
        for (size_t i = 0; i < MAX_OPEN_FILES; ++i) {
            if (fd_type_[i] == FdType::FAT_FILE) {
                fat_.close(&fat_files_[i]);
            }
            fd_type_[i] = FdType::NONE;
        }
        for (size_t i = 0; i < MAX_OPEN_DIRS; ++i) {
            if (dir_type_[i] == FdType::SLIM_DIR) {
                (void)slim_.dir_close(slim_dirs_[i]);
            } else if (dir_type_[i] == FdType::FAT_DIR) {
                fat_.closedir(&fat_dirs_[i]);
            }
            dir_type_[i] = FdType::NONE;
        }
    }

    /// Notify SD card insertion
    void on_sd_insert() noexcept {
        if (!sd_mounted_) {
            umi::fs::FatFsVolume vol{};
            if (fat_.mount(&vol, "", 1) == umi::fs::FatResult::OK) {
                sd_mounted_ = true;
            }
        }
    }

    /// Notify SD card removal
    void on_sd_remove() noexcept {
        // Force-close all /sd/ fds
        for (size_t i = 0; i < MAX_OPEN_FILES; ++i) {
            if (fd_type_[i] == FdType::FAT_FILE) {
                fat_.close(&fat_files_[i]);
                fd_type_[i] = FdType::NONE;
            }
        }
        for (size_t i = 0; i < MAX_OPEN_DIRS; ++i) {
            if (dir_type_[i] == FdType::FAT_DIR) {
                fat_.closedir(&fat_dirs_[i]);
                dir_type_[i] = FdType::NONE;
            }
        }
        fat_.unmount("");
        sd_mounted_ = false;
    }

  private:
    enum class FdType : uint8_t { NONE, SLIM_FILE, FAT_FILE, SLIM_DIR, FAT_DIR };

    // --- Path routing ---

    static bool is_flash_path(const char* path) noexcept {
        // /flash/... → slimfs
        return path[0] == '/' && path[1] == 'f' && path[2] == 'l';
    }

    /// Strip mount prefix: "/flash/foo" → "foo", "/sd/foo" → "foo"
    static const char* strip_prefix(const char* path) noexcept {
        if (path[0] == '/') {
            const char* p = path + 1;
            while (*p && *p != '/')
                ++p;
            if (*p == '/')
                return p + 1;
            return p; // root of mount
        }
        return path;
    }

    // --- FATfs error conversion ---

    static int32_t fat_to_errno(umi::fs::FatResult r) noexcept {
        using FR = umi::fs::FatResult;
        switch (r) {
        case FR::OK:
            return 0;
        case FR::DISK_ERR:
            return fs_errno::EIO;
        case FR::NO_FILE:
            return fs_errno::ENOENT;
        case FR::NO_PATH:
            return fs_errno::ENOENT;
        case FR::DENIED:
            return fs_errno::EISDIR;
        case FR::EXIST:
            return fs_errno::EEXIST;
        case FR::WRITE_PROTECTED:
            return fs_errno::EIO;
        case FR::NOT_READY:
            return fs_errno::ENODEV;
        case FR::INVALID_NAME:
            return fs_errno::EINVAL;
        case FR::INVALID_PARAMETER:
            return fs_errno::EINVAL;
        default:
            return fs_errno::EIO;
        }
    }

    // --- fd allocation ---

    int alloc_fd(FdType type) noexcept {
        for (size_t i = 0; i < MAX_OPEN_FILES; ++i) {
            if (fd_type_[i] == FdType::NONE) {
                fd_type_[i] = type;
                return static_cast<int>(i);
            }
        }
        return fs_errno::ENOMEM;
    }

    int alloc_dirfd(FdType type) noexcept {
        for (size_t i = 0; i < MAX_OPEN_DIRS; ++i) {
            if (dir_type_[i] == FdType::NONE) {
                dir_type_[i] = type;
                return static_cast<int>(i);
            }
        }
        return fs_errno::ENOMEM;
    }

    // --- Main dispatch ---

    int32_t dispatch(const FsRequest& req) noexcept {
        using namespace umi::syscall;

        switch (req.syscall_nr) {
        // File operations
        case nr::file_open:
            return do_file_open(req);
        case nr::file_read:
            return do_file_read(req);
        case nr::file_write:
            return do_file_write(req);
        case nr::file_close:
            return do_file_close(req);
        case nr::file_seek:
            return do_file_seek(req);
        case nr::file_tell:
            return do_file_tell(req);
        case nr::file_size:
            return do_file_size(req);
        case nr::file_truncate:
            return do_file_truncate(req);
        case nr::file_sync:
            return do_file_sync(req);
        // Directory operations
        case nr::dir_open:
            return do_dir_open(req);
        case nr::dir_read:
            return do_dir_read(req);
        case nr::dir_close:
            return do_dir_close(req);
        case nr::dir_seek:
            return do_dir_seek(req);
        case nr::dir_tell:
            return do_dir_tell(req);
        // Path operations
        case nr::stat:
            return do_stat(req);
        case nr::fstat:
            return do_fstat(req);
        case nr::mkdir:
            return do_mkdir(req);
        case nr::remove:
            return do_remove(req);
        case nr::rename:
            return do_rename(req);
        // Custom attributes
        case nr::getattr:
            return do_getattr(req);
        case nr::setattr:
            return do_setattr(req);
        case nr::removeattr:
            return do_removeattr(req);
        // FS info
        case nr::fs_stat:
            return do_fs_stat(req);
        default:
            return fs_errno::EINVAL;
        }
    }

    // ================================================================
    // File operations
    // ================================================================

    int32_t do_file_open(const FsRequest& req) noexcept {
        auto* path = reinterpret_cast<const char*>(req.arg0);
        auto flags = static_cast<umi::fs::SlimOpenFlags>(req.arg1);

        if (is_flash_path(path)) {
            if (!flash_mounted_)
                return fs_errno::ENODEV;
            int fd = alloc_fd(FdType::SLIM_FILE);
            if (fd < 0)
                return fd;
            int rc = slim_.file_open(slim_files_[fd], strip_prefix(path), flags);
            if (rc != 0) {
                fd_type_[fd] = FdType::NONE;
                return rc;
            }
            return fd;
        } else {
            if (!sd_mounted_)
                return fs_errno::ENODEV;
            int fd = alloc_fd(FdType::FAT_FILE);
            if (fd < 0)
                return fd;
            // Convert OpenFlags to FATfs mode
            uint8_t mode = 0x01; // FA_READ
            auto f = static_cast<uint32_t>(flags);
            if (f & 0x02)
                mode = 0x02; // FA_WRITE
            if (f & 0x03)
                mode = 0x03; // FA_READ | FA_WRITE
            if (f & 0x0100)
                mode |= 0x08; // FA_CREATE_NEW → FA_OPEN_ALWAYS
            if (f & 0x0400)
                mode |= 0x04; // FA_CREATE_ALWAYS (TRUNC)
            auto rc = fat_.open(&fat_files_[fd], strip_prefix(path), mode);
            if (rc != umi::fs::FatResult::OK) {
                fd_type_[fd] = FdType::NONE;
                return fat_to_errno(rc);
            }
            return fd;
        }
    }

    int32_t do_file_read(const FsRequest& req) noexcept {
        int fd = static_cast<int>(req.arg0);
        auto* buf = reinterpret_cast<uint8_t*>(req.arg1);
        uint32_t len = req.arg2;

        if (fd < 0 || fd >= static_cast<int>(MAX_OPEN_FILES))
            return fs_errno::EBADF;
        if (fd_type_[fd] == FdType::SLIM_FILE) {
            return slim_.file_read(slim_files_[fd], {buf, len});
        } else if (fd_type_[fd] == FdType::FAT_FILE) {
            uint32_t br = 0;
            auto rc = fat_.read(&fat_files_[fd], buf, len, &br);
            if (rc != umi::fs::FatResult::OK)
                return fat_to_errno(rc);
            return static_cast<int32_t>(br);
        }
        return fs_errno::EBADF;
    }

    int32_t do_file_write(const FsRequest& req) noexcept {
        int fd = static_cast<int>(req.arg0);
        auto* buf = reinterpret_cast<const uint8_t*>(req.arg1);
        uint32_t len = req.arg2;

        if (fd < 0 || fd >= static_cast<int>(MAX_OPEN_FILES))
            return fs_errno::EBADF;
        if (fd_type_[fd] == FdType::SLIM_FILE) {
            return slim_.file_write(slim_files_[fd], {buf, len});
        } else if (fd_type_[fd] == FdType::FAT_FILE) {
            uint32_t bw = 0;
            auto rc = fat_.write(&fat_files_[fd], buf, len, &bw);
            if (rc != umi::fs::FatResult::OK)
                return fat_to_errno(rc);
            return static_cast<int32_t>(bw);
        }
        return fs_errno::EBADF;
    }

    int32_t do_file_close(const FsRequest& req) noexcept {
        int fd = static_cast<int>(req.arg0);
        if (fd < 0 || fd >= static_cast<int>(MAX_OPEN_FILES))
            return fs_errno::EBADF;
        int32_t rc = 0;
        if (fd_type_[fd] == FdType::SLIM_FILE) {
            rc = slim_.file_close(slim_files_[fd]);
        } else if (fd_type_[fd] == FdType::FAT_FILE) {
            rc = fat_to_errno(fat_.close(&fat_files_[fd]));
        } else {
            return fs_errno::EBADF;
        }
        fd_type_[fd] = FdType::NONE;
        return rc;
    }

    int32_t do_file_seek(const FsRequest& req) noexcept {
        int fd = static_cast<int>(req.arg0);
        auto offset = static_cast<int32_t>(req.arg1);
        auto whence = static_cast<uint8_t>(req.arg2);

        if (fd < 0 || fd >= static_cast<int>(MAX_OPEN_FILES))
            return fs_errno::EBADF;
        if (fd_type_[fd] == FdType::SLIM_FILE) {
            return slim_.file_seek(slim_files_[fd], offset, static_cast<umi::fs::SlimWhence>(whence));
        } else if (fd_type_[fd] == FdType::FAT_FILE) {
            // FATfs only supports absolute seek
            uint32_t abs_pos = 0;
            if (whence == 0) {
                abs_pos = static_cast<uint32_t>(offset);
            } else if (whence == 1) {
                abs_pos = fat_files_[fd].fptr + static_cast<uint32_t>(offset);
            } else if (whence == 2) {
                abs_pos = fat_files_[fd].obj.objsize + static_cast<uint32_t>(offset);
            }
            auto rc = fat_.lseek(&fat_files_[fd], abs_pos);
            if (rc != umi::fs::FatResult::OK)
                return fat_to_errno(rc);
            return static_cast<int32_t>(fat_files_[fd].fptr);
        }
        return fs_errno::EBADF;
    }

    int32_t do_file_tell(const FsRequest& req) noexcept {
        int fd = static_cast<int>(req.arg0);
        if (fd < 0 || fd >= static_cast<int>(MAX_OPEN_FILES))
            return fs_errno::EBADF;
        if (fd_type_[fd] == FdType::SLIM_FILE) {
            return slim_.file_tell(slim_files_[fd]);
        } else if (fd_type_[fd] == FdType::FAT_FILE) {
            return static_cast<int32_t>(fat_files_[fd].fptr);
        }
        return fs_errno::EBADF;
    }

    int32_t do_file_size(const FsRequest& req) noexcept {
        int fd = static_cast<int>(req.arg0);
        if (fd < 0 || fd >= static_cast<int>(MAX_OPEN_FILES))
            return fs_errno::EBADF;
        if (fd_type_[fd] == FdType::SLIM_FILE) {
            return slim_.file_size(slim_files_[fd]);
        } else if (fd_type_[fd] == FdType::FAT_FILE) {
            return static_cast<int32_t>(fat_files_[fd].obj.objsize);
        }
        return fs_errno::EBADF;
    }

    int32_t do_file_truncate(const FsRequest& req) noexcept {
        int fd = static_cast<int>(req.arg0);
        uint32_t size = req.arg1;
        if (fd < 0 || fd >= static_cast<int>(MAX_OPEN_FILES))
            return fs_errno::EBADF;
        if (fd_type_[fd] == FdType::SLIM_FILE) {
            return slim_.file_truncate(slim_files_[fd], size);
        } else if (fd_type_[fd] == FdType::FAT_FILE) {
            // FATfs: seek to size, then truncate
            auto rc = fat_.lseek(&fat_files_[fd], size);
            if (rc != umi::fs::FatResult::OK)
                return fat_to_errno(rc);
            return fat_to_errno(fat_.truncate(&fat_files_[fd]));
        }
        return fs_errno::EBADF;
    }

    int32_t do_file_sync(const FsRequest& req) noexcept {
        int fd = static_cast<int>(req.arg0);
        if (fd < 0 || fd >= static_cast<int>(MAX_OPEN_FILES))
            return fs_errno::EBADF;
        if (fd_type_[fd] == FdType::SLIM_FILE) {
            return slim_.file_sync(slim_files_[fd]);
        } else if (fd_type_[fd] == FdType::FAT_FILE) {
            return fat_to_errno(fat_.sync(&fat_files_[fd]));
        }
        return fs_errno::EBADF;
    }

    // ================================================================
    // Directory operations
    // ================================================================

    int32_t do_dir_open(const FsRequest& req) noexcept {
        auto* path = reinterpret_cast<const char*>(req.arg0);

        if (is_flash_path(path)) {
            if (!flash_mounted_)
                return fs_errno::ENODEV;
            int dfd = alloc_dirfd(FdType::SLIM_DIR);
            if (dfd < 0)
                return dfd;
            int rc = slim_.dir_open(slim_dirs_[dfd], strip_prefix(path));
            if (rc != 0) {
                dir_type_[dfd] = FdType::NONE;
                return rc;
            }
            return dfd;
        } else {
            if (!sd_mounted_)
                return fs_errno::ENODEV;
            int dfd = alloc_dirfd(FdType::FAT_DIR);
            if (dfd < 0)
                return dfd;
            auto rc = fat_.opendir(&fat_dirs_[dfd], strip_prefix(path));
            if (rc != umi::fs::FatResult::OK) {
                dir_type_[dfd] = FdType::NONE;
                return fat_to_errno(rc);
            }
            return dfd;
        }
    }

    int32_t do_dir_read(const FsRequest& req) noexcept {
        int dfd = static_cast<int>(req.arg0);
        auto* info = reinterpret_cast<FsInfo*>(req.arg1);

        if (dfd < 0 || dfd >= static_cast<int>(MAX_OPEN_DIRS))
            return fs_errno::EBADF;
        if (dir_type_[dfd] == FdType::SLIM_DIR) {
            umi::fs::SlimInfo si{};
            int rc = slim_.dir_read(slim_dirs_[dfd], si);
            if (rc < 0)
                return rc;
            if (rc == 0)
                return 0; // EOF
            info->type = static_cast<uint8_t>(si.type);
            info->size = si.size;
            std::strncpy(info->name, si.name, sizeof(info->name) - 1);
            info->name[sizeof(info->name) - 1] = '\0';
            return 1;
        } else if (dir_type_[dfd] == FdType::FAT_DIR) {
            umi::fs::FatFileInfo fi{};
            auto rc = fat_.readdir(&fat_dirs_[dfd], &fi);
            if (rc != umi::fs::FatResult::OK)
                return fat_to_errno(rc);
            if (fi.fname[0] == '\0')
                return 0;                             // EOF
            info->type = (fi.fattrib & 0x10) ? 2 : 1; // DIR=2, REG=1
            info->size = static_cast<uint32_t>(fi.fsize);
            std::strncpy(info->name, fi.fname, sizeof(info->name) - 1);
            info->name[sizeof(info->name) - 1] = '\0';
            return 1;
        }
        return fs_errno::EBADF;
    }

    int32_t do_dir_close(const FsRequest& req) noexcept {
        int dfd = static_cast<int>(req.arg0);
        if (dfd < 0 || dfd >= static_cast<int>(MAX_OPEN_DIRS))
            return fs_errno::EBADF;
        int32_t rc = 0;
        if (dir_type_[dfd] == FdType::SLIM_DIR) {
            rc = slim_.dir_close(slim_dirs_[dfd]);
        } else if (dir_type_[dfd] == FdType::FAT_DIR) {
            rc = fat_to_errno(fat_.closedir(&fat_dirs_[dfd]));
        } else {
            return fs_errno::EBADF;
        }
        dir_type_[dfd] = FdType::NONE;
        return rc;
    }

    int32_t do_dir_seek(const FsRequest& req) noexcept {
        int dfd = static_cast<int>(req.arg0);
        uint32_t off = req.arg1;
        if (dfd < 0 || dfd >= static_cast<int>(MAX_OPEN_DIRS))
            return fs_errno::EBADF;
        if (dir_type_[dfd] == FdType::SLIM_DIR) {
            return slim_.dir_seek(slim_dirs_[dfd], off);
        }
        // FATfs does not support dir_seek
        return fs_errno::ENOSYS;
    }

    int32_t do_dir_tell(const FsRequest& req) noexcept {
        int dfd = static_cast<int>(req.arg0);
        if (dfd < 0 || dfd >= static_cast<int>(MAX_OPEN_DIRS))
            return fs_errno::EBADF;
        if (dir_type_[dfd] == FdType::SLIM_DIR) {
            return slim_.dir_tell(slim_dirs_[dfd]);
        }
        return fs_errno::ENOSYS;
    }

    // ================================================================
    // Path operations
    // ================================================================

    int32_t do_stat(const FsRequest& req) noexcept {
        auto* path = reinterpret_cast<const char*>(req.arg0);
        auto* info = reinterpret_cast<FsInfo*>(req.arg1);

        if (is_flash_path(path)) {
            if (!flash_mounted_)
                return fs_errno::ENODEV;
            umi::fs::SlimInfo si{};
            int rc = slim_.stat(strip_prefix(path), si);
            if (rc != 0)
                return rc;
            info->type = static_cast<uint8_t>(si.type);
            info->size = si.size;
            std::strncpy(info->name, si.name, sizeof(info->name) - 1);
            info->name[sizeof(info->name) - 1] = '\0';
            return 0;
        } else {
            if (!sd_mounted_)
                return fs_errno::ENODEV;
            umi::fs::FatFileInfo fi{};
            auto rc = fat_.stat(strip_prefix(path), &fi);
            if (rc != umi::fs::FatResult::OK)
                return fat_to_errno(rc);
            info->type = (fi.fattrib & 0x10) ? 2 : 1;
            info->size = static_cast<uint32_t>(fi.fsize);
            std::strncpy(info->name, fi.fname, sizeof(info->name) - 1);
            info->name[sizeof(info->name) - 1] = '\0';
            return 0;
        }
    }

    int32_t do_fstat(const FsRequest& req) noexcept {
        int fd = static_cast<int>(req.arg0);
        auto* info = reinterpret_cast<FsInfo*>(req.arg1);

        if (fd < 0 || fd >= static_cast<int>(MAX_OPEN_FILES))
            return fs_errno::EBADF;
        if (fd_type_[fd] == FdType::SLIM_FILE) {
            info->type = 1; // REG
            info->size = static_cast<uint32_t>(slim_.file_size(slim_files_[fd]));
            info->name[0] = '\0';
            return 0;
        } else if (fd_type_[fd] == FdType::FAT_FILE) {
            info->type = 1; // REG
            info->size = static_cast<uint32_t>(fat_files_[fd].obj.objsize);
            info->name[0] = '\0';
            return 0;
        }
        return fs_errno::EBADF;
    }

    int32_t do_mkdir(const FsRequest& req) noexcept {
        auto* path = reinterpret_cast<const char*>(req.arg0);
        if (is_flash_path(path)) {
            if (!flash_mounted_)
                return fs_errno::ENODEV;
            return slim_.mkdir(strip_prefix(path));
        } else {
            if (!sd_mounted_)
                return fs_errno::ENODEV;
            return fat_to_errno(fat_.mkdir(strip_prefix(path)));
        }
    }

    int32_t do_remove(const FsRequest& req) noexcept {
        auto* path = reinterpret_cast<const char*>(req.arg0);
        if (is_flash_path(path)) {
            if (!flash_mounted_)
                return fs_errno::ENODEV;
            return slim_.remove(strip_prefix(path));
        } else {
            if (!sd_mounted_)
                return fs_errno::ENODEV;
            return fat_to_errno(fat_.unlink(strip_prefix(path)));
        }
    }

    int32_t do_rename(const FsRequest& req) noexcept {
        auto* oldpath = reinterpret_cast<const char*>(req.arg0);
        auto* newpath = reinterpret_cast<const char*>(req.arg1);
        // Both paths must be on the same mount
        if (is_flash_path(oldpath)) {
            if (!flash_mounted_)
                return fs_errno::ENODEV;
            return slim_.rename(strip_prefix(oldpath), strip_prefix(newpath));
        } else {
            if (!sd_mounted_)
                return fs_errno::ENODEV;
            return fat_to_errno(fat_.rename(strip_prefix(oldpath), strip_prefix(newpath)));
        }
    }

    // ================================================================
    // Custom attributes (slimfs only)
    // ================================================================

    int32_t do_getattr(const FsRequest& req) noexcept {
        auto* path = reinterpret_cast<const char*>(req.arg0);
        uint8_t type = static_cast<uint8_t>(req.arg1 & 0xFF);
        uint32_t len = req.arg1 >> 8;
        auto* buf = reinterpret_cast<uint8_t*>(req.arg2);
        if (!is_flash_path(path))
            return fs_errno::ENOSYS;
        if (!flash_mounted_)
            return fs_errno::ENODEV;
        return slim_.getattr(strip_prefix(path), type, {buf, len});
    }

    int32_t do_setattr(const FsRequest& req) noexcept {
        auto* path = reinterpret_cast<const char*>(req.arg0);
        uint8_t type = static_cast<uint8_t>(req.arg1 & 0xFF);
        uint32_t len = req.arg1 >> 8;
        auto* buf = reinterpret_cast<const uint8_t*>(req.arg2);
        if (!is_flash_path(path))
            return fs_errno::ENOSYS;
        if (!flash_mounted_)
            return fs_errno::ENODEV;
        return slim_.setattr(strip_prefix(path), type, {buf, len});
    }

    int32_t do_removeattr(const FsRequest& req) noexcept {
        auto* path = reinterpret_cast<const char*>(req.arg0);
        uint8_t type = static_cast<uint8_t>(req.arg1);
        if (!is_flash_path(path))
            return fs_errno::ENOSYS;
        if (!flash_mounted_)
            return fs_errno::ENODEV;
        return slim_.removeattr(strip_prefix(path), type);
    }

    // ================================================================
    // FS info
    // ================================================================

    int32_t do_fs_stat(const FsRequest& req) noexcept {
        auto* path = reinterpret_cast<const char*>(req.arg0);
        auto* info = reinterpret_cast<FsStatInfo*>(req.arg1);

        if (is_flash_path(path)) {
            if (!flash_mounted_)
                return fs_errno::ENODEV;
            info->block_size = slim_.block_size();
            info->block_count = slim_.block_count_total();
            int used = slim_.fs_size();
            info->blocks_used = (used >= 0) ? static_cast<uint32_t>(used) : 0;
            return 0;
        } else {
            if (!sd_mounted_)
                return fs_errno::ENODEV;
            uint32_t free_clust = 0;
            umi::fs::FatFsVolume* fs_ptr = nullptr;
            auto rc = fat_.getfree("", &free_clust, &fs_ptr);
            if (rc != umi::fs::FatResult::OK)
                return fat_to_errno(rc);
            if (fs_ptr) {
                info->block_size = fs_ptr->csize * 512; // cluster size in bytes
                info->block_count = fs_ptr->n_fatent - 2;
                info->blocks_used = info->block_count - free_clust;
            }
            return 0;
        }
    }

    // ================================================================
    // State
    // ================================================================

    FlashDev& flash_;
    SdDev& sd_;
    FsResultSlot& result_slot_;

    // Pending request (queue depth = 1)
    FsRequest request_{};
    bool pending_ = false;

    // Filesystem instances
    umi::fs::SlimFs slim_{};
    umi::fs::FatFs fat_{};
    umi::fs::SlimConfig slim_cfg_{};

    // slimfs buffers (sized for typical 512B block)
    static constexpr size_t SLIM_BUF_SIZE = 512;
    uint8_t slim_rd_buf_[SLIM_BUF_SIZE]{};
    uint8_t slim_pr_buf_[SLIM_BUF_SIZE]{};
    uint8_t slim_la_buf_[32]{}; // 256 blocks lookahead

    // File/dir handle pools
    umi::fs::SlimFile slim_files_[MAX_OPEN_FILES]{};
    umi::fs::FatFile fat_files_[MAX_OPEN_FILES]{};
    FdType fd_type_[MAX_OPEN_FILES]{};

    umi::fs::SlimDir slim_dirs_[MAX_OPEN_DIRS]{};
    umi::fs::FatDir fat_dirs_[MAX_OPEN_DIRS]{};
    FdType dir_type_[MAX_OPEN_DIRS]{};

    bool flash_mounted_ = false;
    bool sd_mounted_ = false;
};

} // namespace umi::kernel
