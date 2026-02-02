// SPDX-License-Identifier: MIT
// Unified filesystem adapter for benchmark and equivalence testing.
// Wraps 4 FS implementations (lfs_ref, fat, fat_ref, slim) behind
// a common interface so that the same benchmark/test code runs against each.

#pragma once

#include <cstdint>
#include <cstring>

// ============================================================================
// Common storage parameters — all adapters use the same geometry
// ============================================================================

static constexpr uint32_t BENCH_BLOCK_SIZE = 512;
static constexpr uint32_t BENCH_BLOCK_COUNT = 512; // 256KB
static constexpr uint32_t BENCH_TOTAL_SIZE = BENCH_BLOCK_SIZE * BENCH_BLOCK_COUNT;

// ============================================================================
// FsAdapter concept — what every adapter must provide
// ============================================================================
//
//   void init(uint8_t* storage);    // bind to a storage region
//   int  format();                  // format + mount
//   void unmount();
//   int  write_file(const char* path, const void* data, uint32_t size);
//   int  read_file(const char* path, void* buf, uint32_t buf_size, uint32_t* out_size);
//   int  mkdir(const char* path);
//   int  stat(const char* path);    // 0 = exists
//   int  remove(const char* path);
//   int  rename(const char* oldp, const char* newp);
//   const char* name() const;

// ============================================================================
// LfsRefAdapter — reference C littlefs
// ============================================================================

#ifdef FS_ADAPTER_LFS_REF
extern "C" {
#include "lfs.h"
}

struct LfsRefAdapter {
    uint8_t* storage = nullptr;
    lfs_t lfs{};
    lfs_config cfg{};
    uint8_t rd_buf[BENCH_BLOCK_SIZE]{};
    uint8_t pr_buf[BENCH_BLOCK_SIZE]{};
    uint8_t la_buf[16]{};
    uint8_t fi_buf[BENCH_BLOCK_SIZE]{};

    static int s_read(const lfs_config* c, lfs_block_t b, lfs_off_t o, void* buf, lfs_size_t sz) {
        auto* s = static_cast<uint8_t*>(c->context);
        std::memcpy(buf, &s[b * BENCH_BLOCK_SIZE + o], sz);
        return 0;
    }
    static int s_prog(const lfs_config* c, lfs_block_t b, lfs_off_t o, const void* buf, lfs_size_t sz) {
        auto* s = static_cast<uint8_t*>(c->context);
        std::memcpy(&s[b * BENCH_BLOCK_SIZE + o], buf, sz);
        return 0;
    }
    static int s_erase(const lfs_config* c, lfs_block_t b) {
        auto* s = static_cast<uint8_t*>(c->context);
        std::memset(&s[b * BENCH_BLOCK_SIZE], 0xFF, BENCH_BLOCK_SIZE);
        return 0;
    }
    static int s_sync(const lfs_config*) { return 0; }

    void init(uint8_t* s) { storage = s; }

    int format() {
        std::memset(storage, 0xFF, BENCH_TOTAL_SIZE);
        cfg = {};
        cfg.context = storage;
        cfg.read = s_read;
        cfg.prog = s_prog;
        cfg.erase = s_erase;
        cfg.sync = s_sync;
        cfg.read_size = 16;
        cfg.prog_size = 16;
        cfg.block_size = BENCH_BLOCK_SIZE;
        cfg.block_count = BENCH_BLOCK_COUNT;
        cfg.cache_size = BENCH_BLOCK_SIZE;
        cfg.lookahead_size = 16;
        cfg.block_cycles = 500;
        cfg.read_buffer = rd_buf;
        cfg.prog_buffer = pr_buf;
        cfg.lookahead_buffer = la_buf;
        int err = lfs_format(&lfs, &cfg);
        if (err) return err;
        return lfs_mount(&lfs, &cfg);
    }

    void unmount() { lfs_unmount(&lfs); }

    int write_file(const char* path, const void* data, uint32_t size) {
        lfs_file_t f{};
        lfs_file_config fc{};
        fc.buffer = fi_buf;
        int err = lfs_file_opencfg(&lfs, &f, path, LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC, &fc);
        if (err) return err;
        auto n = lfs_file_write(&lfs, &f, data, size);
        lfs_file_close(&lfs, &f);
        return n < 0 ? n : 0;
    }

