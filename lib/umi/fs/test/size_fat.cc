// SPDX-License-Identifier: MIT
// Minimal binary to measure FATfs (cleanroom) code size on ARM.

#ifdef __clang__
#pragma GCC diagnostic ignored "-Wshorten-64-to-32"
#pragma GCC diagnostic ignored "-Wreturn-type-c-linkage"
#pragma GCC diagnostic ignored "-Wsection"
#endif

#include <common/vector_table.hh>
#include <umifs/fat/ff.hh>
#include <umifs/fat/ff_diskio.hh>
#include <cstring>

static constexpr uint32_t BLK_SZ = 512;
static constexpr uint32_t BLK_CNT = 128;
static uint8_t storage[BLK_SZ * BLK_CNT];

struct RamDev {
    int read(uint32_t b, uint32_t o, void* buf, uint32_t sz) {
        std::memcpy(buf, &storage[b * BLK_SZ + o], sz);
        return 0;
    }
    int write(uint32_t b, uint32_t o, const void* buf, uint32_t sz) {
        std::memcpy(&storage[b * BLK_SZ + o], buf, sz);
        return 0;
    }
    int erase(uint32_t b) {
        std::memset(&storage[b * BLK_SZ], 0xFF, BLK_SZ);
        return 0;
    }
    uint32_t block_size() const { return BLK_SZ; }
    uint32_t block_count() const { return BLK_CNT; }
};

static RamDev dev;

extern "C" [[noreturn]] void _start() {
    using namespace umi::fs;

    // Format a simple FAT12 image
    std::memset(storage, 0, sizeof(storage));
    uint8_t* bs = storage;
    bs[0] = 0xEB; bs[1] = 0x3C; bs[2] = 0x90;
    std::memcpy(&bs[3], "MSDOS5.0", 8);
    bs[11] = 0x00; bs[12] = 0x02;
    bs[13] = 1; bs[14] = 1; bs[15] = 0; bs[16] = 2;
    bs[17] = 0x40; bs[18] = 0x00;
    bs[19] = 128; bs[20] = 0; bs[21] = 0xF8;
    bs[22] = 1; bs[23] = 0; bs[24] = 0x3F; bs[25] = 0;
    bs[26] = 0xFF; bs[27] = 0; bs[38] = 0x29;
    std::memcpy(&bs[43], "NO NAME    ", 11);
    std::memcpy(&bs[54], "FAT12   ", 8);
    bs[510] = 0x55; bs[511] = 0xAA;
    uint8_t* fat1 = &storage[512];
    fat1[0] = 0xF8; fat1[1] = 0xFF; fat1[2] = 0xFF;
    std::memcpy(&storage[512 * 2], fat1, 3);

    auto dio = make_diskio(dev);
    FatFs fs{};
    fs.set_diskio(&dio);
    FatFsVolume vol{};
    fs.mount(&vol, "", 1);

    FatFile fp{};
    uint32_t bw, br;
    fs.open(&fp, "T.TXT", FA_WRITE | FA_CREATE_ALWAYS);
    char data[16] = "hello";
    fs.write(&fp, data, sizeof(data), &bw);
    fs.lseek(&fp, 0);
    fs.truncate(&fp);
    fs.sync(&fp);
    fs.close(&fp);

    fs.open(&fp, "T.TXT", FA_READ);
    fs.read(&fp, data, sizeof(data), &br);
    fs.close(&fp);

    fs.mkdir("DIR");
    FatFileInfo fno{};
    fs.stat("DIR", &fno);
    fs.rename("DIR", "DIR2");
    fs.unlink("DIR2");

    FatDir dp{};
    fs.opendir(&dp, "");
    fs.readdir(&dp, &fno);
    fs.closedir(&dp);

    uint32_t nclst;
    FatFsVolume* vp;
    fs.getfree("", &nclst, &vp);
    fs.unmount("");

    while (true) { __asm volatile("wfi"); }
}
