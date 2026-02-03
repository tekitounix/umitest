// SPDX-License-Identifier: MIT
// Comparison tests: C++23 port (umi::fs::fat) vs reference FatFs C implementation
// Verifies functional equivalence and measures performance delta

#include <umitest.hh>
using namespace umitest;

// --- C++23 port ---
#include <umifs/fat/ff.hh>
#include <umifs/fat/ff_diskio.hh>

// --- Reference C implementation ---
// We need custom ffconf.h for reference — override with defines before include
#define FFCONF_DEF 80386
#define FF_FS_READONLY 0
#define FF_FS_MINIMIZE 0
#define FF_USE_FIND 0
#define FF_USE_MKFS 0
#define FF_USE_FASTSEEK 0
#define FF_USE_EXPAND 0
#define FF_USE_CHMOD 0
#define FF_USE_LABEL 0
#define FF_USE_FORWARD 0
#define FF_USE_STRFUNC 0
#define FF_PRINT_LLI 0
#define FF_PRINT_FLOAT 0
#define FF_STRF_ENCODE 0
#define FF_CODE_PAGE 437
#define FF_USE_LFN 0
#define FF_MAX_LFN 255
#define FF_LFN_UNICODE 0
#define FF_LFN_BUF 255
#define FF_SFN_BUF 12
#define FF_FS_RPATH 0
#define FF_VOLUMES 1
#define FF_STR_VOLUME_ID 0
#define FF_MULTI_PARTITION 0
#define FF_MIN_SS 512
#define FF_MAX_SS 512
#define FF_LBA64 0
#define FF_MIN_GPT 0x10000000
#define FF_USE_TRIM 0
#define FF_FS_TINY 0
#define FF_FS_EXFAT 0
#define FF_FS_NORTC 1
#define FF_NORTC_MON 1
#define FF_NORTC_MDAY 1
#define FF_NORTC_YEAR 2025
#define FF_FS_NOFSINFO 0
#define FF_FS_LOCK 0
#define FF_FS_REENTRANT 0
#define FF_FS_TIMEOUT 1000
#define FF_PATH_DEPTH 10
#define FF_FS_CRTIME 0

extern "C" {
// Prevent including ffconf.h from ff.h by defining FF_DEFINED
// This is tricky — we include ff.h with our defines already active
#include "ff.h"
#include "diskio.h"
}

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// ============================================================================
// Shared constants
// ============================================================================

static constexpr uint32_t SECTOR_SIZE = 512;
static constexpr uint32_t SECTOR_COUNT = 2048; // 1MB
static constexpr uint32_t TOTAL_SIZE = SECTOR_SIZE * SECTOR_COUNT;

static uint8_t storage_port[TOTAL_SIZE];
static uint8_t storage_ref[TOTAL_SIZE];

// ============================================================================
// FAT16 image formatter (shared between both implementations)
// ============================================================================

static void format_fat16(uint8_t* storage) {
    std::memset(storage, 0, TOTAL_SIZE);

    uint8_t* bs = storage;
    bs[0] = 0xEB; bs[1] = 0x3C; bs[2] = 0x90;
    std::memcpy(&bs[3], "MSDOS5.0", 8);
    bs[11] = 0x00; bs[12] = 0x02; // bytes per sector = 512
    bs[13] = 4;                     // sectors per cluster
    bs[14] = 1; bs[15] = 0;        // reserved sectors
    bs[16] = 2;                     // number of FATs
    bs[17] = 0x00; bs[18] = 0x02;  // root entries = 512
    bs[19] = 0x00; bs[20] = 0x08;  // total sectors = 2048
    bs[21] = 0xF8;                  // media type
    bs[22] = 2; bs[23] = 0;        // sectors per FAT
    bs[24] = 0x3F; bs[25] = 0;     // sectors per track
    bs[26] = 0xFF; bs[27] = 0;     // heads
    bs[38] = 0x29;                  // boot signature
    bs[39] = 0x12; bs[40] = 0x34; bs[41] = 0x56; bs[42] = 0x78;
    std::memcpy(&bs[43], "NO NAME    ", 11);
    std::memcpy(&bs[54], "FAT16   ", 8);
    bs[510] = 0x55; bs[511] = 0xAA;

    // FAT tables
    uint8_t* fat1 = &storage[512];
    fat1[0] = 0xF8; fat1[1] = 0xFF;
    fat1[2] = 0xFF; fat1[3] = 0xFF;
    uint8_t* fat2 = &storage[512 * 3];
    fat2[0] = 0xF8; fat2[1] = 0xFF;
    fat2[2] = 0xFF; fat2[3] = 0xFF;
}