    int read_file(const char* path, void* buf, uint32_t buf_size, uint32_t* out_size) {
        lfs_file_t f{};
        lfs_file_config fc{};
        fc.buffer = fi_buf;
        int err = lfs_file_opencfg(&lfs, &f, path, LFS_O_RDONLY, &fc);
        if (err) return err;
        auto n = lfs_file_read(&lfs, &f, buf, buf_size);
        lfs_file_close(&lfs, &f);
        if (n < 0) return n;
        if (out_size) *out_size = static_cast<uint32_t>(n);
        return 0;
    }

    int mkdir(const char* path) { return lfs_mkdir(&lfs, path); }

    int stat(const char* path) {
        lfs_info info{};
        return lfs_stat(&lfs, path, &info);
    }

    int remove(const char* path) { return lfs_remove(&lfs, path); }
    int rename(const char* o, const char* n) { return lfs_rename(&lfs, o, n); }
    const char* name() const { return "lfs(ref)"; }
};
#endif // FS_ADAPTER_LFS_REF

// ============================================================================
// FatAdapter — umi::fs::FatFs (cleanroom C++23 port)
// ============================================================================

#ifdef FS_ADAPTER_FAT
#include <umifs/fat/ff.hh>
#include <umifs/fat/ff_diskio.hh>

struct FatRamDev {
    uint8_t* storage = nullptr;
    int read(uint32_t block, uint32_t off, void* buf, uint32_t size) {
        std::memcpy(buf, &storage[block * BENCH_BLOCK_SIZE + off], size);
        return 0;
    }
    int write(uint32_t block, uint32_t off, const void* buf, uint32_t size) {
        std::memcpy(&storage[block * BENCH_BLOCK_SIZE + off], buf, size);
        return 0;
    }
    int erase(uint32_t block) {
        std::memset(&storage[block * BENCH_BLOCK_SIZE], 0xFF, BENCH_BLOCK_SIZE);
        return 0;
    }
    uint32_t block_size() const { return BENCH_BLOCK_SIZE; }
    uint32_t block_count() const { return BENCH_BLOCK_COUNT; }
};

// Format a FAT16 image in the given storage (512B sectors, 512 sectors = 256KB)
static void format_fat16_image(uint8_t* storage) {
    std::memset(storage, 0, BENCH_TOTAL_SIZE);
    uint8_t* bs = storage;
    bs[0] = 0xEB; bs[1] = 0x3C; bs[2] = 0x90;
    std::memcpy(&bs[3], "MSDOS5.0", 8);
    bs[11] = 0x00; bs[12] = 0x02; // bytes per sector = 512
    bs[13] = 1;                     // sectors per cluster = 1
    bs[14] = 1; bs[15] = 0;        // reserved sectors = 1
    bs[16] = 2;                     // number of FATs
    bs[17] = 0x00; bs[18] = 0x02;  // root entries = 512
    bs[19] = 0x00; bs[20] = 0x02;  // total sectors = 512
    bs[21] = 0xF8;                  // media type
    bs[22] = 1; bs[23] = 0;        // sectors per FAT = 1
    bs[24] = 0x3F; bs[25] = 0;
    bs[26] = 0xFF; bs[27] = 0;
    bs[38] = 0x29;
    bs[39] = 0x12; bs[40] = 0x34; bs[41] = 0x56; bs[42] = 0x78;
    std::memcpy(&bs[43], "NO NAME    ", 11);
    std::memcpy(&bs[54], "FAT16   ", 8);
    bs[510] = 0x55; bs[511] = 0xAA;
    // FAT tables
    uint8_t* fat1 = &storage[512];
    fat1[0] = 0xF8; fat1[1] = 0xFF;
    fat1[2] = 0xFF; fat1[3] = 0xFF;
    uint8_t* fat2 = &storage[512 * 2];
    fat2[0] = 0xF8; fat2[1] = 0xFF;
    fat2[2] = 0xFF; fat2[3] = 0xFF;
}

struct FatAdapter {
    FatRamDev dev;
    umi::fs::FatFs fs{};
    umi::fs::DiskIo dio{};
    umi::fs::FatFsVolume vol{};

    void init(uint8_t* storage) { dev.storage = storage; }

    int format() {
        format_fat16_image(dev.storage);
        fs = {};
        dio = umi::fs::make_diskio(dev);
        fs.set_diskio(&dio);
        vol = {};
        auto r = fs.mount(&vol, "", 1);
        return r == umi::fs::FatResult::OK ? 0 : static_cast<int>(r);
    }

