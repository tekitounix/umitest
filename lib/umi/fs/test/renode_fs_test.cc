// =============================================================================
// UMI-OS Renode Unified Benchmark — fat(cr) + slim
// =============================================================================
// DWT cycle counter benchmark on Cortex-M4 via Renode emulation.
// Uses identical operation set and iterations as renode_fs_test_ref.cc
// for fair comparison across all 4 FS implementations.
// =============================================================================

#ifdef __clang__
#pragma GCC diagnostic ignored "-Wshorten-64-to-32"
#pragma GCC diagnostic ignored "-Wreturn-type-c-linkage"
#pragma GCC diagnostic ignored "-Wsection"
#endif

#include <common/vector_table.hh>

#include <umifs/fat/ff.hh>
#include <umifs/fat/ff_diskio.hh>
#include <umifs/slim/slim.hh>
#include <umifs/slim/slim_config.hh>

#include <cstdint>
#include <cstring>

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
// Benchmark parameters — must match renode_fs_test_ref.cc
// =============================================================================

static constexpr int BENCH_ITERATIONS = 20;
static constexpr int BENCH_WARMUP = 2;
static constexpr int N_DIRS = 5;
static constexpr int N_FILES = 10;

// =============================================================================
// Shared storage geometry
// =============================================================================

static constexpr uint32_t BLK_SZ = 512;
static constexpr uint32_t BLK_CNT = 128; // 64KB — fits in SRAM

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
// FATfs cleanroom
// =============================================================================

// Single shared storage — fat and slim run sequentially, never concurrently
static uint8_t shared_storage[BLK_SZ * BLK_CNT];

struct FatRamDev {
    int read(uint32_t block, uint32_t off, void* buf, uint32_t size) {
        std::memcpy(buf, &shared_storage[block * BLK_SZ + off], size);
        return 0;
    }
    int write(uint32_t block, uint32_t off, const void* buf, uint32_t size) {
        std::memcpy(&shared_storage[block * BLK_SZ + off], buf, size);
        return 0;
    }
    int erase(uint32_t block) {
        std::memset(&shared_storage[block * BLK_SZ], 0xFF, BLK_SZ);
        return 0;
    }
    uint32_t block_size() const { return BLK_SZ; }
    uint32_t block_count() const { return BLK_CNT; }
};

