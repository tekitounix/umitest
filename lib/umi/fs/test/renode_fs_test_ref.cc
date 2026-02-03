// =============================================================================
// UMI-OS Renode Unified Benchmark — lfs(ref) + fat(ref)
// =============================================================================
// DWT cycle counter benchmark on Cortex-M4 via Renode emulation.
// Uses identical operation set and iterations as renode_fs_test.cc
// for fair comparison across all 4 FS implementations.
// =============================================================================

#ifdef __clang__
#pragma GCC diagnostic ignored "-Wshorten-64-to-32"
#pragma GCC diagnostic ignored "-Wreturn-type-c-linkage"
#pragma GCC diagnostic ignored "-Wsection"
#endif

#include <common/vector_table.hh>

extern "C" {
#include "lfs.h"
#include "ff.h"
#include "diskio.h"
}

#include <cstdint>
#include <cstring>

// =============================================================================
// Heap symbol aliases (picolibc)
// =============================================================================

extern "C" {
extern char _heap_start;
extern char _heap_end;
__attribute__((used)) char* __heap_start = &_heap_start;
__attribute__((used)) char* __heap_end = &_heap_end;
}

// =============================================================================
// UART output (provided by syscalls.cc)
// =============================================================================

extern "C" int _write(int, const void*, int);

namespace {

void print(const char* s) {
    while (*s) { _write(1, s, 1); ++s; }
}

void println(const char* s) { print(s); print("\r\n"); }

void print_uint32(uint32_t val) {
    if (val == 0) { print("0"); return; }
    char buf[12];
    int i = 0;
    while (val > 0) { buf[i++] = '0' + (val % 10); val /= 10; }
    while (i > 0) { char c[2] = {buf[--i], 0}; print(c); }
}

void print_int(int val) {
    if (val < 0) { print("-"); val = -val; }
    if (val == 0) { print("0"); return; }
    char buf[12];
    int i = 0;
    while (val > 0) { buf[i++] = '0' + (val % 10); val /= 10; }
    while (i > 0) { char c[2] = {buf[--i], 0}; print(c); }
}

} // namespace

// =============================================================================
// DWT Cycle Counter
// =============================================================================

static volatile uint32_t* const DWT_CTRL_REG = reinterpret_cast<volatile uint32_t*>(0xE0001000);
static volatile uint32_t* const DWT_CYCCNT_REG = reinterpret_cast<volatile uint32_t*>(0xE0001004);
static volatile uint32_t* const DEMCR_REG = reinterpret_cast<volatile uint32_t*>(0xE000EDFC);

static void dwt_init() {
    *DEMCR_REG |= (1u << 24);
    *DWT_CYCCNT_REG = 0;
    *DWT_CTRL_REG |= 1u;
}

static uint32_t dwt_get() { return *DWT_CYCCNT_REG; }

// =============================================================================
// Benchmark parameters — must match renode_fs_test.cc
// =============================================================================

static constexpr int BENCH_ITERATIONS = 20;
static constexpr int BENCH_WARMUP = 2;
static constexpr int N_DIRS = 5;
static constexpr int N_FILES = 10;

// =============================================================================
// Shared storage geometry
// =============================================================================

static constexpr uint32_t BLK_SZ = 512;
static constexpr uint32_t BLK_CNT = 128; // 64KB

// =============================================================================
// Result reporting
// =============================================================================

static void report(const char* fs, const char* op, uint32_t total_cycles, int iters) {
    uint32_t avg = total_cycles / static_cast<uint32_t>(iters);
    print("  ");
    print(fs);
    print(" | ");
    print(op);
    print(" | avg ");
    print_uint32(avg);
    print(" cycles (");
    print_int(iters);
    print(" iters, total ");
    print_uint32(total_cycles);
    print(")");
    println("");
}

// =============================================================================
// littlefs reference C
// =============================================================================

// Single shared storage — lfs and fat run sequentially
static uint8_t shared_storage[BLK_SZ * BLK_CNT];
static uint8_t lfs_rd[BLK_SZ], lfs_pr[BLK_SZ], lfs_la[16], lfs_fb[BLK_SZ];

