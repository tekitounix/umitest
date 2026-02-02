// SPDX-License-Identifier: MIT
// Unified benchmark: 3 filesystem implementations compared side-by-side.
// lfs(ref), fat(cr), slim
//
// fat(ref) is in test_bench_fat_ref.cc (separate binary due to global symbols).
// All benchmarks use identical storage geometry (512B × 512 blocks = 256KB)
// and identical operation sequences for fair comparison.

#include <umitest.hh>
using namespace umitest;

// Enable adapters
#define FS_ADAPTER_LFS_REF
#define FS_ADAPTER_FAT
#define FS_ADAPTER_SLIM

#include "fs_adapter.hh"
#include "bench_common.hh"

// ============================================================================
// Storage arrays — one per adapter to avoid interference
// ============================================================================

static uint8_t storage_lfs_ref[BENCH_TOTAL_SIZE];
static uint8_t storage_fat[BENCH_TOTAL_SIZE];
static uint8_t storage_slim[BENCH_TOTAL_SIZE];

// ============================================================================
// Run all benchmarks for one adapter
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

// ============================================================================
// Entry point
// ============================================================================

int main() {
    Suite s("fs_bench");

    std::printf("\n=== umifs unified benchmark (host) ===\n");
    std::printf("Storage: %u B blocks × %u blocks = %u KB\n\n",
                BENCH_BLOCK_SIZE, BENCH_BLOCK_COUNT, BENCH_TOTAL_SIZE / 1024);

    constexpr int N_FS = 3;
    constexpr int N_OPS = 6;
    const char* fs_names[N_FS] = {"lfs(ref)", "fat(cr)", "slim"};
    const char* op_names[N_OPS] = {
        "format+mount",
        "write 1KB",
        "write 4KB",
        "read 1KB",
        "mkdir+stat x5",
        "create+delete x10",
    };

    double results[N_FS][N_OPS]{};

    s.section("Benchmark: lfs (reference C)");
    {
        LfsRefAdapter a;
        run_benchmarks(a, storage_lfs_ref, results[0]);
        s.check(true, "lfs(ref) benchmarks complete");
    }

    s.section("Benchmark: fat (cleanroom)");
    {
        FatAdapter a;
        run_benchmarks(a, storage_fat, results[1]);
        s.check(true, "fat(cr) benchmarks complete");
    }

    s.section("Benchmark: slim");
    {
        SlimAdapter a;
        run_benchmarks(a, storage_slim, results[2]);
        s.check(true, "slim benchmarks complete");
    }

    // ================================================================
    // Summary table
    // ================================================================

    std::printf("\n\n=== Benchmark Results (avg us) ===\n\n");

    // Header
    std::printf("  %-25s", "Operation");
    for (int f = 0; f < N_FS; f++) std::printf(" %10s", fs_names[f]);
    std::printf("\n");
    std::printf("  %-25s", "-------------------------");
    for (int f = 0; f < N_FS; f++) std::printf(" %10s", "----------");
    std::printf("\n");

    // Rows
    for (int o = 0; o < N_OPS; o++) {
        std::printf("  %-25s", op_names[o]);
        for (int f = 0; f < N_FS; f++) {
            std::printf(" %10.1f", results[f][o]);
        }
        std::printf("\n");
    }

    std::printf("\n");
    return s.summary();
}