static FatRamDev fat_dev;

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
    using namespace umi::fs;
    println("--- fat(cr) benchmark ---");

    auto diskio = make_diskio(fat_dev);
    uint32_t t0, t1, total;
    char data[BLK_SZ];
    std::memset(data, 'A', sizeof(data));

    // format+mount
    total = 0;
    for (int i = 0; i < BENCH_WARMUP + BENCH_ITERATIONS; i++) {
        format_fat12();
        FatFs fs{}; fs.set_diskio(&diskio);
        FatFsVolume vol{};
        t0 = dwt_get();
        fs.mount(&vol, "", 1);
        t1 = dwt_get();
        if (i >= BENCH_WARMUP) total += (t1 - t0);
        fs.unmount("");
    }
    report("fat(cr)", "format+mount", total, BENCH_ITERATIONS);

    // write 1KB
    total = 0;
    for (int i = 0; i < BENCH_WARMUP + BENCH_ITERATIONS; i++) {
        format_fat12();
        FatFs fs{}; fs.set_diskio(&diskio);
        FatFsVolume vol{}; fs.mount(&vol, "", 1);
        FatFile fp{}; fs.open(&fp, "B.DAT", FA_WRITE | FA_CREATE_ALWAYS);
        t0 = dwt_get();
        for (int c = 0; c < 2; c++) { uint32_t bw; fs.write(&fp, data, BLK_SZ, &bw); }
        t1 = dwt_get();
        fs.close(&fp);
        if (i >= BENCH_WARMUP) total += (t1 - t0);
        fs.unmount("");
    }
    report("fat(cr)", "write 1KB", total, BENCH_ITERATIONS);

    // write 4KB
    total = 0;
    for (int i = 0; i < BENCH_WARMUP + BENCH_ITERATIONS; i++) {
        format_fat12();
        FatFs fs{}; fs.set_diskio(&diskio);
        FatFsVolume vol{}; fs.mount(&vol, "", 1);
        FatFile fp{}; fs.open(&fp, "B.DAT", FA_WRITE | FA_CREATE_ALWAYS);
        t0 = dwt_get();
        for (int c = 0; c < 8; c++) { uint32_t bw; fs.write(&fp, data, BLK_SZ, &bw); }
        t1 = dwt_get();
        fs.close(&fp);
        if (i >= BENCH_WARMUP) total += (t1 - t0);
        fs.unmount("");
    }
    report("fat(cr)", "write 4KB", total, BENCH_ITERATIONS);

    // read 1KB
    total = 0;
    for (int i = 0; i < BENCH_WARMUP + BENCH_ITERATIONS; i++) {
        format_fat12();
        FatFs fs{}; fs.set_diskio(&diskio);
        FatFsVolume vol{}; fs.mount(&vol, "", 1);
        FatFile fp{}; fs.open(&fp, "B.DAT", FA_WRITE | FA_CREATE_ALWAYS);
        for (int c = 0; c < 2; c++) { uint32_t bw; fs.write(&fp, data, BLK_SZ, &bw); }
        fs.close(&fp);
        fs.open(&fp, "B.DAT", FA_READ);
        char rd[BLK_SZ];
        t0 = dwt_get();
        for (int c = 0; c < 2; c++) { uint32_t br; fs.read(&fp, rd, BLK_SZ, &br); }
        t1 = dwt_get();
        fs.close(&fp);
        if (i >= BENCH_WARMUP) total += (t1 - t0);
        fs.unmount("");
    }
    report("fat(cr)", "read 1KB", total, BENCH_ITERATIONS);

    // read 4KB
    total = 0;
    for (int i = 0; i < BENCH_WARMUP + BENCH_ITERATIONS; i++) {
        format_fat12();
        FatFs fs{}; fs.set_diskio(&diskio);
        FatFsVolume vol{}; fs.mount(&vol, "", 1);
        FatFile fp{}; fs.open(&fp, "B.DAT", FA_WRITE | FA_CREATE_ALWAYS);
        for (int c = 0; c < 8; c++) { uint32_t bw; fs.write(&fp, data, BLK_SZ, &bw); }
        fs.close(&fp);
        fs.open(&fp, "B.DAT", FA_READ);
        char rd[BLK_SZ];
        t0 = dwt_get();
        for (int c = 0; c < 8; c++) { uint32_t br; fs.read(&fp, rd, BLK_SZ, &br); }
        t1 = dwt_get();
        fs.close(&fp);
        if (i >= BENCH_WARMUP) total += (t1 - t0);
        fs.unmount("");
    }
    report("fat(cr)", "read 4KB", total, BENCH_ITERATIONS);

    // mkdir+stat x5
    total = 0;
    for (int i = 0; i < BENCH_WARMUP + BENCH_ITERATIONS; i++) {
        format_fat12();
        FatFs fs{}; fs.set_diskio(&diskio);
        FatFsVolume vol{}; fs.mount(&vol, "", 1);
        const char* dirs[] = {"D0", "D1", "D2", "D3", "D4"};
        t0 = dwt_get();
        for (int d = 0; d < N_DIRS; d++) fs.mkdir(dirs[d]);
        FatFileInfo fno{};
        for (int d = 0; d < N_DIRS; d++) fs.stat(dirs[d], &fno);
        t1 = dwt_get();
        if (i >= BENCH_WARMUP) total += (t1 - t0);
        fs.unmount("");
    }
    report("fat(cr)", "mkdir+stat x5", total, BENCH_ITERATIONS);

    // create+delete x10
    total = 0;
    for (int i = 0; i < BENCH_WARMUP + BENCH_ITERATIONS; i++) {
        format_fat12();
        FatFs fs{}; fs.set_diskio(&diskio);
        FatFsVolume vol{}; fs.mount(&vol, "", 1);
        const char* files[] = {"F0","F1","F2","F3","F4","F5","F6","F7","F8","F9"};
        t0 = dwt_get();
        for (int f = 0; f < N_FILES; f++) {
            FatFile fp{}; fs.open(&fp, files[f], FA_WRITE | FA_CREATE_ALWAYS);
            uint32_t bw; fs.write(&fp, "x", 1, &bw); fs.close(&fp);
        }
        for (int f = 0; f < N_FILES; f++) fs.unlink(files[f]);
        t1 = dwt_get();
        if (i >= BENCH_WARMUP) total += (t1 - t0);
        fs.unmount("");
    }
    report("fat(cr)", "create+del x10", total, BENCH_ITERATIONS);
}

// =============================================================================
// slimfs
// =============================================================================