    void unmount() { fs.unmount(""); }

    int write_file(const char* path, const void* data, uint32_t size) {
        umi::fs::FatFile fp{};
        auto r = fs.open(&fp, path, umi::fs::FA_WRITE | umi::fs::FA_CREATE_ALWAYS);
        if (r != umi::fs::FatResult::OK) return static_cast<int>(r);
        uint32_t bw;
        r = fs.write(&fp, data, size, &bw);
        fs.close(&fp);
        return r == umi::fs::FatResult::OK ? 0 : static_cast<int>(r);
    }

    int read_file(const char* path, void* buf, uint32_t buf_size, uint32_t* out_size) {
        umi::fs::FatFile fp{};
        auto r = fs.open(&fp, path, umi::fs::FA_READ);
        if (r != umi::fs::FatResult::OK) return static_cast<int>(r);
        uint32_t br;
        r = fs.read(&fp, buf, buf_size, &br);
        fs.close(&fp);
        if (r != umi::fs::FatResult::OK) return static_cast<int>(r);
        if (out_size) *out_size = br;
        return 0;
    }

    int mkdir(const char* path) {
        auto r = fs.mkdir(path);
        return r == umi::fs::FatResult::OK ? 0 : static_cast<int>(r);
    }

    int stat(const char* path) {
        umi::fs::FatFileInfo fno{};
        auto r = fs.stat(path, &fno);
        return r == umi::fs::FatResult::OK ? 0 : static_cast<int>(r);
    }

    int remove(const char* path) {
        auto r = fs.unlink(path);
        return r == umi::fs::FatResult::OK ? 0 : static_cast<int>(r);
    }

    int rename(const char* o, const char* n) {
        auto r = fs.rename(o, n);
        return r == umi::fs::FatResult::OK ? 0 : static_cast<int>(r);
    }

    const char* name() const { return "fat(cr)"; }
};
#endif // FS_ADAPTER_FAT

// ============================================================================
// FatRefAdapter — reference C FATfs
// ============================================================================

#ifdef FS_ADAPTER_FAT_REF

// The reference FATfs uses global disk I/O callbacks.
// We provide them here; they operate on a storage pointer set by the adapter.
namespace fat_ref_detail {
    inline uint8_t* g_storage = nullptr;
}

// Reference C FATfs includes — caller must define FF config macros before
// including this header, or provide ffconf.h via include path.
extern "C" {
#include "ff.h"
#include "diskio.h"

DWORD get_fattime(void);
DSTATUS disk_initialize(BYTE pdrv);
DSTATUS disk_status(BYTE pdrv);
DRESULT disk_read(BYTE pdrv, BYTE* buff, LBA_t sector, UINT count);
DRESULT disk_write(BYTE pdrv, const BYTE* buff, LBA_t sector, UINT count);
DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void* buff);
}

struct FatRefAdapter {
    FATFS vol{};

    void init(uint8_t* storage) { fat_ref_detail::g_storage = storage; }

    int format() {
        format_fat16_image(fat_ref_detail::g_storage);
        vol = {};
        FRESULT r = f_mount(&vol, "", 1);
        return r == FR_OK ? 0 : static_cast<int>(r);
    }

    void unmount() { f_unmount(""); }

    int write_file(const char* path, const void* data, uint32_t size) {
        FIL fp{};
        FRESULT r = f_open(&fp, path, FA_WRITE | FA_CREATE_ALWAYS);
        if (r != FR_OK) return static_cast<int>(r);
        UINT bw;
        r = f_write(&fp, data, size, &bw);
        f_close(&fp);
        return r == FR_OK ? 0 : static_cast<int>(r);
    }

    int read_file(const char* path, void* buf, uint32_t buf_size, uint32_t* out_size) {
        FIL fp{};
        FRESULT r = f_open(&fp, path, FA_READ);
        if (r != FR_OK) return static_cast<int>(r);
        UINT br;
        r = f_read(&fp, buf, buf_size, &br);
        f_close(&fp);
        if (r != FR_OK) return static_cast<int>(r);
        if (out_size) *out_size = br;
        return 0;
    }

    int mkdir(const char* path) { return f_mkdir(path) == FR_OK ? 0 : 1; }

    int stat(const char* path) {
        FILINFO fno{};
        return f_stat(path, &fno) == FR_OK ? 0 : 1;
    }

