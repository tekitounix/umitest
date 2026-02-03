// SPDX-License-Identifier: MIT
// Comprehensive unit tests for FATfs C++23 port
// Tests all public API functions for Phase 0 regression baseline

#include <cstdio>
#include <cstring>
#include <umifs/fat/ff.hh>
#include <umifs/fat/ff_diskio.hh>

#include <umitest.hh>

using namespace umi::fs;
using namespace umitest;

// ============================================================================
// RAM Block Device for testing (simulates SD card with 512-byte sectors)
// ============================================================================

static constexpr uint32_t BLOCK_SIZE = 512;
static constexpr uint32_t BLOCK_COUNT = 2048; // 1MB
static constexpr uint32_t TOTAL_SIZE = BLOCK_SIZE * BLOCK_COUNT;

static uint8_t ram_storage[TOTAL_SIZE];

struct RamBlockDevice {
    int read(uint32_t block, uint32_t offset, void* buffer, uint32_t size) {
        if (block >= BLOCK_COUNT || offset + size > BLOCK_SIZE)
            return -1;
        std::memcpy(buffer, &ram_storage[block * BLOCK_SIZE + offset], size);
        return 0;
    }

    int write(uint32_t block, uint32_t offset, const void* buffer, uint32_t size) {
        if (block >= BLOCK_COUNT || offset + size > BLOCK_SIZE)
            return -1;
        std::memcpy(&ram_storage[block * BLOCK_SIZE + offset], buffer, size);
        return 0;
    }

    int erase(uint32_t block) {
        if (block >= BLOCK_COUNT)
            return -1;
        std::memset(&ram_storage[block * BLOCK_SIZE], 0xFF, BLOCK_SIZE);
        return 0;
    }

    uint32_t block_size() const { return BLOCK_SIZE; }
    uint32_t block_count() const { return BLOCK_COUNT; }
};

static RamBlockDevice ram_dev;

// ============================================================================
// Helper: create a FAT16 formatted image in RAM
// ============================================================================

static void format_fat16_image() {
    std::memset(ram_storage, 0, TOTAL_SIZE);

    uint8_t* bs = ram_storage;

    // Jump instruction
    bs[0] = 0xEB;
    bs[1] = 0x3C;
    bs[2] = 0x90;
    std::memcpy(&bs[3], "MSDOS5.0", 8);
    // Bytes per sector = 512
    bs[11] = 0x00;
    bs[12] = 0x02;
    // Sectors per cluster = 4
    bs[13] = 4;
    // Reserved sectors = 1
    bs[14] = 1;
    bs[15] = 0;
    // Number of FATs = 2
    bs[16] = 2;
    // Root directory entries = 512
    bs[17] = 0x00;
    bs[18] = 0x02;
    // Total sectors (16-bit) = 2048
    bs[19] = 0x00;
    bs[20] = 0x08;
    // Media type
    bs[21] = 0xF8;
    // Sectors per FAT = 2
    bs[22] = 2;
    bs[23] = 0;
    // Sectors per track
    bs[24] = 0x3F;
    bs[25] = 0;
    // Number of heads
    bs[26] = 0xFF;
    bs[27] = 0;
    // Boot signature
    bs[38] = 0x29;
    bs[39] = 0x12;
    bs[40] = 0x34;
    bs[41] = 0x56;
    bs[42] = 0x78;
    std::memcpy(&bs[43], "NO NAME    ", 11);
    std::memcpy(&bs[54], "FAT16   ", 8);
    bs[510] = 0x55;
    bs[511] = 0xAA;

    // Initialize FAT tables
    uint8_t* fat1 = &ram_storage[512];
    fat1[0] = 0xF8;
    fat1[1] = 0xFF;
    fat1[2] = 0xFF;
    fat1[3] = 0xFF;

    uint8_t* fat2 = &ram_storage[512 * 3];
    fat2[0] = 0xF8;
    fat2[1] = 0xFF;
    fat2[2] = 0xFF;
    fat2[3] = 0xFF;
}

// ============================================================================
// Fixture helper
// ============================================================================

struct FatFixture {
    FatFs fs{};
    DiskIo diskio{};
    FatFsVolume vol{};

    void mount() {
        format_fat16_image();
        diskio = make_diskio(ram_dev);
        fs.set_diskio(&diskio);
        fs.mount(&vol, "", 1);
    }

    void unmount() { fs.unmount(""); }
};

// ============================================================================
// Basic: mount, unmount
// ============================================================================

static void test_mount(Suite& s) {
    s.section("FATfs Mount/Unmount");

    FatFixture f;
    f.mount();

    FatResult res = f.fs.unmount("");
    s.check(res == FatResult::OK, "unmount");
}

// ============================================================================
// File: create, write, read
// ============================================================================

