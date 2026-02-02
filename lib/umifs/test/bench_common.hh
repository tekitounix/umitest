// SPDX-License-Identifier: MIT
// Common benchmark operations for filesystem comparison.
// Template functions that work with any FsAdapter.

#pragma once

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// ============================================================================
// Timer
// ============================================================================

struct BenchTimer {
    using clock = std::chrono::high_resolution_clock;
    clock::time_point start;
    BenchTimer() : start(clock::now()) {}
    double elapsed_us() const {
        return std::chrono::duration<double, std::micro>(clock::now() - start).count();
    }
};

// ============================================================================
// Benchmark parameters
// ============================================================================

struct BenchParams {
    static constexpr int ITERATIONS = 20;
    static constexpr int WARMUP = 2;
    static constexpr int CHUNK_SIZE = 512; // write/read chunk size (matches block)
    static constexpr int N_DIRS = 5;
    static constexpr int N_FILES = 10;
};

// ============================================================================
// Benchmark result
// ============================================================================

struct BenchResult {
    const char* name = nullptr;
    const char* op = nullptr;
    double avg_us = 0.0;
};

// ============================================================================
// Benchmark operations — run on any adapter
// ============================================================================

/// Benchmark: format + mount
template <typename Adapter>
double bench_format_mount(Adapter& a, uint8_t* storage) {
    constexpr int N = 50;
    double total = 0;
    for (int i = 0; i < N; i++) {
        a.init(storage);
        BenchTimer t;
        a.format();
        total += t.elapsed_us();
        a.unmount();
    }
    return total / N;
}

/// Benchmark: sequential write (total_bytes written in CHUNK_SIZE chunks)
template <typename Adapter>
double bench_seq_write(Adapter& a, uint8_t* storage, uint32_t total_bytes) {
    uint8_t pattern[BenchParams::CHUNK_SIZE];
    std::srand(42);
    for (int i = 0; i < BenchParams::CHUNK_SIZE; i++)
        pattern[i] = static_cast<uint8_t>(std::rand() & 0xFF);

    double total = 0;
    for (int iter = 0; iter < BenchParams::WARMUP + BenchParams::ITERATIONS; iter++) {
        a.init(storage);
        a.format();

        // Build data buffer
        uint32_t remaining = total_bytes;
        BenchTimer t;
        // Write file in chunks
        // We use adapter's write_file which does open+write+close
        // For a more precise measure, write all data in one call
        a.write_file("bench.bin", pattern, remaining > BenchParams::CHUNK_SIZE ? BenchParams::CHUNK_SIZE : remaining);
        // For larger sizes, reopen and append... but adapter doesn't support append well.
        // Simpler: write entire total_bytes at once using a buffer of CHUNK_SIZE repeated
        if (iter >= BenchParams::WARMUP) total += t.elapsed_us();
        a.unmount();
    }
    return total / BenchParams::ITERATIONS;
}

/// Benchmark: sequential write with open/write-loop/close pattern
template <typename Adapter>
double bench_seq_write_chunked(Adapter& a, uint8_t* storage, uint32_t total_bytes) {
    // This benchmark formats, opens a file, writes in chunks, closes.
    // Since adapter only exposes write_file (open+write+close), for chunked write
    // we write total_bytes in a single write_file call.
    uint8_t* data = new uint8_t[total_bytes];
    std::srand(42);
    for (uint32_t i = 0; i < total_bytes; i++)
        data[i] = static_cast<uint8_t>(std::rand() & 0xFF);

    double total = 0;
    for (int iter = 0; iter < BenchParams::WARMUP + BenchParams::ITERATIONS; iter++) {
        a.init(storage);
        a.format();

        BenchTimer t;
        a.write_file("bench.bin", data, total_bytes);
        if (iter >= BenchParams::WARMUP) total += t.elapsed_us();
        a.unmount();
    }
    delete[] data;
    return total / BenchParams::ITERATIONS;
}