// ============================================================================
// Port helpers
// ============================================================================

struct PortBlockDev {
    uint8_t* storage;

    int read(uint32_t block, uint32_t offset, void* buf, uint32_t size) {
        std::memcpy(buf, &storage[block * SECTOR_SIZE + offset], size);
        return 0;
    }
    int write(uint32_t block, uint32_t offset, const void* buf, uint32_t size) {
        std::memcpy(&storage[block * SECTOR_SIZE + offset], buf, size);
        return 0;
    }
    int erase(uint32_t block) {
        std::memset(&storage[block * SECTOR_SIZE], 0xFF, SECTOR_SIZE);
        return 0;
    }
    uint32_t block_size() const { return SECTOR_SIZE; }
    uint32_t block_count() const { return SECTOR_COUNT; }
};

static PortBlockDev port_dev{storage_port};

// ============================================================================
// Reference C implementation — disk I/O callbacks
// Must implement disk_initialize, disk_status, disk_read, disk_write, disk_ioctl
// ============================================================================

extern "C" {

// Stub for get_fattime (reference ff.c requires this when FF_FS_NORTC == 0)
DWORD get_fattime(void) {
    return ((DWORD)(2025 - 1980) << 25) | ((DWORD)1 << 21) | ((DWORD)1 << 16);
}

DSTATUS disk_initialize(BYTE /*pdrv*/) { return 0; }
DSTATUS disk_status(BYTE /*pdrv*/) { return 0; }

DRESULT disk_read(BYTE /*pdrv*/, BYTE* buff, LBA_t sector, UINT count) {
    std::memcpy(buff, &storage_ref[sector * SECTOR_SIZE], count * SECTOR_SIZE);
    return RES_OK;
}

DRESULT disk_write(BYTE /*pdrv*/, const BYTE* buff, LBA_t sector, UINT count) {
    std::memcpy(&storage_ref[sector * SECTOR_SIZE], buff, count * SECTOR_SIZE);
    return RES_OK;
}

DRESULT disk_ioctl(BYTE /*pdrv*/, BYTE cmd, void* buff) {
    switch (cmd) {
    case CTRL_SYNC: return RES_OK;
    case GET_SECTOR_COUNT: *static_cast<LBA_t*>(buff) = SECTOR_COUNT; return RES_OK;
    case GET_SECTOR_SIZE: *static_cast<WORD*>(buff) = SECTOR_SIZE; return RES_OK;
    case GET_BLOCK_SIZE: *static_cast<DWORD*>(buff) = 1; return RES_OK;
    default: return RES_PARERR;
    }
}

} // extern "C"

// ============================================================================
// Timer
// ============================================================================

struct Timer {
    using clock = std::chrono::high_resolution_clock;
    clock::time_point start;
    Timer() : start(clock::now()) {}
    double elapsed_us() const {
        return std::chrono::duration<double, std::micro>(clock::now() - start).count();
    }
};

static void report_perf(const char* op, double port_us, double ref_us) {
    double ratio = (ref_us > 0.0) ? port_us / ref_us : 0.0;
    std::printf("  [perf] %-30s  port: %8.1f us  ref: %8.1f us  ratio: %.2fx\n", op, port_us, ref_us, ratio);
}

// ============================================================================
// Test: mount comparison
// ============================================================================