static void test_file_create_write_read(Suite& s) {
    s.section("FATfs File Create/Write/Read");

    FatFixture f;
    f.mount();

    FatFile fp{};
    FatResult res = f.fs.open(&fp, "test.txt", FA_WRITE | FA_CREATE_ALWAYS);
    s.check(res == FatResult::OK, "file create");

    const char* msg = "Hello, FATfs!";
    uint32_t bw = 0;
    res = f.fs.write(&fp, msg, std::strlen(msg), &bw);
    s.check(res == FatResult::OK, "write");
    s.check(bw == std::strlen(msg), "bytes written");

    res = f.fs.close(&fp);
    s.check(res == FatResult::OK, "close after write");

    // Read back
    FatFile fp2{};
    res = f.fs.open(&fp2, "test.txt", FA_READ);
    s.check(res == FatResult::OK, "open for read");

    char buf[64]{};
    uint32_t br = 0;
    res = f.fs.read(&fp2, buf, sizeof(buf), &br);
    s.check(res == FatResult::OK, "read");
    s.check(br == std::strlen(msg), "bytes read");
    s.check(std::memcmp(buf, msg, std::strlen(msg)) == 0, "content matches");

    f.fs.close(&fp2);
    f.unmount();
}

// ============================================================================
// File: open modes
// ============================================================================

static void test_open_create_new(Suite& s) {
    s.section("FATfs Open CREATE_NEW");

    FatFixture f;
    f.mount();

    FatFile fp{};
    FatResult res = f.fs.open(&fp, "new.txt", FA_WRITE | FA_CREATE_NEW);
    s.check(res == FatResult::OK, "CREATE_NEW on new file");
    f.fs.close(&fp);

    // Second CREATE_NEW should fail
    res = f.fs.open(&fp, "new.txt", FA_WRITE | FA_CREATE_NEW);
    s.check(res == FatResult::EXIST, "CREATE_NEW on existing file fails");

    f.unmount();
}

static void test_open_always(Suite& s) {
    s.section("FATfs Open OPEN_ALWAYS");

    FatFixture f;
    f.mount();

    FatFile fp{};
    FatResult res = f.fs.open(&fp, "oa.txt", FA_WRITE | FA_OPEN_ALWAYS);
    s.check(res == FatResult::OK, "OPEN_ALWAYS creates new");
    uint32_t bw;
    f.fs.write(&fp, "data", 4, &bw);
    f.fs.close(&fp);

    // Open again, should succeed (existing file)
    res = f.fs.open(&fp, "oa.txt", FA_READ | FA_OPEN_ALWAYS);
    s.check(res == FatResult::OK, "OPEN_ALWAYS opens existing");
    f.fs.close(&fp);

    f.unmount();
}

static void test_open_append(Suite& s) {
    s.section("FATfs Open APPEND");

    FatFixture f;
    f.mount();

    FatFile fp{};
    uint32_t bw;
    f.fs.open(&fp, "app.txt", FA_WRITE | FA_CREATE_ALWAYS);
    f.fs.write(&fp, "AAA", 3, &bw);
    f.fs.close(&fp);

    f.fs.open(&fp, "app.txt", FA_WRITE | FA_OPEN_APPEND);
    f.fs.write(&fp, "BBB", 3, &bw);
    f.fs.close(&fp);

    // Read and verify
    f.fs.open(&fp, "app.txt", FA_READ);
    char buf[16]{};
    uint32_t br;
    f.fs.read(&fp, buf, sizeof(buf), &br);
    s.check(br == 6, "append total bytes");
    s.check(std::memcmp(buf, "AAABBB", 6) == 0, "append content");
    f.fs.close(&fp);

    f.unmount();
}

// ============================================================================
// File: lseek
// ============================================================================

static void test_lseek(Suite& s) {
    s.section("FATfs Lseek");

    FatFixture f;
    f.mount();

    FatFile fp{};
    uint32_t bw;
    f.fs.open(&fp, "seek.bin", FA_WRITE | FA_READ | FA_CREATE_ALWAYS);
    f.fs.write(&fp, "0123456789", 10, &bw);

    // Seek to position 5
    FatResult res = f.fs.lseek(&fp, 5);
    s.check(res == FatResult::OK, "lseek to 5");

    char buf[5]{};
    uint32_t br;
    f.fs.read(&fp, buf, 5, &br);
    s.check(br == 5, "read 5 bytes from pos 5");
    s.check(std::memcmp(buf, "56789", 5) == 0, "content from pos 5");

    // Seek to 0
    f.fs.lseek(&fp, 0);
    f.fs.read(&fp, buf, 3, &br);
    s.check(std::memcmp(buf, "012", 3) == 0, "content from pos 0");

    f.fs.close(&fp);
    f.unmount();
}

// ============================================================================
// File: truncate
// ============================================================================

