// SPDX-License-Identifier: MIT
// Minimal binary to measure slimfs code size on ARM.

#ifdef __clang__
#pragma GCC diagnostic ignored "-Wshorten-64-to-32"
#pragma GCC diagnostic ignored "-Wreturn-type-c-linkage"
#pragma GCC diagnostic ignored "-Wsection"
#endif

#include <common/vector_table.hh>
#include <umifs/slim/slim.hh>
#include <umifs/slim/slim_config.hh>
#include <cstring>

static constexpr uint32_t BLK_SZ = 256;
static constexpr uint32_t BLK_CNT = 64;
static uint8_t storage[BLK_SZ * BLK_CNT];
static uint8_t rd[BLK_SZ], pr[BLK_SZ], la[BLK_CNT / 8];

static int s_read(const umi::fs::SlimConfig*, uint32_t b, uint32_t o, void* buf, uint32_t sz) {
    std::memcpy(buf, &storage[b * BLK_SZ + o], sz);
    return 0;
}
static int s_prog(const umi::fs::SlimConfig*, uint32_t b, uint32_t o, const void* buf, uint32_t sz) {
    std::memcpy(&storage[b * BLK_SZ + o], buf, sz);
    return 0;
}
static int s_erase(const umi::fs::SlimConfig*, uint32_t b) {
    std::memset(&storage[b * BLK_SZ], 0xFF, BLK_SZ);
    return 0;
}
static int s_sync(const umi::fs::SlimConfig*) { return 0; }

extern "C" [[noreturn]] void _start() {
    using namespace umi::fs;
    std::memset(storage, 0xFF, sizeof(storage));

    SlimConfig cfg{};
    cfg.read = s_read;
    cfg.prog = s_prog;
    cfg.erase = s_erase;
    cfg.sync = s_sync;
    cfg.read_size = 1;
    cfg.prog_size = 1;
    cfg.block_size = BLK_SZ;
    cfg.block_count = BLK_CNT;
    cfg.read_buffer = {rd, BLK_SZ};
    cfg.prog_buffer = {pr, BLK_SZ};
    cfg.lookahead_buffer = {la, sizeof(la)};

    SlimFs fs{};
    (void)fs.format(&cfg);
    (void)fs.mount(&cfg);

    SlimFile f{};
    (void)fs.file_open(f, "t.bin", SlimOpenFlags::RDWR | SlimOpenFlags::CREAT);
    uint8_t data[16] = {};
    (void)fs.file_write(f, {data, sizeof(data)});
    (void)fs.file_seek(f, 0, SlimWhence::SET);
    (void)fs.file_read(f, {data, sizeof(data)});
    (void)fs.file_truncate(f, 4);
    (void)fs.file_sync(f);
    (void)fs.file_close(f);

    (void)fs.mkdir("dir");
    SlimInfo info{};
    (void)fs.stat("dir", info);
    (void)fs.rename("dir", "dir2");
    (void)fs.remove("dir2");

    SlimDir d{};
    (void)fs.dir_open(d, "/");
    (void)fs.dir_read(d, info);
    (void)fs.dir_close(d);

    (void)fs.fs_size();
    (void)fs.unmount();

    while (true) { __asm volatile("wfi"); }
}