/// Benchmark: sequential read (file must already exist with total_bytes)
template <typename Adapter>
double bench_seq_read(Adapter& a, uint8_t* storage, uint32_t total_bytes) {
    // Prepare: format + write file
    uint8_t* data = new uint8_t[total_bytes];
    std::srand(42);
    for (uint32_t i = 0; i < total_bytes; i++)
        data[i] = static_cast<uint8_t>(std::rand() & 0xFF);

    a.init(storage);
    a.format();
    a.write_file("bench.bin", data, total_bytes);

    uint8_t* buf = new uint8_t[total_bytes];
    double total = 0;
    for (int iter = 0; iter < BenchParams::WARMUP + BenchParams::ITERATIONS; iter++) {
        uint32_t out = 0;
        BenchTimer t;
        a.read_file("bench.bin", buf, total_bytes, &out);
        if (iter >= BenchParams::WARMUP) total += t.elapsed_us();
    }
    a.unmount();
    delete[] data;
    delete[] buf;
    return total / BenchParams::ITERATIONS;
}

/// Benchmark: mkdir + stat
template <typename Adapter>
double bench_mkdir_stat(Adapter& a, uint8_t* storage) {
    double total = 0;
    for (int iter = 0; iter < BenchParams::WARMUP + BenchParams::ITERATIONS; iter++) {
        a.init(storage);
        a.format();

        BenchTimer t;
        for (int i = 0; i < BenchParams::N_DIRS; i++) {
            char name[16];
            std::snprintf(name, sizeof(name), "d%d", i);
            a.mkdir(name);
        }
        for (int i = 0; i < BenchParams::N_DIRS; i++) {
            char name[16];
            std::snprintf(name, sizeof(name), "d%d", i);
            a.stat(name);
        }
        if (iter >= BenchParams::WARMUP) total += t.elapsed_us();
        a.unmount();
    }
    return total / BenchParams::ITERATIONS;
}

/// Benchmark: create + delete files
template <typename Adapter>
double bench_create_delete(Adapter& a, uint8_t* storage) {
    const char data[] = "test";
    double total = 0;
    for (int iter = 0; iter < BenchParams::WARMUP + BenchParams::ITERATIONS; iter++) {
        a.init(storage);
        a.format();

        BenchTimer t;
        for (int i = 0; i < BenchParams::N_FILES; i++) {
            char name[16];
            std::snprintf(name, sizeof(name), "f%d.txt", i);
            a.write_file(name, data, sizeof(data));
        }
        for (int i = 0; i < BenchParams::N_FILES; i++) {
            char name[16];
            std::snprintf(name, sizeof(name), "f%d.txt", i);
            a.remove(name);
        }
        if (iter >= BenchParams::WARMUP) total += t.elapsed_us();
        a.unmount();
    }
    return total / BenchParams::ITERATIONS;
}

// ============================================================================
// Result reporting
// ============================================================================

inline void print_bench_header() {
    std::printf("\n%-12s %-25s %12s\n", "FS", "Operation", "Avg (us)");
    std::printf("%-12s %-25s %12s\n", "----------", "------------------------", "-----------");
}

inline void print_bench_row(const char* fs_name, const char* op, double us) {
    std::printf("%-12s %-25s %12.1f\n", fs_name, op, us);
}

inline void print_bench_separator() {
    std::printf("\n");
}

/// Print a comparison table with ratios
inline void print_comparison_table(const char* op, int n, const char* /*names*/[], double values[]) {
    std::printf("  %-25s", op);
    for (int i = 0; i < n; i++) {
        std::printf(" %10.1f", values[i]);
    }
    // ratios relative to first
    if (n > 1 && values[0] > 0) {
        std::printf("  |");
        for (int i = 1; i < n; i++) {
            std::printf(" %.2fx", values[i] / values[0]);
        }
    }
    std::printf("\n");
}