static void test_truncate(Suite& s) {
    s.section("FATfs Truncate");

    FatFixture f;
    f.mount();

    FatFile fp{};
    uint32_t bw;
    f.fs.open(&fp, "trunc.bin", FA_WRITE | FA_READ | FA_CREATE_ALWAYS);
    f.fs.write(&fp, "0123456789", 10, &bw);

    // Seek to position 5 then truncate
    f.fs.lseek(&fp, 5);
    FatResult res = f.fs.truncate(&fp);
    s.check(res == FatResult::OK, "truncate");

    // Verify size
    f.fs.lseek(&fp, 0);
    char buf[16]{};
    uint32_t br;
    f.fs.read(&fp, buf, sizeof(buf), &br);
    s.check(br == 5, "size after truncate");
    s.check(std::memcmp(buf, "01234", 5) == 0, "content after truncate");

    f.fs.close(&fp);
    f.unmount();
}

// ============================================================================
// File: sync
// ============================================================================

static void test_sync(Suite& s) {
    s.section("FATfs Sync");

    FatFixture f;
    f.mount();

    FatFile fp{};
    uint32_t bw;
    f.fs.open(&fp, "sync.bin", FA_WRITE | FA_CREATE_ALWAYS);
    f.fs.write(&fp, "data", 4, &bw);

    FatResult res = f.fs.sync(&fp);
    s.check(res == FatResult::OK, "sync");

    f.fs.close(&fp);
    f.unmount();
}

// ============================================================================
// Directory: mkdir, stat
// ============================================================================

static void test_mkdir_and_stat(Suite& s) {
    s.section("FATfs Mkdir/Stat");

    FatFixture f;
    f.mount();

    FatResult res = f.fs.mkdir("MYDIR");
    s.check(res == FatResult::OK, "mkdir");

    FatFileInfo fno{};
    res = f.fs.stat("MYDIR", &fno);
    s.check(res == FatResult::OK, "stat dir");
    s.check((fno.fattrib & AM_DIR) != 0, "is directory");

    // stat non-existent
    res = f.fs.stat("NOEXIST", &fno);
    s.check(res != FatResult::OK, "stat non-existent fails");

    f.unmount();
}

// ============================================================================
// Directory: opendir, readdir, closedir
// ============================================================================

static void test_readdir(Suite& s) {
    s.section("FATfs Readdir");

    FatFixture f;
    f.mount();

    FatFile fp{};
    f.fs.open(&fp, "A.TXT", FA_WRITE | FA_CREATE_NEW);
    f.fs.close(&fp);
    f.fs.open(&fp, "B.TXT", FA_WRITE | FA_CREATE_NEW);
    f.fs.close(&fp);
    f.fs.mkdir("SUBDIR");

    FatDir dir{};
    FatResult res = f.fs.opendir(&dir, "");
    s.check(res == FatResult::OK, "opendir root");

    int count = 0;
    FatFileInfo fno{};
    while (f.fs.readdir(&dir, &fno) == FatResult::OK && fno.fname[0] != '\0') {
        count++;
    }
    s.check(count == 3, "root has 3 entries");

    // Rewind by passing nullptr
    res = f.fs.readdir(&dir, nullptr);
    s.check(res == FatResult::OK, "readdir nullptr rewinds");

    // Count again
    count = 0;
    while (f.fs.readdir(&dir, &fno) == FatResult::OK && fno.fname[0] != '\0') {
        count++;
    }
    s.check(count == 3, "after rewind: 3 entries");

    f.fs.closedir(&dir);
    f.unmount();
}

// ============================================================================
// Operations: unlink file
// ============================================================================

static void test_unlink_file(Suite& s) {
    s.section("FATfs Unlink File");

    FatFixture f;
    f.mount();

    FatFile fp{};
    f.fs.open(&fp, "DEL.TXT", FA_WRITE | FA_CREATE_NEW);
    f.fs.close(&fp);

    FatFileInfo fno{};
    s.check(f.fs.stat("DEL.TXT", &fno) == FatResult::OK, "exists before unlink");

    FatResult res = f.fs.unlink("DEL.TXT");
    s.check(res == FatResult::OK, "unlink");

    s.check(f.fs.stat("DEL.TXT", &fno) != FatResult::OK, "gone after unlink");

    f.unmount();
}

// ============================================================================
// Operations: unlink directory
// ============================================================================

static void test_unlink_empty_dir(Suite& s) {
    s.section("FATfs Unlink Empty Dir");

    FatFixture f;
    f.mount();

    f.fs.mkdir("EMPTYDIR");
    FatResult res = f.fs.unlink("EMPTYDIR");
    s.check(res == FatResult::OK, "unlink empty dir");

    f.unmount();
}

static void test_unlink_nonempty_dir_fails(Suite& s) {
    s.section("FATfs Unlink Non-empty Dir Fails");

    FatFixture f;
    f.mount();

    f.fs.mkdir("FULLDIR");
    FatFile fp{};
    f.fs.open(&fp, "FULLDIR/X.TXT", FA_WRITE | FA_CREATE_NEW);
    f.fs.close(&fp);

    FatResult res = f.fs.unlink("FULLDIR");
    s.check(res == FatResult::DENIED, "unlink non-empty dir fails");

    f.unmount();
}

// ============================================================================
// Operations: rename
// ============================================================================