static int lfs_ram_read(const lfs_config* c, lfs_block_t b, lfs_off_t o, void* buf, lfs_size_t sz) {
    std::memcpy(buf, &static_cast<uint8_t*>(c->context)[b * BLK_SZ + o], sz);
    return 0;
}
static int lfs_ram_prog(const lfs_config* c, lfs_block_t b, lfs_off_t o, const void* buf, lfs_size_t sz) {
    std::memcpy(&static_cast<uint8_t*>(c->context)[b * BLK_SZ + o], buf, sz);
    return 0;
}
static int lfs_ram_erase(const lfs_config* c, lfs_block_t b) {
    std::memset(&static_cast<uint8_t*>(c->context)[b * BLK_SZ], 0xFF, BLK_SZ);
    return 0;
}
static int lfs_ram_sync(const lfs_config*) { return 0; }

static lfs_config make_lfs_cfg() {
    lfs_config cfg{};
    cfg.context = shared_storage;
    cfg.read = lfs_ram_read;
    cfg.prog = lfs_ram_prog;
    cfg.erase = lfs_ram_erase;
    cfg.sync = lfs_ram_sync;
    cfg.read_size = 16;
    cfg.prog_size = 16;
    cfg.block_size = BLK_SZ;
    cfg.block_count = BLK_CNT;
    cfg.cache_size = BLK_SZ;
    cfg.lookahead_size = 16;
    cfg.block_cycles = 500;
    cfg.read_buffer = lfs_rd;
    cfg.prog_buffer = lfs_pr;
    cfg.lookahead_buffer = lfs_la;
    return cfg;
}

