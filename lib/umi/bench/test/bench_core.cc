// Generic benchmark core - target independent
// Include this in your target-specific main

#pragma once

#include <cstddef>
#include <cstdint>

// DWT Timer interface (to be provided by target)
struct DWT_Timer {
    static uint32_t now();
    static void reset();
    static void enable();
};

// UART interface (to be provided by target)
struct UART_Console {
    static void putc(char c);
    static void puts(const char* s);
    static void print_uint(uint32_t n);
    static void print_int(int32_t n);
};

// Statistics
struct BenchStats {
    uint32_t min = 0, max = 0, mean = 0;

    template<size_t N>
    static BenchStats compute(const uint32_t (&samples)[N]) {
        BenchStats s;
        s.min = s.max = samples[0];
        uint64_t sum = 0;
        for (size_t i = 0; i < N; i++) {
            if (samples[i] < s.min) s.min = samples[i];
            if (samples[i] > s.max) s.max = samples[i];
            sum += samples[i];
        }
        s.mean = static_cast<uint32_t>(sum / N);
        return s;
    }
};

// Measurement with baseline correction
template<typename Timer, typename Func>
uint32_t measure_cycles(Func&& f) {
    Timer::reset();
    asm volatile("dmb" ::: "memory");
    uint32_t start = Timer::now();
    asm volatile("dmb" ::: "memory");
    f();
    asm volatile("dmb" ::: "memory");
    uint32_t end = Timer::now();
    return end - start;
}

// Benchmark with statistics
template<typename Timer, size_t N, typename Func>
BenchStats benchmark(Func&& f) {
    uint32_t samples[N];
    for (size_t i = 0; i < N; i++) {
        samples[i] = measure_cycles<Timer>(f);
    }
    return BenchStats::compute(samples);
}

// ============ Benchmark Tests ============

inline void bench_add_10x() {
    volatile int x = 0;
    x += 1; x += 2; x += 3; x += 4; x += 5;
    x += 6; x += 7; x += 8; x += 9; x += 10;
    (void)x;
}

inline void bench_mul_10x() {
    volatile int x = 100;
    x *= 2; x *= 3; x *= 4; x *= 5;
    x *= 6; x *= 7; x *= 8; x *= 9;
    x *= 10; x *= 11;
    (void)x;
}

inline void bench_float_mul_10x() {
    volatile float x = 1.0f;
    x *= 1.1f; x *= 1.2f; x *= 1.3f; x *= 1.4f; x *= 1.5f;
    x *= 1.6f; x *= 1.7f; x *= 1.8f; x *= 1.9f; x *= 2.0f;
    (void)x;
}

inline void bench_array_access() {
    volatile int arr[10];
    arr[0] = 0; arr[1] = 1; arr[2] = 2; arr[3] = 3; arr[4] = 4;
    arr[5] = 5; arr[6] = 6; arr[7] = 7; arr[8] = 8; arr[9] = 9;
    volatile int sum = arr[0] + arr[9];
    (void)sum;
}

// Main benchmark runner
template<typename Timer, typename Console>
void run_benchmarks() {
    Console::puts("\n=== Benchmark Core ===\n");

    // Baseline (empty)
    auto baseline = benchmark<Timer, 10>([]{});
    Console::puts("Baseline: min=");
    Console::print_uint(baseline.min);
    Console::puts(" mean=");
    Console::print_uint(baseline.mean);
    Console::puts("\n");

    // 10x ADD
    auto r1 = benchmark<Timer, 10>(bench_add_10x);
    Console::puts("10x ADD: min=");
    Console::print_uint(r1.min - baseline.min);
    Console::puts(" mean=");
    Console::print_uint(r1.mean - baseline.mean);
    Console::puts(" cy\n");

    // 10x MUL
    auto r2 = benchmark<Timer, 10>(bench_mul_10x);
    Console::puts("10x MUL: min=");
    Console::print_uint(r2.min - baseline.min);
    Console::puts(" mean=");
    Console::print_uint(r2.mean - baseline.mean);
    Console::puts(" cy\n");

    // 10x float MUL
    auto r3 = benchmark<Timer, 10>(bench_float_mul_10x);
    Console::puts("10x float MUL: min=");
    Console::print_uint(r3.min - baseline.min);
    Console::puts(" mean=");
    Console::print_uint(r3.mean - baseline.mean);
    Console::puts(" cy\n");

    Console::puts("=== Done ===\n\n");
}