static void test_rename(Suite& s) {
    s.section("FATfs Rename");

    FatFixture f;
    f.mount();

    FatFile fp{};
    uint32_t bw;
    f.fs.open(&fp, "OLD.TXT", FA_WRITE | FA_CREATE_ALWAYS);
    f.fs.write(&fp, "data", 4, &bw);
    f.fs.close(&fp);

    FatResult res = f.fs.rename("OLD.TXT", "NEW.TXT");
    s.check(res == FatResult::OK, "rename");

    FatFileInfo fno{};
    s.check(f.fs.stat("OLD.TXT", &fno) != FatResult::OK, "old name gone");
    s.check(f.fs.stat("NEW.TXT", &fno) == FatResult::OK, "new name exists");
    s.check(fno.fsize == 4, "size preserved");

    f.unmount();
}

// ============================================================================
// Volume: getfree
// ============================================================================

static void test_getfree(Suite& s) {
    s.section("FATfs Getfree");

    FatFixture f;
    f.mount();

    uint32_t nclst = 0;
    FatFsVolume* vol_ptr = nullptr;
    FatResult res = f.fs.getfree("", &nclst, &vol_ptr);
    s.check(res == FatResult::OK, "getfree");
    s.check(nclst > 0, "free clusters > 0");
    s.check(vol_ptr != nullptr, "volume pointer valid");

    f.unmount();
}

// ============================================================================
// LFN: long file names
// ============================================================================

static void test_lfn(Suite& s) {
    s.section("FATfs Long File Names");

    FatFixture f;
    f.mount();

    FatFile fp{};
    uint32_t bw;
    FatResult res = f.fs.open(&fp, "this is a long file name.txt", FA_WRITE | FA_CREATE_ALWAYS);
    s.check(res == FatResult::OK, "create LFN file");
    f.fs.write(&fp, "lfn", 3, &bw);
    f.fs.close(&fp);

    FatFileInfo fno{};
    res = f.fs.stat("this is a long file name.txt", &fno);
    s.check(res == FatResult::OK, "stat LFN file");
    s.check(fno.fsize == 3, "LFN file size");

    f.unmount();
}

// ============================================================================
// Edge case: large file (multi-cluster)
// ============================================================================

static void test_large_file(Suite& s) {
    s.section("FATfs Large File (multi-cluster)");

    FatFixture f;
    f.mount();

    FatFile fp{};
    uint32_t bw;
    f.fs.open(&fp, "big.bin", FA_WRITE | FA_CREATE_ALWAYS);

    // Write 4KB (cluster size = 4 sectors * 512 = 2KB, so 2 clusters)
    uint8_t pattern[512];
    for (int i = 0; i < 512; i++)
        pattern[i] = static_cast<uint8_t>(i & 0xFF);

    for (int i = 0; i < 8; i++) {
        f.fs.write(&fp, pattern, 512, &bw);
        s.check(bw == 512, "write sector");
    }
    f.fs.close(&fp);

    // Read back
    f.fs.open(&fp, "big.bin", FA_READ);
    FatFileInfo fno{};
    f.fs.stat("big.bin", &fno);
    s.check(fno.fsize == 4096, "large file size");

    for (int i = 0; i < 8; i++) {
        uint8_t buf[512]{};
        uint32_t br;
        f.fs.read(&fp, buf, 512, &br);
        s.check(br == 512, "read sector");
        s.check(std::memcmp(buf, pattern, 512) == 0, "sector content matches");
    }
    f.fs.close(&fp);

    f.unmount();
}

// ============================================================================
// Edge case: many files
// ============================================================================

static void test_many_files(Suite& s) {
    s.section("FATfs Many Files");

    FatFixture f;
    f.mount();

    constexpr int N = 20;
    for (int i = 0; i < N; i++) {
        char name[16];
        std::snprintf(name, sizeof(name), "F%02d.TXT", i);
        FatFile fp{};
        f.fs.open(&fp, name, FA_WRITE | FA_CREATE_NEW);
        f.fs.close(&fp);
    }

    // Count
    FatDir dir{};
    f.fs.opendir(&dir, "");
    int count = 0;
    FatFileInfo fno{};
    while (f.fs.readdir(&dir, &fno) == FatResult::OK && fno.fname[0] != '\0') {
        count++;
    }
    s.check(count == N, "all files created");
    f.fs.closedir(&dir);

    f.unmount();
}

// ============================================================================
// Edge case: read-only file access
// ============================================================================

static void test_readonly_deny(Suite& s) {
    s.section("FATfs Read-only Access Denied");

    FatFixture f;
    f.mount();

    FatFile fp{};
    f.fs.open(&fp, "ro.txt", FA_WRITE | FA_CREATE_ALWAYS);
    uint32_t bw;
    f.fs.write(&fp, "data", 4, &bw);
    f.fs.close(&fp);

    // Open read-only, try to write
    f.fs.open(&fp, "ro.txt", FA_READ);
    FatResult res = f.fs.write(&fp, "x", 1, &bw);
    s.check(res == FatResult::DENIED, "write to read-only denied");
    f.fs.close(&fp);

    f.unmount();
}