    int remove(const char* path) { return f_unlink(path) == FR_OK ? 0 : 1; }
    int rename(const char* o, const char* n) { return f_rename(o, n) == FR_OK ? 0 : 1; }
    const char* name() const { return "fat(ref)"; }
};
#endif // FS_ADAPTER_FAT_REF

// ============================================================================
// SlimAdapter — umi::fs::SlimFs
// ============================================================================

#ifdef FS_ADAPTER_SLIM
#include <umifs/slim/slim.hh>
#include <umifs/slim/slim_config.hh>

namespace slim_detail {
    inline uint8_t* g_storage = nullptr;

    inline int s_read(const umi::fs::SlimConfig*, uint32_t b, uint32_t o, void* buf, uint32_t sz) {
        std::memcpy(buf, &g_storage[b * BENCH_BLOCK_SIZE + o], sz);
        return 0;
    }
    inline int s_prog(const umi::fs::SlimConfig*, uint32_t b, uint32_t o, const void* buf, uint32_t sz) {
        std::memcpy(&g_storage[b * BENCH_BLOCK_SIZE + o], buf, sz);
        return 0;
    }
    inline int s_erase(const umi::fs::SlimConfig*, uint32_t b) {
        std::memset(&g_storage[b * BENCH_BLOCK_SIZE], 0xFF, BENCH_BLOCK_SIZE);
        return 0;
    }
    inline int s_sync(const umi::fs::SlimConfig*) { return 0; }
}

struct SlimAdapter {
    umi::fs::SlimFs fs{};
    umi::fs::SlimConfig cfg{};
    uint8_t rd_buf[BENCH_BLOCK_SIZE]{};
    uint8_t pr_buf[BENCH_BLOCK_SIZE]{};
    uint8_t la_buf[BENCH_BLOCK_COUNT / 8]{};

    void init(uint8_t* storage) { slim_detail::g_storage = storage; }

    int format() {
        std::memset(slim_detail::g_storage, 0xFF, BENCH_TOTAL_SIZE);
        cfg = {};
        cfg.read = slim_detail::s_read;
        cfg.prog = slim_detail::s_prog;
        cfg.erase = slim_detail::s_erase;
        cfg.sync = slim_detail::s_sync;
        cfg.read_size = 1;
        cfg.prog_size = 1;
        cfg.block_size = BENCH_BLOCK_SIZE;
        cfg.block_count = BENCH_BLOCK_COUNT;
        cfg.read_buffer = {rd_buf, BENCH_BLOCK_SIZE};
        cfg.prog_buffer = {pr_buf, BENCH_BLOCK_SIZE};
        cfg.lookahead_buffer = {la_buf, sizeof(la_buf)};
        int err = fs.format(&cfg);
        if (err) return err;
        return fs.mount(&cfg);
    }

    void unmount() { (void)fs.unmount(); }

    int write_file(const char* path, const void* data, uint32_t size) {
        umi::fs::SlimFile f{};
        int err = fs.file_open(f, path,
            umi::fs::SlimOpenFlags::WRONLY | umi::fs::SlimOpenFlags::CREAT | umi::fs::SlimOpenFlags::TRUNC);
        if (err) return err;
        auto span = std::span<const uint8_t>(static_cast<const uint8_t*>(data), size);
        err = fs.file_write(f, span);
        (void)fs.file_close(f);
        return err < 0 ? err : 0;
    }

    int read_file(const char* path, void* buf, uint32_t buf_size, uint32_t* out_size) {
        umi::fs::SlimFile f{};
        int err = fs.file_open(f, path, umi::fs::SlimOpenFlags::RDONLY);
        if (err) return err;
        auto span = std::span<uint8_t>(static_cast<uint8_t*>(buf), buf_size);
        err = fs.file_read(f, span);
        (void)fs.file_close(f);
        if (err < 0) return err;
        if (out_size) *out_size = static_cast<uint32_t>(err);
        return 0;
    }

    int mkdir(const char* path) { return fs.mkdir(path); }

    int stat(const char* path) {
        umi::fs::SlimInfo info{};
        return fs.stat(path, info);
    }

    int remove(const char* path) { return fs.remove(path); }
    int rename(const char* o, const char* n) { return fs.rename(o, n); }
    const char* name() const { return "slim"; }
};
#endif // FS_ADAPTER_SLIM