static uint8_t slim_rd[BLK_SZ], slim_pr[BLK_SZ], slim_la[BLK_CNT / 8];

static int slim_read(const umi::fs::SlimConfig*, uint32_t b, uint32_t o, void* buf, uint32_t sz) {
    std::memcpy(buf, &shared_storage[b * BLK_SZ + o], sz); return 0;
}
static int slim_prog(const umi::fs::SlimConfig*, uint32_t b, uint32_t o, const void* buf, uint32_t sz) {
    std::memcpy(&shared_storage[b * BLK_SZ + o], buf, sz); return 0;
}
static int slim_erase(const umi::fs::SlimConfig*, uint32_t b) {
    std::memset(&shared_storage[b * BLK_SZ], 0xFF, BLK_SZ); return 0;
}
static int slim_sync(const umi::fs::SlimConfig*) { return 0; }

static umi::fs::SlimConfig make_slim_cfg() {
    umi::fs::SlimConfig cfg{};
    cfg.read = slim_read;
    cfg.prog = slim_prog;
    cfg.erase = slim_erase;
    cfg.sync = slim_sync;
    cfg.read_size = 1;
    cfg.prog_size = 1;
    cfg.block_size = BLK_SZ;
    cfg.block_count = BLK_CNT;
    cfg.read_buffer = {slim_rd, BLK_SZ};
    cfg.prog_buffer = {slim_pr, BLK_SZ};
    cfg.lookahead_buffer = {slim_la, sizeof(slim_la)};
    return cfg;
}