// ============================================================================
// Edge case: full disk
// ============================================================================

static void test_full_disk(Suite& s) {
    s.section("FATfs Full Disk");

    FatFixture f;
    f.mount();

    int files_created = 0;
    for (int i = 0; i < 200; i++) {
        char name[16];
        std::snprintf(name, sizeof(name), "FI%03d.BIN", i);
        FatFile fp{};
        FatResult res = f.fs.open(&fp, name, FA_WRITE | FA_CREATE_NEW);
        if (res != FatResult::OK)
            break;

        uint8_t data[2048];
        std::memset(data, static_cast<uint8_t>(i), sizeof(data));
        uint32_t bw;
        res = f.fs.write(&fp, data, sizeof(data), &bw);
        f.fs.close(&fp);
        if (bw < sizeof(data))
            break;
        files_created++;
    }
    s.check(files_created > 0, "created files before full");

    // Verify first file
    FatFile fp{};
    f.fs.open(&fp, "FI000.BIN", FA_READ);
    uint8_t buf[1];
    uint32_t br;
    f.fs.read(&fp, buf, 1, &br);
    s.check(br == 1 && buf[0] == 0, "first file readable on full disk");
    f.fs.close(&fp);

    f.unmount();
}

// ============================================================================
// Edge case: boundary writes (sector-aligned)
// ============================================================================

static void test_boundary_writes(Suite& s) {
    s.section("FATfs Boundary Writes");

    FatFixture f;
    f.mount();

    uint8_t data[BLOCK_SIZE + 1];
    for (uint32_t i = 0; i <= BLOCK_SIZE; i++)
        data[i] = static_cast<uint8_t>(i);

    // Exactly one sector
    FatFile fp{};
    uint32_t bw;
    f.fs.open(&fp, "EXACT.BIN", FA_WRITE | FA_CREATE_ALWAYS);
    f.fs.write(&fp, data, BLOCK_SIZE, &bw);
    s.check(bw == BLOCK_SIZE, "write exact sector");
    f.fs.close(&fp);

    // One sector minus 1
    f.fs.open(&fp, "MINUS1.BIN", FA_WRITE | FA_CREATE_ALWAYS);
    f.fs.write(&fp, data, BLOCK_SIZE - 1, &bw);
    s.check(bw == BLOCK_SIZE - 1, "write sector-1");
    f.fs.close(&fp);

    // One sector plus 1
    f.fs.open(&fp, "PLUS1.BIN", FA_WRITE | FA_CREATE_ALWAYS);
    f.fs.write(&fp, data, BLOCK_SIZE + 1, &bw);
    s.check(bw == BLOCK_SIZE + 1, "write sector+1");
    f.fs.close(&fp);

    // Verify sizes
    FatFileInfo fno{};
    f.fs.stat("EXACT.BIN", &fno);
    s.check(fno.fsize == BLOCK_SIZE, "exact size");
    f.fs.stat("MINUS1.BIN", &fno);
    s.check(fno.fsize == BLOCK_SIZE - 1, "minus1 size");
    f.fs.stat("PLUS1.BIN", &fno);
    s.check(fno.fsize == BLOCK_SIZE + 1, "plus1 size");

    f.unmount();
}

// ============================================================================
// Edge case: overwrite existing file
// ============================================================================

static void test_overwrite(Suite& s) {
    s.section("FATfs Overwrite");

    FatFixture f;
    f.mount();

    FatFile fp{};
    uint32_t bw;
    f.fs.open(&fp, "OVER.TXT", FA_WRITE | FA_CREATE_ALWAYS);
    f.fs.write(&fp, "AAAAAAAAAA", 10, &bw);
    f.fs.close(&fp);

    // Overwrite with shorter
    f.fs.open(&fp, "OVER.TXT", FA_WRITE | FA_CREATE_ALWAYS);
    f.fs.write(&fp, "BBB", 3, &bw);
    f.fs.close(&fp);

    f.fs.open(&fp, "OVER.TXT", FA_READ);
    char buf[16]{};
    uint32_t br;
    f.fs.read(&fp, buf, sizeof(buf), &br);
    s.check(br == 3, "overwritten size");
    s.check(std::memcmp(buf, "BBB", 3) == 0, "overwritten content");
    f.fs.close(&fp);

    f.unmount();
}

// ============================================================================
// Edge case: fragmentation
// ============================================================================