static void test_mount_compare(Suite& s) {
    s.section("Compare: Mount");

    // Port
    format_fat16(storage_port);
    umi::fs::FatFs pfs{};
    umi::fs::DiskIo pdio = umi::fs::make_diskio(port_dev);
    pfs.set_diskio(&pdio);
    umi::fs::FatFsVolume pvol{};
    auto pres = pfs.mount(&pvol, "", 1);
    s.check(pres == umi::fs::FatResult::OK, "port mount");
    pfs.unmount("");

    // Reference
    format_fat16(storage_ref);
    FATFS rvol{};
    FRESULT rres = f_mount(&rvol, "", 1);
    s.check(rres == FR_OK, "ref mount");
    f_unmount("");
}

// ============================================================================
// Test: file write/read equivalence
// ============================================================================

static void test_file_rw_compare(Suite& s) {
    s.section("Compare: File Write/Read");

    const char* data = "Hello, FATfs comparison!";
    auto len = std::strlen(data);

    // Port
    format_fat16(storage_port);
    umi::fs::FatFs pfs{};
    umi::fs::DiskIo pdio = umi::fs::make_diskio(port_dev);
    pfs.set_diskio(&pdio);
    umi::fs::FatFsVolume pvol{};
    pfs.mount(&pvol, "", 1);

    umi::fs::FatFile pfp{};
    pfs.open(&pfp, "TEST.TXT", FA_WRITE | FA_CREATE_ALWAYS);
    uint32_t pbw;
    pfs.write(&pfp, data, len, &pbw);
    pfs.close(&pfp);

    pfs.open(&pfp, "TEST.TXT", FA_READ);
    char pbuf[64]{};
    uint32_t pbr;
    pfs.read(&pfp, pbuf, sizeof(pbuf), &pbr);
    pfs.close(&pfp);
    pfs.unmount("");

    // Reference
    format_fat16(storage_ref);
    FATFS rvol{};
    f_mount(&rvol, "", 1);

    FIL rfp{};
    f_open(&rfp, "TEST.TXT", FA_WRITE | FA_CREATE_ALWAYS);
    UINT rbw;
    f_write(&rfp, data, len, &rbw);
    f_close(&rfp);

    f_open(&rfp, "TEST.TXT", FA_READ);
    char rbuf[64]{};
    UINT rbr;
    f_read(&rfp, rbuf, sizeof(rbuf), &rbr);
    f_close(&rfp);
    f_unmount("");

    s.check(pbr == rbr, "bytes read match");
    s.check(pbw == rbw, "bytes written match");
    s.check(std::memcmp(pbuf, rbuf, pbr) == 0, "file content matches between port and ref");
    s.check(std::memcmp(pbuf, data, len) == 0, "data is correct");
}

// ============================================================================
// Test: on-disk format compatibility (port write → ref read)
// ============================================================================

static void test_cross_port_to_ref(Suite& s) {
    s.section("Compare: Cross-read (port → ref)");

    // Write with port
    format_fat16(storage_port);
    umi::fs::FatFs pfs{};
    umi::fs::DiskIo pdio = umi::fs::make_diskio(port_dev);
    pfs.set_diskio(&pdio);
    umi::fs::FatFsVolume pvol{};
    pfs.mount(&pvol, "", 1);

    umi::fs::FatFile pfp{};
    pfs.open(&pfp, "CROSS.TXT", FA_WRITE | FA_CREATE_ALWAYS);
    const char* msg = "port-to-ref";
    uint32_t bw;
    pfs.write(&pfp, msg, std::strlen(msg), &bw);
    pfs.close(&pfp);
    pfs.unmount("");

    // Copy storage
    std::memcpy(storage_ref, storage_port, TOTAL_SIZE);

    // Read with reference
    FATFS rvol{};
    FRESULT res = f_mount(&rvol, "", 1);
    s.check(res == FR_OK, "ref mounts port image");

    FIL rfp{};
    res = f_open(&rfp, "CROSS.TXT", FA_READ);
    s.check(res == FR_OK, "ref opens port file");

    char rbuf[32]{};
    UINT br;
    f_read(&rfp, rbuf, sizeof(rbuf), &br);
    s.check(br == std::strlen(msg), "ref reads correct size");
    s.check(std::memcmp(rbuf, msg, std::strlen(msg)) == 0, "ref reads correct data");
    f_close(&rfp);
    f_unmount("");
}

// ============================================================================
// Test: on-disk format compatibility (ref write → port read)
// ============================================================================