static void bench_lfs() {
    println("--- lfs(ref) benchmark ---");

    uint32_t t0, t1, total;
    char data[BLK_SZ];
    std::memset(data, 'A', sizeof(data));

    // format+mount
    total = 0;
    for (int i = 0; i < BENCH_WARMUP + BENCH_ITERATIONS; i++) {
        std::memset(shared_storage, 0xFF, sizeof(shared_storage));
        auto cfg = make_lfs_cfg();
        lfs_t lfs{};
        t0 = dwt_get();
        lfs_format(&lfs, &cfg);
        lfs_mount(&lfs, &cfg);
        t1 = dwt_get();
        if (i >= BENCH_WARMUP) total += (t1 - t0);
        lfs_unmount(&lfs);
    }
    report("lfs(ref)", "format+mount", total, BENCH_ITERATIONS);

    // write 1KB
    total = 0;
    for (int i = 0; i < BENCH_WARMUP + BENCH_ITERATIONS; i++) {
        std::memset(shared_storage, 0xFF, sizeof(shared_storage));
        auto cfg = make_lfs_cfg();
        lfs_t lfs{}; lfs_format(&lfs, &cfg); lfs_mount(&lfs, &cfg);
        lfs_file_t f{}; lfs_file_config fc{}; fc.buffer = lfs_fb;
        lfs_file_opencfg(&lfs, &f, "B.DAT", LFS_O_WRONLY | LFS_O_CREAT, &fc);
        t0 = dwt_get();
        for (int c = 0; c < 2; c++) lfs_file_write(&lfs, &f, data, BLK_SZ);
        t1 = dwt_get();
        lfs_file_close(&lfs, &f);
        if (i >= BENCH_WARMUP) total += (t1 - t0);
        lfs_unmount(&lfs);
    }
    report("lfs(ref)", "write 1KB", total, BENCH_ITERATIONS);

    // write 4KB
    total = 0;
    for (int i = 0; i < BENCH_WARMUP + BENCH_ITERATIONS; i++) {
        std::memset(shared_storage, 0xFF, sizeof(shared_storage));
        auto cfg = make_lfs_cfg();
        lfs_t lfs{}; lfs_format(&lfs, &cfg); lfs_mount(&lfs, &cfg);
        lfs_file_t f{}; lfs_file_config fc{}; fc.buffer = lfs_fb;
        lfs_file_opencfg(&lfs, &f, "B.DAT", LFS_O_WRONLY | LFS_O_CREAT, &fc);
        t0 = dwt_get();
        for (int c = 0; c < 8; c++) lfs_file_write(&lfs, &f, data, BLK_SZ);
        t1 = dwt_get();
        lfs_file_close(&lfs, &f);
        if (i >= BENCH_WARMUP) total += (t1 - t0);
        lfs_unmount(&lfs);
    }
    report("lfs(ref)", "write 4KB", total, BENCH_ITERATIONS);

    // read 1KB
    total = 0;
    for (int i = 0; i < BENCH_WARMUP + BENCH_ITERATIONS; i++) {
        std::memset(shared_storage, 0xFF, sizeof(shared_storage));
        auto cfg = make_lfs_cfg();
        lfs_t lfs{}; lfs_format(&lfs, &cfg); lfs_mount(&lfs, &cfg);
        lfs_file_t f{}; lfs_file_config fc{}; fc.buffer = lfs_fb;
        lfs_file_opencfg(&lfs, &f, "B.DAT", LFS_O_WRONLY | LFS_O_CREAT, &fc);
        for (int c = 0; c < 2; c++) lfs_file_write(&lfs, &f, data, BLK_SZ);
        lfs_file_close(&lfs, &f);
        fc = {}; fc.buffer = lfs_fb;
        lfs_file_opencfg(&lfs, &f, "B.DAT", LFS_O_RDONLY, &fc);
        char rd[BLK_SZ];
        t0 = dwt_get();
        for (int c = 0; c < 2; c++) lfs_file_read(&lfs, &f, rd, BLK_SZ);
        t1 = dwt_get();
        lfs_file_close(&lfs, &f);
        if (i >= BENCH_WARMUP) total += (t1 - t0);
        lfs_unmount(&lfs);
    }
    report("lfs(ref)", "read 1KB", total, BENCH_ITERATIONS);

    // read 4KB
    total = 0;
    for (int i = 0; i < BENCH_WARMUP + BENCH_ITERATIONS; i++) {
        std::memset(shared_storage, 0xFF, sizeof(shared_storage));
        auto cfg = make_lfs_cfg();
        lfs_t lfs{}; lfs_format(&lfs, &cfg); lfs_mount(&lfs, &cfg);
        lfs_file_t f{}; lfs_file_config fc{}; fc.buffer = lfs_fb;
        lfs_file_opencfg(&lfs, &f, "B.DAT", LFS_O_WRONLY | LFS_O_CREAT, &fc);
        for (int c = 0; c < 8; c++) lfs_file_write(&lfs, &f, data, BLK_SZ);
        lfs_file_close(&lfs, &f);
        fc = {}; fc.buffer = lfs_fb;
        lfs_file_opencfg(&lfs, &f, "B.DAT", LFS_O_RDONLY, &fc);
        char rd[BLK_SZ];
        t0 = dwt_get();
        for (int c = 0; c < 8; c++) lfs_file_read(&lfs, &f, rd, BLK_SZ);
        t1 = dwt_get();
        lfs_file_close(&lfs, &f);
        if (i >= BENCH_WARMUP) total += (t1 - t0);
        lfs_unmount(&lfs);
    }
    report("lfs(ref)", "read 4KB", total, BENCH_ITERATIONS);

    // mkdir+stat x5
    total = 0;
    for (int i = 0; i < BENCH_WARMUP + BENCH_ITERATIONS; i++) {
        std::memset(shared_storage, 0xFF, sizeof(shared_storage));
        auto cfg = make_lfs_cfg();
        lfs_t lfs{}; lfs_format(&lfs, &cfg); lfs_mount(&lfs, &cfg);
        const char* dirs[] = {"D0", "D1", "D2", "D3", "D4"};
        t0 = dwt_get();
        for (int d = 0; d < N_DIRS; d++) lfs_mkdir(&lfs, dirs[d]);
        lfs_info info{};
        for (int d = 0; d < N_DIRS; d++) lfs_stat(&lfs, dirs[d], &info);
        t1 = dwt_get();
        if (i >= BENCH_WARMUP) total += (t1 - t0);
        lfs_unmount(&lfs);
    }
    report("lfs(ref)", "mkdir+stat x5", total, BENCH_ITERATIONS);

    // create+delete x10
    total = 0;
    for (int i = 0; i < BENCH_WARMUP + BENCH_ITERATIONS; i++) {
        std::memset(shared_storage, 0xFF, sizeof(shared_storage));
        auto cfg = make_lfs_cfg();
        lfs_t lfs{}; lfs_format(&lfs, &cfg); lfs_mount(&lfs, &cfg);
        const char* files[] = {"F0","F1","F2","F3","F4","F5","F6","F7","F8","F9"};
        t0 = dwt_get();
        for (int f = 0; f < N_FILES; f++) {
            lfs_file_t sf{}; lfs_file_config fc{}; fc.buffer = lfs_fb;
            lfs_file_opencfg(&lfs, &sf, files[f], LFS_O_WRONLY | LFS_O_CREAT, &fc);
            lfs_file_write(&lfs, &sf, "x", 1);
            lfs_file_close(&lfs, &sf);
        }
        for (int f = 0; f < N_FILES; f++) lfs_remove(&lfs, files[f]);
        t1 = dwt_get();
        if (i >= BENCH_WARMUP) total += (t1 - t0);
        lfs_unmount(&lfs);
    }
    report("lfs(ref)", "create+del x10", total, BENCH_ITERATIONS);
}