static void test_fragmentation(Suite& s) {
    s.section("FATfs Fragmentation");

    FatFixture f;
    f.mount();

    // Create 10 files
    for (int i = 0; i < 10; i++) {
        char name[16];
        std::snprintf(name, sizeof(name), "FR%02d.TXT", i);
        FatFile fp{};
        uint32_t bw;
        f.fs.open(&fp, name, FA_WRITE | FA_CREATE_NEW);
        f.fs.write(&fp, "x", 1, &bw);
        f.fs.close(&fp);
    }

    // Delete odd-numbered
    for (int i = 1; i < 10; i += 2) {
        char name[16];
        std::snprintf(name, sizeof(name), "FR%02d.TXT", i);
        f.fs.unlink(name);
    }

    // Create new files in fragmented space
    for (int i = 0; i < 5; i++) {
        char name[16];
        std::snprintf(name, sizeof(name), "NW%02d.TXT", i);
        FatFile fp{};
        uint32_t bw;
        FatResult res = f.fs.open(&fp, name, FA_WRITE | FA_CREATE_NEW);
        s.check(res == FatResult::OK, "create in fragmented space");
        f.fs.write(&fp, "yy", 2, &bw);
        f.fs.close(&fp);
    }

    // Verify even originals survive
    for (int i = 0; i < 10; i += 2) {
        char name[16];
        std::snprintf(name, sizeof(name), "FR%02d.TXT", i);
        FatFileInfo fno{};
        s.check(f.fs.stat(name, &fno) == FatResult::OK, "original survives");
    }

    f.unmount();
}

// ============================================================================
// Edge case: nested directories
// ============================================================================

static void test_nested_dirs(Suite& s) {
    s.section("FATfs Nested Directories");

    FatFixture f;
    f.mount();

    f.fs.mkdir("A");
    f.fs.mkdir("A/B");
    f.fs.mkdir("A/B/C");

    FatFile fp{};
    uint32_t bw;
    FatResult res = f.fs.open(&fp, "A/B/C/DEEP.TXT", FA_WRITE | FA_CREATE_NEW);
    s.check(res == FatResult::OK, "create in nested dir");
    f.fs.write(&fp, "deep", 4, &bw);
    f.fs.close(&fp);

    FatFileInfo fno{};
    res = f.fs.stat("A/B/C/DEEP.TXT", &fno);
    s.check(res == FatResult::OK, "stat nested file");
    s.check(fno.fsize == 4, "nested file size");

    f.unmount();
}

// ============================================================================
// Interleaved writes to multiple files
// ============================================================================

static void test_interleaved_write(Suite& s) {
    s.section("FATfs Interleaved Write");

    FatFixture f;
    f.mount();

    FatFile fp1{}, fp2{}, fp3{};
    uint32_t bw;
    f.fs.open(&fp1, "IW1.TXT", FA_WRITE | FA_CREATE_ALWAYS);
    f.fs.open(&fp2, "IW2.TXT", FA_WRITE | FA_CREATE_ALWAYS);
    f.fs.open(&fp3, "IW3.TXT", FA_WRITE | FA_CREATE_ALWAYS);

    // Round-robin writes
    for (int i = 0; i < 10; i++) {
        char buf[32];
        int len = std::snprintf(buf, sizeof(buf), "F1-%d,", i);
        f.fs.write(&fp1, buf, static_cast<uint32_t>(len), &bw);
        len = std::snprintf(buf, sizeof(buf), "F2-%d,", i);
        f.fs.write(&fp2, buf, static_cast<uint32_t>(len), &bw);
        len = std::snprintf(buf, sizeof(buf), "F3-%d,", i);
        f.fs.write(&fp3, buf, static_cast<uint32_t>(len), &bw);
    }

    f.fs.close(&fp1);
    f.fs.close(&fp2);
    f.fs.close(&fp3);

    // Verify each file starts with expected prefix
    char rbuf[256]{};
    uint32_t br;
    f.fs.open(&fp1, "IW1.TXT", FA_READ);
    f.fs.read(&fp1, rbuf, sizeof(rbuf), &br);
    s.check(br > 0, "file1 has data");
    s.check(std::memcmp(rbuf, "F1-0,", 5) == 0, "file1 starts correctly");
    f.fs.close(&fp1);

    f.fs.open(&fp2, "IW2.TXT", FA_READ);
    f.fs.read(&fp2, rbuf, sizeof(rbuf), &br);
    s.check(std::memcmp(rbuf, "F2-0,", 5) == 0, "file2 starts correctly");
    f.fs.close(&fp2);

    f.fs.open(&fp3, "IW3.TXT", FA_READ);
    f.fs.read(&fp3, rbuf, sizeof(rbuf), &br);
    s.check(std::memcmp(rbuf, "F3-0,", 5) == 0, "file3 starts correctly");
    f.fs.close(&fp3);

    f.unmount();
}

// ============================================================================
// Seek beyond EOF
// ============================================================================