static void test_cross_ref_to_port(Suite& s) {
    s.section("Compare: Cross-read (ref → port)");

    // Write with reference
    format_fat16(storage_ref);
    FATFS rvol{};
    f_mount(&rvol, "", 1);

    FIL rfp{};
    f_open(&rfp, "CROSS.TXT", FA_WRITE | FA_CREATE_ALWAYS);
    const char* msg = "ref-to-port";
    UINT bw;
    f_write(&rfp, msg, std::strlen(msg), &bw);
    f_close(&rfp);
    f_unmount("");

    // Copy storage
    std::memcpy(storage_port, storage_ref, TOTAL_SIZE);

    // Read with port
    umi::fs::FatFs pfs{};
    umi::fs::DiskIo pdio = umi::fs::make_diskio(port_dev);
    pfs.set_diskio(&pdio);
    umi::fs::FatFsVolume pvol{};
    auto pres = pfs.mount(&pvol, "", 1);
    s.check(pres == umi::fs::FatResult::OK, "port mounts ref image");

    umi::fs::FatFile pfp{};
    pres = pfs.open(&pfp, "CROSS.TXT", FA_READ);
    s.check(pres == umi::fs::FatResult::OK, "port opens ref file");

    char pbuf[32]{};
    uint32_t br;
    pfs.read(&pfp, pbuf, sizeof(pbuf), &br);
    s.check(br == std::strlen(msg), "port reads correct size");
    s.check(std::memcmp(pbuf, msg, std::strlen(msg)) == 0, "port reads correct data");
    pfs.close(&pfp);
    pfs.unmount("");
}

// ============================================================================
// Test: directory operations equivalence
// ============================================================================

static void test_dir_compare(Suite& s) {
    s.section("Compare: Directory operations");

    // Port
    format_fat16(storage_port);
    umi::fs::FatFs pfs{};
    umi::fs::DiskIo pdio = umi::fs::make_diskio(port_dev);
    pfs.set_diskio(&pdio);
    umi::fs::FatFsVolume pvol{};
    pfs.mount(&pvol, "", 1);
    pfs.mkdir("DIR1");
    pfs.mkdir("DIR2");
    {
        umi::fs::FatFile fp{};
        pfs.open(&fp, "FILE.TXT", FA_WRITE | FA_CREATE_NEW);
        pfs.close(&fp);
    }
    constexpr int EXPECT_ENTRIES = 3; // "DIR1", "DIR2", "FILE.TXT"
    char pnames[EXPECT_ENTRIES][16]{};
    uint8_t pattrs[EXPECT_ENTRIES]{};
    int pcount = 0;
    {
        umi::fs::FatDir dir{};
        pfs.opendir(&dir, "");
        umi::fs::FatFileInfo fno{};
        while (pfs.readdir(&dir, &fno) == umi::fs::FatResult::OK && fno.fname[0] != '\0'
               && pcount < EXPECT_ENTRIES) {
            std::strncpy(pnames[pcount], fno.fname, 15);
            pattrs[pcount] = fno.fattrib;
            pcount++;
        }
        pfs.closedir(&dir);
    }
    pfs.unmount("");

    // Reference
    format_fat16(storage_ref);
    FATFS rvol{};
    f_mount(&rvol, "", 1);
    f_mkdir("DIR1");
    f_mkdir("DIR2");
    {
        FIL fp{};
        f_open(&fp, "FILE.TXT", FA_WRITE | FA_CREATE_NEW);
        f_close(&fp);
    }
    char rnames[EXPECT_ENTRIES][16]{};
    uint8_t rattrs[EXPECT_ENTRIES]{};
    int rcount = 0;
    {
        DIR dir{};
        FILINFO fno{};
        f_opendir(&dir, "");
        while (f_readdir(&dir, &fno) == FR_OK && fno.fname[0] != '\0' && rcount < EXPECT_ENTRIES) {
            std::strncpy(rnames[rcount], fno.fname, 15);
            rattrs[rcount] = fno.fattrib;
            rcount++;
        }
        f_closedir(&dir);
    }
    f_unmount("");

    s.check(pcount == rcount, "directory entry count matches");
    s.check(pcount == EXPECT_ENTRIES, "both have 3 entries");
    for (int i = 0; i < pcount && i < rcount; i++) {
        s.check(std::strcmp(pnames[i], rnames[i]) == 0, "entry name matches");
        s.check((pattrs[i] & 0x10) == (rattrs[i] & 0x10), "entry dir attribute matches");
    }
}