static void bench_slim() {
    using namespace umi::fs;
    println("--- slim benchmark ---");

    uint32_t t0, t1, total;
    uint8_t data[BLK_SZ];
    std::memset(data, 'A', sizeof(data));

    // format+mount
    total = 0;
    for (int i = 0; i < BENCH_WARMUP + BENCH_ITERATIONS; i++) {
        std::memset(shared_storage, 0xFF, sizeof(shared_storage));
        auto cfg = make_slim_cfg();
        SlimFs fs{};
        t0 = dwt_get();
        (void)fs.format(&cfg);
        (void)fs.mount(&cfg);
        t1 = dwt_get();
        if (i >= BENCH_WARMUP) total += (t1 - t0);
        (void)fs.unmount();
    }
    report("slim", "format+mount", total, BENCH_ITERATIONS);

    // write 1KB
    total = 0;
    for (int i = 0; i < BENCH_WARMUP + BENCH_ITERATIONS; i++) {
        std::memset(shared_storage, 0xFF, sizeof(shared_storage));
        auto cfg = make_slim_cfg();
        SlimFs fs{}; (void)fs.format(&cfg); (void)fs.mount(&cfg);
        SlimFile f{};
        (void)fs.file_open(f, "B.DAT", SlimOpenFlags::WRONLY | SlimOpenFlags::CREAT);
        t0 = dwt_get();
        for (int c = 0; c < 2; c++)
            (void)fs.file_write(f, {data, BLK_SZ});
        t1 = dwt_get();
        (void)fs.file_close(f);
        if (i >= BENCH_WARMUP) total += (t1 - t0);
        (void)fs.unmount();
    }
    report("slim", "write 1KB", total, BENCH_ITERATIONS);

    // write 4KB
    total = 0;
    for (int i = 0; i < BENCH_WARMUP + BENCH_ITERATIONS; i++) {
        std::memset(shared_storage, 0xFF, sizeof(shared_storage));
        auto cfg = make_slim_cfg();
        SlimFs fs{}; (void)fs.format(&cfg); (void)fs.mount(&cfg);
        SlimFile f{};
        (void)fs.file_open(f, "B.DAT", SlimOpenFlags::WRONLY | SlimOpenFlags::CREAT);
        t0 = dwt_get();
        for (int c = 0; c < 8; c++)
            (void)fs.file_write(f, {data, BLK_SZ});
        t1 = dwt_get();
        (void)fs.file_close(f);
        if (i >= BENCH_WARMUP) total += (t1 - t0);
        (void)fs.unmount();
    }
    report("slim", "write 4KB", total, BENCH_ITERATIONS);

    // read 1KB
    total = 0;
    for (int i = 0; i < BENCH_WARMUP + BENCH_ITERATIONS; i++) {
        std::memset(shared_storage, 0xFF, sizeof(shared_storage));
        auto cfg = make_slim_cfg();
        SlimFs fs{}; (void)fs.format(&cfg); (void)fs.mount(&cfg);
        SlimFile f{};
        (void)fs.file_open(f, "B.DAT", SlimOpenFlags::WRONLY | SlimOpenFlags::CREAT);
        for (int c = 0; c < 2; c++) (void)fs.file_write(f, {data, BLK_SZ});
        (void)fs.file_close(f);
        (void)fs.file_open(f, "B.DAT", SlimOpenFlags::RDONLY);
        uint8_t rd[BLK_SZ];
        t0 = dwt_get();
        for (int c = 0; c < 2; c++) (void)fs.file_read(f, {rd, BLK_SZ});
        t1 = dwt_get();
        (void)fs.file_close(f);
        if (i >= BENCH_WARMUP) total += (t1 - t0);
        (void)fs.unmount();
    }
    report("slim", "read 1KB", total, BENCH_ITERATIONS);

    // read 4KB
    total = 0;
    for (int i = 0; i < BENCH_WARMUP + BENCH_ITERATIONS; i++) {
        std::memset(shared_storage, 0xFF, sizeof(shared_storage));
        auto cfg = make_slim_cfg();
        SlimFs fs{}; (void)fs.format(&cfg); (void)fs.mount(&cfg);
        SlimFile f{};
        (void)fs.file_open(f, "B.DAT", SlimOpenFlags::WRONLY | SlimOpenFlags::CREAT);
        for (int c = 0; c < 8; c++) (void)fs.file_write(f, {data, BLK_SZ});
        (void)fs.file_close(f);
        (void)fs.file_open(f, "B.DAT", SlimOpenFlags::RDONLY);
        uint8_t rd[BLK_SZ];
        t0 = dwt_get();
        for (int c = 0; c < 8; c++) (void)fs.file_read(f, {rd, BLK_SZ});
        t1 = dwt_get();
        (void)fs.file_close(f);
        if (i >= BENCH_WARMUP) total += (t1 - t0);
        (void)fs.unmount();
    }
    report("slim", "read 4KB", total, BENCH_ITERATIONS);

    // mkdir+stat x5
    total = 0;
    for (int i = 0; i < BENCH_WARMUP + BENCH_ITERATIONS; i++) {
        std::memset(shared_storage, 0xFF, sizeof(shared_storage));
        auto cfg = make_slim_cfg();
        SlimFs fs{}; (void)fs.format(&cfg); (void)fs.mount(&cfg);
        const char* dirs[] = {"D0", "D1", "D2", "D3", "D4"};
        t0 = dwt_get();
        for (int d = 0; d < N_DIRS; d++) (void)fs.mkdir(dirs[d]);
        SlimInfo info{};
        for (int d = 0; d < N_DIRS; d++) (void)fs.stat(dirs[d], info);
        t1 = dwt_get();
        if (i >= BENCH_WARMUP) total += (t1 - t0);
        (void)fs.unmount();
    }
    report("slim", "mkdir+stat x5", total, BENCH_ITERATIONS);

    // create+delete x10
    total = 0;
    for (int i = 0; i < BENCH_WARMUP + BENCH_ITERATIONS; i++) {
        std::memset(shared_storage, 0xFF, sizeof(shared_storage));
        auto cfg = make_slim_cfg();
        SlimFs fs{}; (void)fs.format(&cfg); (void)fs.mount(&cfg);
        const char* files[] = {"F0","F1","F2","F3","F4","F5","F6","F7","F8","F9"};
        t0 = dwt_get();
        for (int f = 0; f < N_FILES; f++) {
            SlimFile sf{};
            (void)fs.file_open(sf, files[f], SlimOpenFlags::WRONLY | SlimOpenFlags::CREAT);
            uint8_t x = 'x';
            (void)fs.file_write(sf, {&x, 1});
            (void)fs.file_close(sf);
        }
        for (int f = 0; f < N_FILES; f++) (void)fs.remove(files[f]);
        t1 = dwt_get();
        if (i >= BENCH_WARMUP) total += (t1 - t0);
        (void)fs.unmount();
    }
    report("slim", "create+del x10", total, BENCH_ITERATIONS);
}

// =============================================================================
// Main
// =============================================================================

extern "C" [[noreturn]] void _start() {
    dwt_init();

    println("");
    println("========================================");
    println("  Renode FS Benchmark: fat(cr) + slim");
    println("  DWT cycles, 20 iterations");
    println("========================================");

    bench_fat();
    bench_slim();

    println("");
    println("BENCH_COMPLETE");

    while (true) { __asm volatile("wfi"); }
}