static void test_seek_beyond_eof(Suite& s) {
    s.section("FATfs Seek Beyond EOF");

    FatFixture f;
    f.mount();

    FatFile fp{};
    uint32_t bw, br;
    f.fs.open(&fp, "SEEK.TXT", FA_WRITE | FA_READ | FA_CREATE_ALWAYS);
    f.fs.write(&fp, "ABCDE", 5, &bw);

    // Seek beyond EOF and write — creates sparse region
    FatResult res = f.fs.lseek(&fp, 100);
    s.check(res == FatResult::OK, "seek beyond eof");

    f.fs.write(&fp, "XY", 2, &bw);
    s.check(bw == 2, "write after seek");

    // Verify file size
    FatFileInfo fno{};
    f.fs.close(&fp);
    f.fs.stat("SEEK.TXT", &fno);
    s.check(fno.fsize == 102, "file size after sparse write");

    // Verify original data preserved
    f.fs.open(&fp, "SEEK.TXT", FA_READ);
    char buf[8]{};
    f.fs.read(&fp, buf, 5, &br);
    s.check(br == 5, "read original data");
    s.check(std::memcmp(buf, "ABCDE", 5) == 0, "original data intact");

    // Read data at offset 100
    f.fs.lseek(&fp, 100);
    char buf2[4]{};
    f.fs.read(&fp, buf2, 2, &br);
    s.check(br == 2, "read at sparse offset");
    s.check(std::memcmp(buf2, "XY", 2) == 0, "data at sparse offset");
    f.fs.close(&fp);

    f.unmount();
}

// ============================================================================
// Write empty buffer (0 bytes)
// ============================================================================

static void test_write_empty_buf(Suite& s) {
    s.section("FATfs Write Empty Buffer");

    FatFixture f;
    f.mount();

    FatFile fp{};
    uint32_t bw;
    f.fs.open(&fp, "EMPTY.TXT", FA_WRITE | FA_CREATE_ALWAYS);

    FatResult res = f.fs.write(&fp, "x", 0, &bw);
    s.check(res == FatResult::OK, "zero-byte write ok");
    s.check(bw == 0, "zero bytes written");

    f.fs.close(&fp);

    FatFileInfo fno{};
    f.fs.stat("EMPTY.TXT", &fno);
    s.check(fno.fsize == 0, "file size is 0");

    f.unmount();
}

// ============================================================================
// Double close
// ============================================================================

static void test_double_close(Suite& s) {
    s.section("FATfs Double Close");

    FatFixture f;
    f.mount();

    FatFile fp{};
    uint32_t bw;
    f.fs.open(&fp, "DC.TXT", FA_WRITE | FA_CREATE_ALWAYS);
    f.fs.write(&fp, "data", 4, &bw);
    FatResult res = f.fs.close(&fp);
    s.check(res == FatResult::OK, "first close succeeds");

    // Second close — validate() detects obj.fs == nullptr and returns error
    res = f.fs.close(&fp);
    s.check(res != FatResult::OK, "second close returns error");

    f.unmount();
}

// ============================================================================
// Remove then create same name
// ============================================================================

static void test_remove_then_create(Suite& s) {
    s.section("FATfs Remove Then Create");

    FatFixture f;
    f.mount();

    FatFile fp{};
    uint32_t bw, br;

    // Create and write
    f.fs.open(&fp, "RECR.TXT", FA_WRITE | FA_CREATE_ALWAYS);
    f.fs.write(&fp, "OLD", 3, &bw);
    f.fs.close(&fp);

    // Remove
    FatResult res = f.fs.unlink("RECR.TXT");
    s.check(res == FatResult::OK, "unlink");

    // Recreate with different data
    f.fs.open(&fp, "RECR.TXT", FA_WRITE | FA_CREATE_ALWAYS);
    f.fs.write(&fp, "NEWDATA", 7, &bw);
    f.fs.close(&fp);

    // Verify new data
    f.fs.open(&fp, "RECR.TXT", FA_READ);
    char buf[16]{};
    f.fs.read(&fp, buf, sizeof(buf), &br);
    s.check(br == 7, "new file size");
    s.check(std::memcmp(buf, "NEWDATA", 7) == 0, "new file content");
    f.fs.close(&fp);

    f.unmount();
}

// ============================================================================
// Overwrite preserves surrounding data
// ============================================================================

static void test_overwrite_preserves_surrounding(Suite& s) {
    s.section("FATfs Overwrite Preserves Surrounding");

    FatFixture f;
    f.mount();

    FatFile fp{};
    uint32_t bw, br;

    // Write "AAAAABBBBBCCCCC" (15 bytes)
    f.fs.open(&fp, "SURR.TXT", FA_WRITE | FA_READ | FA_CREATE_ALWAYS);
    f.fs.write(&fp, "AAAAABBBBBCCCCC", 15, &bw);

    // Seek to offset 5 and overwrite "BBBBB" with "XXXXX"
    f.fs.lseek(&fp, 5);
    f.fs.write(&fp, "XXXXX", 5, &bw);
    s.check(bw == 5, "overwrite middle");

    // Read entire file
    f.fs.lseek(&fp, 0);
    char buf[32]{};
    f.fs.read(&fp, buf, 32, &br);
    s.check(br == 15, "size unchanged");
    s.check(std::memcmp(buf, "AAAAAXXXXXCCCCC", 15) == 0, "surrounding preserved");
    f.fs.close(&fp);

    f.unmount();
}