// ============================================================================
// Performance: file write
// ============================================================================

static void test_perf_write(Suite& s) {
    s.section("Perf: File write 4KB");

    constexpr int ITERATIONS = 30;
    constexpr int WARMUP = 2;
    uint8_t pattern[512];
    std::srand(42);
    for (int i = 0; i < 512; i++) pattern[i] = static_cast<uint8_t>(std::rand() & 0xFF);

    // Port
    double port_total = 0;
    for (int iter = 0; iter < WARMUP + ITERATIONS; iter++) {
        format_fat16(storage_port);
        umi::fs::FatFs pfs{};
        umi::fs::DiskIo pdio = umi::fs::make_diskio(port_dev);
        pfs.set_diskio(&pdio);
        umi::fs::FatFsVolume pvol{};
        pfs.mount(&pvol, "", 1);

        umi::fs::FatFile fp{};
        Timer t;
        pfs.open(&fp, "BENCH.BIN", FA_WRITE | FA_CREATE_ALWAYS);
        for (int s = 0; s < 8; s++) {
            uint32_t bw;
            pfs.write(&fp, pattern, 512, &bw);
        }
        pfs.close(&fp);
        if (iter >= WARMUP) port_total += t.elapsed_us();
        pfs.unmount("");
    }

    // Reference
    double ref_total = 0;
    for (int iter = 0; iter < WARMUP + ITERATIONS; iter++) {
        format_fat16(storage_ref);
        FATFS rvol{};
        f_mount(&rvol, "", 1);

        FIL fp{};
        Timer t;
        f_open(&fp, "BENCH.BIN", FA_WRITE | FA_CREATE_ALWAYS);
        for (int s = 0; s < 8; s++) {
            UINT bw;
            f_write(&fp, pattern, 512, &bw);
        }
        f_close(&fp);
        if (iter >= WARMUP) ref_total += t.elapsed_us();
        f_unmount("");
    }

    report_perf("write 4KB", port_total / ITERATIONS, ref_total / ITERATIONS);
    s.check(port_total / ref_total < 2.0, "port write not more than 2x slower than ref");
}

// ============================================================================
// Performance: file read
// ============================================================================

static void test_perf_read(Suite& s) {
    s.section("Perf: File read 4KB");

    uint8_t pattern[512];
    std::srand(123);
    for (int i = 0; i < 512; i++) pattern[i] = static_cast<uint8_t>(std::rand() & 0xFF);

    // Prepare port
    format_fat16(storage_port);
    umi::fs::FatFs pfs{};
    umi::fs::DiskIo pdio = umi::fs::make_diskio(port_dev);
    pfs.set_diskio(&pdio);
    umi::fs::FatFsVolume pvol{};
    pfs.mount(&pvol, "", 1);
    {
        umi::fs::FatFile fp{};
        pfs.open(&fp, "BENCH.BIN", FA_WRITE | FA_CREATE_ALWAYS);
        for (int s = 0; s < 8; s++) {
            uint32_t bw;
            pfs.write(&fp, pattern, 512, &bw);
        }
        pfs.close(&fp);
    }

    constexpr int ITERATIONS = 30;
    constexpr int WARMUP = 2;
    double port_total = 0;
    for (int iter = 0; iter < WARMUP + ITERATIONS; iter++) {
        umi::fs::FatFile fp{};
        pfs.open(&fp, "BENCH.BIN", FA_READ);
        Timer t;
        for (int s = 0; s < 8; s++) {
            uint8_t buf[512];
            uint32_t br;
            pfs.read(&fp, buf, 512, &br);
        }
        if (iter >= WARMUP) port_total += t.elapsed_us();
        pfs.close(&fp);
    }
    pfs.unmount("");

    // Prepare ref
    format_fat16(storage_ref);
    FATFS rvol{};
    f_mount(&rvol, "", 1);
    {
        FIL fp{};
        f_open(&fp, "BENCH.BIN", FA_WRITE | FA_CREATE_ALWAYS);
        for (int s = 0; s < 8; s++) {
            UINT bw;
            f_write(&fp, pattern, 512, &bw);
        }
        f_close(&fp);
    }

    double ref_total = 0;
    for (int iter = 0; iter < WARMUP + ITERATIONS; iter++) {
        FIL fp{};
        f_open(&fp, "BENCH.BIN", FA_READ);
        Timer t;
        for (int s = 0; s < 8; s++) {
            uint8_t buf[512];
            UINT br;
            f_read(&fp, buf, 512, &br);
        }
        if (iter >= WARMUP) ref_total += t.elapsed_us();
        f_close(&fp);
    }
    f_unmount("");

    report_perf("read 4KB", port_total / ITERATIONS, ref_total / ITERATIONS);
    s.check(port_total / ref_total < 2.0, "port read not more than 2x slower than ref");
}

