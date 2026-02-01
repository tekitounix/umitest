// SPDX-License-Identifier: MIT
// Unit tests for FATfs C++23 port

#include "test_common.hh"

#include <umios/fs/fatfs/ff.hh>
#include <umios/fs/fatfs/ff_diskio.hh>
#include <cstring>

using namespace umi::fs::fat;

// ============================================================================
// RAM Block Device for testing (simulates SD card with 512-byte sectors)
// ============================================================================

static constexpr uint32_t BLOCK_SIZE = 512;
static constexpr uint32_t BLOCK_COUNT = 2048;  // 1MB
static constexpr uint32_t TOTAL_SIZE = BLOCK_SIZE * BLOCK_COUNT;

// 1MB RAM disk
static uint8_t ram_storage[TOTAL_SIZE];

struct RamBlockDevice {
    int read(uint32_t block, uint32_t offset, void* buffer, uint32_t size) {
        if (block >= BLOCK_COUNT || offset + size > BLOCK_SIZE) {
            return -1;
        }
        std::memcpy(buffer, &ram_storage[block * BLOCK_SIZE + offset], size);
        return 0;
    }

    int write(uint32_t block, uint32_t offset, const void* buffer, uint32_t size) {
        if (block >= BLOCK_COUNT || offset + size > BLOCK_SIZE) {
            return -1;
        }
        std::memcpy(&ram_storage[block * BLOCK_SIZE + offset], buffer, size);
        return 0;
    }

    int erase(uint32_t block) {
        if (block >= BLOCK_COUNT) {
            return -1;
        }
        std::memset(&ram_storage[block * BLOCK_SIZE], 0xFF, BLOCK_SIZE);
        return 0;
    }

    uint32_t block_size() const { return BLOCK_SIZE; }
    uint32_t block_count() const { return BLOCK_COUNT; }
};

static RamBlockDevice ram_dev;

// ============================================================================
// Helper: create a FAT12/16 formatted image in RAM
// Since FF_USE_MKFS=0 we need to write a minimal FAT boot sector manually
// ============================================================================

static void format_fat16_image() {
    std::memset(ram_storage, 0, TOTAL_SIZE);

    // Minimal FAT16 boot sector
    uint8_t* bs = ram_storage;

    // Jump instruction
    bs[0] = 0xEB; bs[1] = 0x3C; bs[2] = 0x90;
    // OEM name
    std::memcpy(&bs[3], "MSDOS5.0", 8);
    // Bytes per sector = 512
    bs[11] = 0x00; bs[12] = 0x02;
    // Sectors per cluster = 4
    bs[13] = 4;
    // Reserved sectors = 1
    bs[14] = 1; bs[15] = 0;
    // Number of FATs = 2
    bs[16] = 2;
    // Root directory entries = 512
    bs[17] = 0x00; bs[18] = 0x02;
    // Total sectors (16-bit) = 2048
    bs[19] = 0x00; bs[20] = 0x08;
    // Media type
    bs[21] = 0xF8;
    // Sectors per FAT = 2
    bs[22] = 2; bs[23] = 0;
    // Sectors per track
    bs[24] = 0x3F; bs[25] = 0;
    // Number of heads
    bs[26] = 0xFF; bs[27] = 0;
    // Hidden sectors = 0
    bs[28] = 0; bs[29] = 0; bs[30] = 0; bs[31] = 0;
    // Total sectors (32-bit) = 0 (using 16-bit field)
    bs[32] = 0; bs[33] = 0; bs[34] = 0; bs[35] = 0;
    // Drive number
    bs[36] = 0x80;
    // Boot signature
    bs[38] = 0x29;
    // Volume serial
    bs[39] = 0x12; bs[40] = 0x34; bs[41] = 0x56; bs[42] = 0x78;
    // Volume label
    std::memcpy(&bs[43], "NO NAME    ", 11);
    // FS type
    std::memcpy(&bs[54], "FAT16   ", 8);
    // Boot signature
    bs[510] = 0x55; bs[511] = 0xAA;

    // Initialize FAT tables
    // FAT1 starts at sector 1
    uint8_t* fat1 = &ram_storage[512];
    fat1[0] = 0xF8; fat1[1] = 0xFF;  // Media byte + 0xFF
    fat1[2] = 0xFF; fat1[3] = 0xFF;  // End of cluster chain marker

    // FAT2 starts at sector 3
    uint8_t* fat2 = &ram_storage[512 * 3];
    fat2[0] = 0xF8; fat2[1] = 0xFF;
    fat2[2] = 0xFF; fat2[3] = 0xFF;
}

// ============================================================================
// Tests
// ============================================================================

