// SPDX-License-Identifier: MIT
// Copyright (c) 2025, tekitounix

#pragma once

#include "ff_config.hh"
#include "ff_diskio.hh"
#include "ff_types.hh"

namespace umi::fs {

/// FatFs filesystem (replaces the global function API)
///
/// Usage:
///   FatFs fs;
///   fs.set_diskio(&diskio);  // provide disk I/O adapter
///   fs.mount("", 1);
///   FatFile fil;
///   fs.open(&fil, "test.txt", FA_READ);
///   ...
class FatFs {
  public:
    /// Set the disk I/O function table. Must be called before mount.
    void set_diskio(DiskIo* io) noexcept { diskio = io; }

    // =================================================================
    // Mount / Unmount
    // =================================================================

    /// Mount a logical drive
    FatResult mount(FatFsVolume* fs, const char* path, uint8_t opt) noexcept;

    /// Unmount a logical drive
    FatResult unmount(const char* path) noexcept;

    // =================================================================
    // File operations
    // =================================================================

    FatResult open(FatFile* fp, const char* path, uint8_t mode) noexcept;
    FatResult close(FatFile* fp) noexcept;
    FatResult read(FatFile* fp, void* buff, uint32_t btr, uint32_t* br) noexcept;
    FatResult write(FatFile* fp, const void* buff, uint32_t btw, uint32_t* bw) noexcept;
    FatResult lseek(FatFile* fp, FSIZE_t ofs) noexcept;
    FatResult truncate(FatFile* fp) noexcept;
    FatResult sync(FatFile* fp) noexcept;

    // =================================================================
    // Directory operations
    // =================================================================

    FatResult opendir(FatDir* dp, const char* path) noexcept;
    FatResult closedir(FatDir* dp) noexcept;
    FatResult readdir(FatDir* dp, FatFileInfo* fno) noexcept;
    FatResult mkdir(const char* path) noexcept;
    FatResult unlink(const char* path) noexcept;
    FatResult rename(const char* path_old, const char* path_new) noexcept;
    FatResult stat(const char* path, FatFileInfo* fno) noexcept;

    // =================================================================
    // Volume management
    // =================================================================

    FatResult getfree(const char* path, uint32_t* nclst, FatFsVolume** fatfs) noexcept;

  private:
    DiskIo* diskio = nullptr;

    // Volume table (single volume)
    FatFsVolume* fat_fs[config::VOLUMES]{};

    // LFN working buffer (static, BSS)
    uint16_t lfn_buf[config::MAX_LFN + 1]{};

    // ---- Internal disk I/O wrappers ----
    uint8_t disk_initialize(uint8_t pdrv) noexcept;
    uint8_t disk_status(uint8_t pdrv) noexcept;
    DiskResult disk_read(uint8_t pdrv, uint8_t* buff, LBA_t sector, uint32_t count) noexcept;
    DiskResult disk_write(uint8_t pdrv, const uint8_t* buff, LBA_t sector, uint32_t count) noexcept;
    DiskResult disk_ioctl(uint8_t pdrv, uint8_t cmd, void* buff) noexcept;

    // ---- Internal FAT helpers (defined in ff.cc) ----
    uint32_t check_fs(FatFsVolume* fs, LBA_t sect) noexcept;
    uint32_t find_volume(FatFsVolume* fs, uint32_t part) noexcept;
    FatResult mount_volume(const char** path, FatFsVolume** rfs, uint8_t mode) noexcept;
    FatResult validate(FatObjId* obj, FatFsVolume** rfs) noexcept;
    FatResult sync_fs(FatFsVolume* fs) noexcept;
    FatResult dir_clear(FatFsVolume* fs, uint32_t clst) noexcept;

    uint32_t get_fat(FatObjId* obj, uint32_t clst) noexcept;
    FatResult put_fat(FatFsVolume* fs, uint32_t clst, uint32_t val) noexcept;
    FatResult remove_chain(FatObjId* obj, uint32_t clst, uint32_t pclst) noexcept;
    uint32_t create_chain(FatObjId* obj, uint32_t clst) noexcept;

    FatResult move_window(FatFsVolume* fs, LBA_t sect) noexcept;
    FatResult sync_window(FatFsVolume* fs) noexcept;
    LBA_t clst2sect(FatFsVolume* fs, uint32_t clst) noexcept;

    FatResult dir_sdi(FatDir* dp, uint32_t ofs) noexcept;
    FatResult dir_next(FatDir* dp, int stretch) noexcept;
    FatResult dir_alloc(FatDir* dp, uint32_t n_ent) noexcept;
    FatResult dir_read(FatDir* dp, int vol) noexcept;
    FatResult dir_find(FatDir* dp) noexcept;
    FatResult dir_register(FatDir* dp) noexcept;
    FatResult dir_remove(FatDir* dp) noexcept;

    void get_fileinfo(FatDir* dp, FatFileInfo* fno) noexcept;
    FatResult follow_path(FatDir* dp, const char* path) noexcept;
    uint32_t ld_clust(FatFsVolume* fs, const uint8_t* dir) noexcept;
    void st_clust(FatFsVolume* fs, uint8_t* dir, uint32_t cl) noexcept;

    // LFN helpers
    int cmp_lfn(const uint16_t* lfnbuf, uint8_t* dir) noexcept;
    int pick_lfn(uint16_t* lfnbuf, uint8_t* dir) noexcept;
    void put_lfn(const uint16_t* lfn, uint8_t* dir, uint8_t ord, uint8_t sum) noexcept;
    void gen_numname(uint8_t* dst, const uint8_t* src, const uint16_t* lfn, uint32_t seq) noexcept;
    uint8_t sum_sfn(const uint8_t* dir) noexcept;
    FatResult create_name(FatDir* dp, const char** path) noexcept;

    uint32_t get_fattime() noexcept;
};

} // namespace umi::fs
