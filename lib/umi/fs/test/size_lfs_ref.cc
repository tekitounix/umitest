// SPDX-License-Identifier: MIT
// Minimal binary to measure reference littlefs C code size on ARM.

#ifdef __clang__
#pragma GCC diagnostic ignored "-Wshorten-64-to-32"
#pragma GCC diagnostic ignored "-Wreturn-type-c-linkage"
#pragma GCC diagnostic ignored "-Wsection"
#endif

#include <common/vector_table.hh>
#include <cstdint>
#include <cstring>

extern "C" {
#include "lfs.h"
}

static constexpr uint32_t BLK_SZ = 256;
static constexpr uint32_t BLK_CNT = 64;
static uint8_t storage[BLK_SZ * BLK_CNT];
static uint8_t rd[BLK_SZ], pr[BLK_SZ], la[16], fb[BLK_SZ];

static int s_read(const lfs_config* c, lfs_block_t b, lfs_off_t o, void* buf, lfs_size_t sz) {
    std::memcpy(buf, &static_cast<uint8_t*>(c->context)[b * BLK_SZ + o], sz);
    return 0;
}
static int s_prog(const lfs_config* c, lfs_block_t b, lfs_off_t o, const void* buf, lfs_size_t sz) {
    std::memcpy(&static_cast<uint8_t*>(c->context)[b * BLK_SZ + o], buf, sz);
    return 0;
}
static int s_erase(const lfs_config* c, lfs_block_t b) {
    std::memset(&static_cast<uint8_t*>(c->context)[b * BLK_SZ], 0xFF, BLK_SZ);
    return 0;
}
static int s_sync(const lfs_config*) { return 0; }

extern "C" [[noreturn]] void _start() {
    std::memset(storage, 0xFF, sizeof(storage));

    lfs_config cfg{};
    cfg.context = storage;
    cfg.read = s_read;
    cfg.prog = s_prog;
    cfg.erase = s_erase;
    cfg.sync = s_sync;
    cfg.read_size = 16;
    cfg.prog_size = 16;
    cfg.block_size = BLK_SZ;
    cfg.block_count = BLK_CNT;
    cfg.cache_size = BLK_SZ;
    cfg.lookahead_size = 16;
    cfg.block_cycles = 500;
    cfg.read_buffer = rd;
    cfg.prog_buffer = pr;
    cfg.lookahead_buffer = la;

    lfs_t lfs{};
    lfs_format(&lfs, &cfg);
    lfs_mount(&lfs, &cfg);

    lfs_file_t f{};
    lfs_file_config fc{};
    fc.buffer = fb;
    lfs_file_opencfg(&lfs, &f, "t.bin", LFS_O_RDWR | LFS_O_CREAT, &fc);
    char data[16] = "hello";
    lfs_file_write(&lfs, &f, data, sizeof(data));
    lfs_file_seek(&lfs, &f, 0, LFS_SEEK_SET);
    lfs_file_read(&lfs, &f, data, sizeof(data));
    lfs_file_truncate(&lfs, &f, 4);
    lfs_file_sync(&lfs, &f);
    lfs_file_close(&lfs, &f);

    lfs_mkdir(&lfs, "dir");
    lfs_info info{};
    lfs_stat(&lfs, "dir", &info);
    lfs_rename(&lfs, "dir", "dir2");
    lfs_remove(&lfs, "dir2");

    lfs_dir_t d{};
    lfs_dir_open(&lfs, &d, "/");
    lfs_dir_read(&lfs, &d, &info);
    lfs_dir_close(&lfs, &d);

    lfs_fs_size(&lfs);
    lfs_unmount(&lfs);

    while (true) { __asm volatile("wfi"); }
}