// =============================================================================
// FATfs reference C
// =============================================================================

extern "C" {

DSTATUS disk_status(BYTE) { return 0; }
DSTATUS disk_initialize(BYTE) { return 0; }

DRESULT disk_read(BYTE, BYTE* buff, LBA_t sector, UINT count) {
    for (UINT i = 0; i < count; i++)
        std::memcpy(buff + i * BLK_SZ, &shared_storage[(sector + i) * BLK_SZ], BLK_SZ);
    return RES_OK;
}

DRESULT disk_write(BYTE, const BYTE* buff, LBA_t sector, UINT count) {
    for (UINT i = 0; i < count; i++)
        std::memcpy(&shared_storage[(sector + i) * BLK_SZ], buff + i * BLK_SZ, BLK_SZ);
    return RES_OK;
}

DRESULT disk_ioctl(BYTE, BYTE cmd, void*) {
    if (cmd == CTRL_SYNC) return RES_OK;
    return RES_PARERR;
}

DWORD get_fattime(void) {
    return ((DWORD)(2025 - 1980) << 25) | ((DWORD)1 << 21) | ((DWORD)1 << 16);
}

} // extern "C"

static void format_fat12() {
    std::memset(shared_storage, 0, sizeof(shared_storage));
    uint8_t* bs = shared_storage;
    bs[0] = 0xEB; bs[1] = 0x3C; bs[2] = 0x90;
    std::memcpy(&bs[3], "MSDOS5.0", 8);
    bs[11] = 0x00; bs[12] = 0x02;
    bs[13] = 1; bs[14] = 1; bs[15] = 0; bs[16] = 2;
    bs[17] = 0x40; bs[18] = 0x00;
    bs[19] = 128; bs[20] = 0; bs[21] = 0xF8;
    bs[22] = 1; bs[23] = 0; bs[24] = 0x3F; bs[25] = 0;
    bs[26] = 0xFF; bs[27] = 0; bs[38] = 0x29;
    bs[39] = 0x12; bs[40] = 0x34; bs[41] = 0x56; bs[42] = 0x78;
    std::memcpy(&bs[43], "NO NAME    ", 11);
    std::memcpy(&bs[54], "FAT12   ", 8);
    bs[510] = 0x55; bs[511] = 0xAA;
    uint8_t* fat1 = &shared_storage[512];
    fat1[0] = 0xF8; fat1[1] = 0xFF; fat1[2] = 0xFF;
    uint8_t* fat2 = &shared_storage[512 * 2];
    fat2[0] = 0xF8; fat2[1] = 0xFF; fat2[2] = 0xFF;
}