// ============================================================================
// Multiple writes crossing sector boundary
// ============================================================================

static void test_multi_write_cross_sector(Suite& s) {
    s.section("FATfs Multi Write Cross Sector");

    FatFixture f;
    f.mount();

    FatFile fp{};
    uint32_t bw;

    f.fs.open(&fp, "CROSS.BIN", FA_WRITE | FA_READ | FA_CREATE_ALWAYS);

    // Write 400 bytes, then 300 bytes — crosses 512-byte sector boundary
    uint8_t data1[400];
    std::memset(data1, 0xAA, sizeof(data1));
    f.fs.write(&fp, data1, sizeof(data1), &bw);
    s.check(bw == 400, "first write");

    uint8_t data2[300];
    std::memset(data2, 0xBB, sizeof(data2));
    f.fs.write(&fp, data2, sizeof(data2), &bw);
    s.check(bw == 300, "second write");

    // Verify
    f.fs.lseek(&fp, 0);
    uint8_t rbuf[700]{};
    uint32_t br;
    f.fs.read(&fp, rbuf, 700, &br);
    s.check(br == 700, "read all");

    bool first_ok = true;
    for (uint32_t i = 0; i < 400; i++) {
        if (rbuf[i] != 0xAA) {
            first_ok = false;
            break;
        }
    }
    s.check(first_ok, "first 400 bytes correct");

    bool second_ok = true;
    for (uint32_t i = 400; i < 700; i++) {
        if (rbuf[i] != 0xBB) {
            second_ok = false;
            break;
        }
    }
    s.check(second_ok, "next 300 bytes correct");

    f.fs.close(&fp);
    f.unmount();
}

// ============================================================================
// Multiple writes within same sector
// ============================================================================

static void test_multi_write_same_sector(Suite& s) {
    s.section("FATfs Multi Write Same Sector");

    FatFixture f;
    f.mount();

    FatFile fp{};
    uint32_t bw;

    f.fs.open(&fp, "SAME.BIN", FA_WRITE | FA_READ | FA_CREATE_ALWAYS);

    // Multiple small writes all within one 512-byte sector
    f.fs.write(&fp, "AAAA", 4, &bw);
    s.check(bw == 4, "write 1");
    f.fs.write(&fp, "BBBB", 4, &bw);
    s.check(bw == 4, "write 2");
    f.fs.write(&fp, "CCCC", 4, &bw);
    s.check(bw == 4, "write 3");

    f.fs.lseek(&fp, 0);
    char buf[16]{};
    uint32_t br;
    f.fs.read(&fp, buf, 12, &br);
    s.check(br == 12, "read all");
    s.check(std::memcmp(buf, "AAAABBBBCCCC", 12) == 0, "concatenated correctly");

    f.fs.close(&fp);
    f.unmount();
}

// ============================================================================
// Full disk mkdir
// ============================================================================

static void test_full_disk_mkdir(Suite& s) {
    s.section("FATfs Full Disk Mkdir");

    FatFixture f;
    f.mount();

    // Fill disk with large file first
    FatFile fp{};
    uint32_t bw;
    f.fs.open(&fp, "BIG.BIN", FA_WRITE | FA_CREATE_ALWAYS);
    uint8_t block[2048];
    std::memset(block, 0xFF, sizeof(block));
    int blocks_written = 0;
    for (int i = 0; i < 600; i++) {
        FatResult res = f.fs.write(&fp, block, sizeof(block), &bw);
        if (res != FatResult::OK || bw < sizeof(block))
            break;
        blocks_written++;
    }
    f.fs.close(&fp);
    s.check(blocks_written > 0, "filled disk");

    // Try mkdir on full disk — should fail
    FatResult res = f.fs.mkdir("NEWDIR");
    s.check(res != FatResult::OK, "mkdir on full disk fails");

    f.unmount();
}

// ============================================================================
// Entry point
// ============================================================================

int main() {
    Suite s("fs_fat");

    test_mount(s);
    test_file_create_write_read(s);
    test_open_create_new(s);
    test_open_always(s);
    test_open_append(s);
    test_lseek(s);
    test_truncate(s);
    test_sync(s);
    test_mkdir_and_stat(s);
    test_readdir(s);
    test_unlink_file(s);
    test_unlink_empty_dir(s);
    test_unlink_nonempty_dir_fails(s);
    test_rename(s);
    test_getfree(s);
    test_lfn(s);
    test_large_file(s);
    test_many_files(s);
    test_readonly_deny(s);
    test_full_disk(s);
    test_boundary_writes(s);
    test_overwrite(s);
    test_fragmentation(s);
    test_nested_dirs(s);
    test_interleaved_write(s);
    test_seek_beyond_eof(s);
    test_write_empty_buf(s);
    test_double_close(s);
    test_remove_then_create(s);
    test_overwrite_preserves_surrounding(s);
    test_multi_write_cross_sector(s);
    test_multi_write_same_sector(s);
    test_full_disk_mkdir(s);

    return s.summary();
}