// ============================================================================
// Performance: mount
// ============================================================================

static void test_perf_mount(Suite& s) {
    s.section("Perf: Mount");

    constexpr int ITERATIONS = 100;

    // Port
    double port_total = 0;
    for (int iter = 0; iter < ITERATIONS; iter++) {
        format_fat16(storage_port);
        umi::fs::FatFs pfs{};
        umi::fs::DiskIo pdio = umi::fs::make_diskio(port_dev);
        pfs.set_diskio(&pdio);
        umi::fs::FatFsVolume pvol{};
        Timer t;
        pfs.mount(&pvol, "", 1);
        port_total += t.elapsed_us();
        pfs.unmount("");
    }

    // Reference
    double ref_total = 0;
    for (int iter = 0; iter < ITERATIONS; iter++) {
        format_fat16(storage_ref);
        FATFS rvol{};
        Timer t;
        f_mount(&rvol, "", 1);
        ref_total += t.elapsed_us();
        f_unmount("");
    }

    report_perf("mount", port_total / ITERATIONS, ref_total / ITERATIONS);
    s.check(port_total / ref_total < 2.0, "port mount not more than 2x slower than ref");
}

// ============================================================================
// Performance: 64KB write with random data
// ============================================================================

static void test_perf_write_64k(Suite& s) {
    s.section("Perf: Write 64KB random");

    constexpr int ITERATIONS = 10;
    constexpr int WARMUP = 2;
    constexpr int CHUNK_SIZE = 512;
    constexpr int CHUNKS = 128; // 64KB total
    uint8_t pattern[CHUNK_SIZE];
    std::srand(777);
    for (int i = 0; i < CHUNK_SIZE; i++) pattern[i] = static_cast<uint8_t>(std::rand() & 0xFF);

    // Port
    double port_total = 0;
    for (int iter = 0; iter < WARMUP + ITERATIONS; iter++) {
        format_fat16(storage_port);
        umi::fs::FatFs pfs{};
        umi::fs::DiskIo pdio = umi::fs::make_diskio(port_dev);
        pfs.set_diskio(&pdio);
        umi::fs::FatFsVolume pvol{};
        pfs.mount(&pvol, "", 1);

        umi::fs::FatFile fp{};
        Timer t;
        pfs.open(&fp, "BIG.BIN", FA_WRITE | FA_CREATE_ALWAYS);
        for (int s = 0; s < CHUNKS; s++) {
            uint32_t bw;
            pfs.write(&fp, pattern, CHUNK_SIZE, &bw);
        }
        pfs.close(&fp);
        if (iter >= WARMUP) port_total += t.elapsed_us();
        pfs.unmount("");
    }

    // Reference
    double ref_total = 0;
    for (int iter = 0; iter < WARMUP + ITERATIONS; iter++) {
        format_fat16(storage_ref);
        FATFS rvol{};
        f_mount(&rvol, "", 1);

        FIL fp{};
        Timer t;
        f_open(&fp, "BIG.BIN", FA_WRITE | FA_CREATE_ALWAYS);
        for (int s = 0; s < CHUNKS; s++) {
            UINT bw;
            f_write(&fp, pattern, CHUNK_SIZE, &bw);
        }
        f_close(&fp);
        if (iter >= WARMUP) ref_total += t.elapsed_us();
        f_unmount("");
    }

    report_perf("write 64KB random", port_total / ITERATIONS, ref_total / ITERATIONS);
    s.check(port_total / ref_total < 2.0, "port write 64KB not more than 2x slower than ref");
}