static void bench_fat() {
    println("--- fat(ref) benchmark ---");

    uint32_t t0, t1, total;
    char data[BLK_SZ];
    std::memset(data, 'A', sizeof(data));

    // format+mount
    total = 0;
    for (int i = 0; i < BENCH_WARMUP + BENCH_ITERATIONS; i++) {
        format_fat12();
        FATFS fs{};
        t0 = dwt_get();
        f_mount(&fs, "", 1);
        t1 = dwt_get();
        if (i >= BENCH_WARMUP) total += (t1 - t0);
        f_mount(nullptr, "", 0);
    }
    report("fat(ref)", "format+mount", total, BENCH_ITERATIONS);

    // write 1KB
    total = 0;
    for (int i = 0; i < BENCH_WARMUP + BENCH_ITERATIONS; i++) {
        format_fat12();
        FATFS fs{}; f_mount(&fs, "", 1);
        FIL fp{}; f_open(&fp, "B.DAT", FA_WRITE | FA_CREATE_ALWAYS);
        t0 = dwt_get();
        for (int c = 0; c < 2; c++) { UINT bw; f_write(&fp, data, BLK_SZ, &bw); }
        t1 = dwt_get();
        f_close(&fp);
        if (i >= BENCH_WARMUP) total += (t1 - t0);
        f_mount(nullptr, "", 0);
    }
    report("fat(ref)", "write 1KB", total, BENCH_ITERATIONS);

    // write 4KB
    total = 0;
    for (int i = 0; i < BENCH_WARMUP + BENCH_ITERATIONS; i++) {
        format_fat12();
        FATFS fs{}; f_mount(&fs, "", 1);
        FIL fp{}; f_open(&fp, "B.DAT", FA_WRITE | FA_CREATE_ALWAYS);
        t0 = dwt_get();
        for (int c = 0; c < 8; c++) { UINT bw; f_write(&fp, data, BLK_SZ, &bw); }
        t1 = dwt_get();
        f_close(&fp);
        if (i >= BENCH_WARMUP) total += (t1 - t0);
        f_mount(nullptr, "", 0);
    }
    report("fat(ref)", "write 4KB", total, BENCH_ITERATIONS);

    // read 1KB
    total = 0;
    for (int i = 0; i < BENCH_WARMUP + BENCH_ITERATIONS; i++) {
        format_fat12();
        FATFS fs{}; f_mount(&fs, "", 1);
        FIL fp{}; f_open(&fp, "B.DAT", FA_WRITE | FA_CREATE_ALWAYS);
        for (int c = 0; c < 2; c++) { UINT bw; f_write(&fp, data, BLK_SZ, &bw); }
        f_close(&fp);
        f_open(&fp, "B.DAT", FA_READ);
        char rd[BLK_SZ];
        t0 = dwt_get();
        for (int c = 0; c < 2; c++) { UINT br; f_read(&fp, rd, BLK_SZ, &br); }
        t1 = dwt_get();
        f_close(&fp);
        if (i >= BENCH_WARMUP) total += (t1 - t0);
        f_mount(nullptr, "", 0);
    }
    report("fat(ref)", "read 1KB", total, BENCH_ITERATIONS);

    // read 4KB
    total = 0;
    for (int i = 0; i < BENCH_WARMUP + BENCH_ITERATIONS; i++) {
        format_fat12();
        FATFS fs{}; f_mount(&fs, "", 1);
        FIL fp{}; f_open(&fp, "B.DAT", FA_WRITE | FA_CREATE_ALWAYS);
        for (int c = 0; c < 8; c++) { UINT bw; f_write(&fp, data, BLK_SZ, &bw); }
        f_close(&fp);
        f_open(&fp, "B.DAT", FA_READ);
        char rd[BLK_SZ];
        t0 = dwt_get();
        for (int c = 0; c < 8; c++) { UINT br; f_read(&fp, rd, BLK_SZ, &br); }
        t1 = dwt_get();
        f_close(&fp);
        if (i >= BENCH_WARMUP) total += (t1 - t0);
        f_mount(nullptr, "", 0);
    }
    report("fat(ref)", "read 4KB", total, BENCH_ITERATIONS);

    // mkdir+stat x5
    total = 0;
    for (int i = 0; i < BENCH_WARMUP + BENCH_ITERATIONS; i++) {
        format_fat12();
        FATFS fs{}; f_mount(&fs, "", 1);
        const char* dirs[] = {"D0", "D1", "D2", "D3", "D4"};
        t0 = dwt_get();
        for (int d = 0; d < N_DIRS; d++) f_mkdir(dirs[d]);
        FILINFO fno{};
        for (int d = 0; d < N_DIRS; d++) f_stat(dirs[d], &fno);
        t1 = dwt_get();
        if (i >= BENCH_WARMUP) total += (t1 - t0);
        f_mount(nullptr, "", 0);
    }
    report("fat(ref)", "mkdir+stat x5", total, BENCH_ITERATIONS);

    // create+delete x10
    total = 0;
    for (int i = 0; i < BENCH_WARMUP + BENCH_ITERATIONS; i++) {
        format_fat12();
        FATFS fs{}; f_mount(&fs, "", 1);
        const char* files[] = {"F0","F1","F2","F3","F4","F5","F6","F7","F8","F9"};
        t0 = dwt_get();
        for (int f = 0; f < N_FILES; f++) {
            FIL fp{}; f_open(&fp, files[f], FA_WRITE | FA_CREATE_ALWAYS);
            UINT bw; f_write(&fp, "x", 1, &bw); f_close(&fp);
        }
        for (int f = 0; f < N_FILES; f++) f_unlink(files[f]);
        t1 = dwt_get();
        if (i >= BENCH_WARMUP) total += (t1 - t0);
        f_mount(nullptr, "", 0);
    }
    report("fat(ref)", "create+del x10", total, BENCH_ITERATIONS);
}

// =============================================================================
// Main
// =============================================================================

extern "C" [[noreturn]] void _start() {
    dwt_init();

    println("");
    println("========================================");
    println("  Renode FS Benchmark: lfs(ref)+fat(ref)");
    println("  DWT cycles, 20 iterations");
    println("========================================");

    bench_lfs();
    bench_fat();

    println("");
    println("BENCH_COMPLETE");

    while (true) { __asm volatile("wfi"); }
}