static void test_mount() {
    SECTION("FATfs Mount");

    format_fat16_image();

    FatFs fs;
    auto diskio = make_diskio(ram_dev);
    fs.set_diskio(&diskio);

    FatFsVolume vol{};
    FatResult res = fs.mount(&vol, "", 1);
    CHECK(res == FatResult::OK, "mount succeeds");

    res = fs.unmount("");
    CHECK(res == FatResult::OK, "unmount succeeds");
}

static void test_file_create_write_read() {
    SECTION("FATfs File Create/Write/Read");

    format_fat16_image();

    FatFs fs;
    auto diskio = make_diskio(ram_dev);
    fs.set_diskio(&diskio);

    FatFsVolume vol{};
    fs.mount(&vol, "", 1);

    // Create and write
    FatFile fp{};
    FatResult res = fs.open(&fp, "test.txt", FA_WRITE | FA_CREATE_ALWAYS);
    CHECK(res == FatResult::OK, "file create");

    const char* msg = "Hello, FATfs!";
    uint32_t bw = 0;
    res = fs.write(&fp, msg, std::strlen(msg), &bw);
    CHECK(res == FatResult::OK, "file write");
    CHECK(bw == std::strlen(msg), "bytes written");

    res = fs.close(&fp);
    CHECK(res == FatResult::OK, "file close after write");

    // Read back
    FatFile fp2{};
    res = fs.open(&fp2, "test.txt", FA_READ);
    CHECK(res == FatResult::OK, "file open for read");

    char buf[64]{};
    uint32_t br = 0;
    res = fs.read(&fp2, buf, sizeof(buf), &br);
    CHECK(res == FatResult::OK, "file read");
    CHECK(br == std::strlen(msg), "bytes read");
    CHECK(std::memcmp(buf, msg, std::strlen(msg)) == 0, "content matches");

    fs.close(&fp2);
    fs.unmount("");
}

static void test_mkdir_and_stat() {
    SECTION("FATfs Mkdir and Stat");

    format_fat16_image();

    FatFs fs;
    auto diskio = make_diskio(ram_dev);
    fs.set_diskio(&diskio);

    FatFsVolume vol{};
    fs.mount(&vol, "", 1);

    FatResult res = fs.mkdir("MYDIR");
    CHECK(res == FatResult::OK, "mkdir");

    FatFileInfo fno{};
    res = fs.stat("MYDIR", &fno);
    CHECK(res == FatResult::OK, "stat");
    CHECK((fno.fattrib & AM_DIR) != 0, "is directory");

    fs.unmount("");
}

static void test_readdir() {
    SECTION("FATfs Readdir");

    format_fat16_image();

    FatFs fs;
    auto diskio = make_diskio(ram_dev);
    fs.set_diskio(&diskio);

    FatFsVolume vol{};
    fs.mount(&vol, "", 1);

    // Create some entries
    FatFile fp{};
    fs.open(&fp, "A.TXT", FA_WRITE | FA_CREATE_NEW);
    fs.close(&fp);
    fs.open(&fp, "B.TXT", FA_WRITE | FA_CREATE_NEW);
    fs.close(&fp);
    fs.mkdir("SUBDIR");

    // Read directory
    FatDir dir{};
    FatResult res = fs.opendir(&dir, "");
    CHECK(res == FatResult::OK, "opendir root");

    int count = 0;
    FatFileInfo fno{};
    while (fs.readdir(&dir, &fno) == FatResult::OK && fno.fname[0] != '\0') {
        count++;
    }
    CHECK(count == 3, "root has 3 entries (A.TXT, B.TXT, SUBDIR)");

    fs.closedir(&dir);
    fs.unmount("");
}

static void test_unlink() {
    SECTION("FATfs Unlink");

    format_fat16_image();

    FatFs fs;
    auto diskio = make_diskio(ram_dev);
    fs.set_diskio(&diskio);

    FatFsVolume vol{};
    fs.mount(&vol, "", 1);

    FatFile fp{};
    fs.open(&fp, "DEL.TXT", FA_WRITE | FA_CREATE_NEW);
    fs.close(&fp);

    FatFileInfo fno{};
    FatResult res = fs.stat("DEL.TXT", &fno);
    CHECK(res == FatResult::OK, "file exists before unlink");

    res = fs.unlink("DEL.TXT");
    CHECK(res == FatResult::OK, "unlink");

    res = fs.stat("DEL.TXT", &fno);
    CHECK(res != FatResult::OK, "file gone after unlink");

    fs.unmount("");
}

int main() {
    test_mount();
    test_file_create_write_read();
    test_mkdir_and_stat();
    test_readdir();
    test_unlink();

    TEST_SUMMARY();
}