// ============================================================================
// Performance: 64KB read with random data
// ============================================================================

static void test_perf_read_64k(Suite& s) {
    s.section("Perf: Read 64KB random");

    constexpr int ITERATIONS = 10;
    constexpr int WARMUP = 2;
    constexpr int CHUNK_SIZE = 512;
    constexpr int CHUNKS = 128; // 64KB total
    uint8_t pattern[CHUNK_SIZE];
    std::srand(777);
    for (int i = 0; i < CHUNK_SIZE; i++) pattern[i] = static_cast<uint8_t>(std::rand() & 0xFF);

    // Prepare port
    format_fat16(storage_port);
    umi::fs::FatFs pfs{};
    umi::fs::DiskIo pdio = umi::fs::make_diskio(port_dev);
    pfs.set_diskio(&pdio);
    umi::fs::FatFsVolume pvol{};
    pfs.mount(&pvol, "", 1);
    {
        umi::fs::FatFile fp{};
        pfs.open(&fp, "BIG.BIN", FA_WRITE | FA_CREATE_ALWAYS);
        for (int s = 0; s < CHUNKS; s++) {
            uint32_t bw;
            pfs.write(&fp, pattern, CHUNK_SIZE, &bw);
        }
        pfs.close(&fp);
    }

    double port_total = 0;
    for (int iter = 0; iter < WARMUP + ITERATIONS; iter++) {
        umi::fs::FatFile fp{};
        pfs.open(&fp, "BIG.BIN", FA_READ);
        Timer t;
        for (int s = 0; s < CHUNKS; s++) {
            uint8_t buf[CHUNK_SIZE];
            uint32_t br;
            pfs.read(&fp, buf, CHUNK_SIZE, &br);
        }
        if (iter >= WARMUP) port_total += t.elapsed_us();
        pfs.close(&fp);
    }
    pfs.unmount("");

    // Prepare ref
    format_fat16(storage_ref);
    FATFS rvol{};
    f_mount(&rvol, "", 1);
    {
        FIL fp{};
        f_open(&fp, "BIG.BIN", FA_WRITE | FA_CREATE_ALWAYS);
        for (int s = 0; s < CHUNKS; s++) {
            UINT bw;
            f_write(&fp, pattern, CHUNK_SIZE, &bw);
        }
        f_close(&fp);
    }

    double ref_total = 0;
    for (int iter = 0; iter < WARMUP + ITERATIONS; iter++) {
        FIL fp{};
        f_open(&fp, "BIG.BIN", FA_READ);
        Timer t;
        for (int s = 0; s < CHUNKS; s++) {
            uint8_t buf[CHUNK_SIZE];
            UINT br;
            f_read(&fp, buf, CHUNK_SIZE, &br);
        }
        if (iter >= WARMUP) ref_total += t.elapsed_us();
        f_close(&fp);
    }
    f_unmount("");

    report_perf("read 64KB random", port_total / ITERATIONS, ref_total / ITERATIONS);
    s.check(port_total / ref_total < 2.0, "port read 64KB not more than 2x slower than ref");
}

// ============================================================================
// Entry point
// ============================================================================

int main() {
    Suite s("fs_fat_compare");

    std::printf("\n=== FATfs: C++23 port vs reference C implementation ===\n\n");

    test_mount_compare(s);
    test_file_rw_compare(s);
    test_cross_port_to_ref(s);
    test_cross_ref_to_port(s);
    test_dir_compare(s);
    test_perf_write(s);
    test_perf_read(s);
    test_perf_write_64k(s);
    test_perf_read_64k(s);
    test_perf_mount(s);

    std::printf("\n");
    return s.summary();
}
