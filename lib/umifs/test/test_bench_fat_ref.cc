// SPDX-License-Identifier: MIT
// FATfs reference C benchmark — separate binary because reference FATfs
// uses global disk_* callback symbols that conflict with the cleanroom port.
// Results should be compared with fat(cr) results from test_bench.

#include "test_common.hh"

#define FS_ADAPTER_FAT
#include "fs_adapter.hh"
#undef FS_ADAPTER_FAT

// Reference FATfs configuration (inline defines to avoid ffconf.h conflicts)
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

#define FS_ADAPTER_FAT_REF
// Need to provide format_fat16_image for FatRefAdapter
// It's already defined via FS_ADAPTER_FAT above, so it's available

extern "C" {
#include "ff.h"
#include "diskio.h"
}

#include "bench_common.hh"

// ============================================================================
// Reference FATfs global disk I/O callbacks
// ============================================================================

namespace fat_ref_detail {
    inline uint8_t* g_storage = nullptr;
}

extern "C" {

DWORD get_fattime(void) {
    return ((DWORD)(2025 - 1980) << 25) | ((DWORD)1 << 21) | ((DWORD)1 << 16);
}

DSTATUS disk_initialize(BYTE) { return 0; }
DSTATUS disk_status(BYTE) { return 0; }

DRESULT disk_read(BYTE, BYTE* buff, LBA_t sector, UINT count) {
    std::memcpy(buff, &fat_ref_detail::g_storage[sector * BENCH_BLOCK_SIZE], count * BENCH_BLOCK_SIZE);
    return RES_OK;
}

DRESULT disk_write(BYTE, const BYTE* buff, LBA_t sector, UINT count) {
    std::memcpy(&fat_ref_detail::g_storage[sector * BENCH_BLOCK_SIZE], buff, count * BENCH_BLOCK_SIZE);
    return RES_OK;
}

DRESULT disk_ioctl(BYTE, BYTE cmd, void* buff) {
    switch (cmd) {
    case CTRL_SYNC: return RES_OK;
    case GET_SECTOR_COUNT: *static_cast<LBA_t*>(buff) = BENCH_BLOCK_COUNT; return RES_OK;
    case GET_SECTOR_SIZE: *static_cast<WORD*>(buff) = BENCH_BLOCK_SIZE; return RES_OK;
    case GET_BLOCK_SIZE: *static_cast<DWORD*>(buff) = 1; return RES_OK;
    default: return RES_PARERR;
    }
}

} // extern "C"

// ============================================================================
// FatRefAdapter (inline here since we have the global callbacks)
// ============================================================================

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
    int stat(const char* path) { FILINFO fi{}; return f_stat(path, &fi) == FR_OK ? 0 : 1; }
    int remove(const char* path) { return f_unlink(path) == FR_OK ? 0 : 1; }
    int rename(const char* o, const char* n) { return f_rename(o, n) == FR_OK ? 0 : 1; }
    const char* name() const { return "fat(ref)"; }
};

// ============================================================================
// Storage
// ============================================================================

static uint8_t storage_cr[BENCH_TOTAL_SIZE];
static uint8_t storage_ref[BENCH_TOTAL_SIZE];

// ============================================================================
// Entry point
// ============================================================================

template <typename Adapter>
static void run_benchmarks(Adapter& a, uint8_t* storage, double results[6]) {
    results[0] = bench_format_mount(a, storage);
    results[1] = bench_seq_write_chunked(a, storage, 1024);
    results[2] = bench_seq_write_chunked(a, storage, 4096);
    results[3] = bench_seq_read(a, storage, 1024);
    results[4] = bench_mkdir_stat(a, storage);
    results[5] = bench_create_delete(a, storage);
}

int main() {
    std::printf("\n=== FATfs benchmark: cleanroom vs reference C ===\n");
    std::printf("Storage: %u B × %u = %u KB\n\n", BENCH_BLOCK_SIZE, BENCH_BLOCK_COUNT, BENCH_TOTAL_SIZE / 1024);

    constexpr int N_OPS = 6;
    const char* op_names[N_OPS] = {
        "format+mount", "write 1KB", "write 4KB",
        "read 1KB", "mkdir+stat x5", "create+delete x10",
    };

    double cr[N_OPS]{}, ref[N_OPS]{};

    SECTION("Benchmark: fat (cleanroom)");
    {
        FatAdapter a;
        run_benchmarks(a, storage_cr, cr);
        CHECK(true, "fat(cr) benchmarks complete");
    }

    SECTION("Benchmark: fat (reference C)");
    {
        FatRefAdapter a;
        run_benchmarks(a, storage_ref, ref);
        CHECK(true, "fat(ref) benchmarks complete");
    }

    // Summary
    std::printf("\n\n=== FATfs Benchmark Results (avg us) ===\n\n");
    std::printf("  %-25s %10s %10s %10s\n", "Operation", "fat(cr)", "fat(ref)", "ratio");
    std::printf("  %-25s %10s %10s %10s\n", "-------------------------", "----------", "----------", "------");
    for (int o = 0; o < N_OPS; o++) {
        double ratio = (ref[o] > 0) ? cr[o] / ref[o] : 0;
        std::printf("  %-25s %10.1f %10.1f %10.3f\n", op_names[o], cr[o], ref[o], ratio);
    }

    std::printf("\n");
    TEST_SUMMARY();
}
