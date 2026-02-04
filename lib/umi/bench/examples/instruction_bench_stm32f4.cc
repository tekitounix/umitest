// SPDX-License-Identifier: MIT
// Comprehensive Instruction Benchmark (new framework)
#include <bench/bench.hh>
#include <bench/platform/stm32f4.hh>
#include <cstdint>

namespace {

using Platform = umi::bench::Stm32f4;
using Output = Platform::Output;

constexpr std::uint32_t iterations = 100;

template <typename Func>
void bench(const char* name, umi::bench::Runner<Platform::Timer>& runner, Func&& func, std::uint32_t expected = 0) {
    auto stats = runner.run<64>(iterations, func);
    umi::bench::report<Output>(name, stats, expected);
}

} // namespace

int main() {
    Platform::init();
    Output::puts("\n\n=== Comprehensive Instruction Benchmark ===\n");

    umi::bench::Runner<Platform::Timer> runner;
    runner.calibrate<256>();

    static volatile int add_x = 1;
    static volatile int mul_x = 2;
    static volatile int div_x = 100000;
    static volatile int div_y = 2;
    static volatile int and_x = 0xFF;
    static volatile int or_x = 0;
    static volatile int xor_x = 0xFF;
    static volatile int lsl_x = 1;
    static volatile int lsr_x = 0x8000;
    static volatile int ldr_arr[4] = {1, 2, 3, 4};
    static volatile int str_arr[4];
    static volatile std::uint32_t ldr_index = 0;
    static volatile std::uint32_t str_index = 0;
    static volatile int pred_x = 0;
    static volatile int mispred_x = 0;
    static volatile int indep_a = 1;
    static volatile int indep_b = 2;
    static volatile int indep_c = 3;
    static volatile int indep_d = 4;
    static volatile int dep_x = 1;
    static volatile float fadd_x = 1.0f;
    static volatile float fmul_x = 1.0f;
    static volatile float fdiv_x = 100.0f;
    static volatile float fdiv_y = 2.0f;

    Output::puts("Baseline: ");
    Output::print_uint(static_cast<std::uint32_t>(runner.get_baseline()));
    Output::puts(" cy\n\n");

    Output::puts("Arithmetic:\n");
    bench("ADD", runner, [&] { add_x += 1; }, 1);
    bench("MUL", runner, [&] { mul_x *= 2; }, 1);
    bench("DIV", runner, [&] { div_x = div_x / div_y; }, 12);

    Output::puts("\nLogic:\n");
    bench("AND", runner, [&] { and_x &= 0xAA; }, 1);
    bench("OR", runner, [&] { or_x |= 0x55; }, 1);
    bench("XOR", runner, [&] { xor_x ^= 0x55; }, 1);
    bench("LSL", runner, [&] { lsl_x <<= 1; }, 1);
    bench("LSR", runner, [&] { lsr_x >>= 1; }, 1);

    Output::puts("\nMemory:\n");
    bench(
        "LDR",
        runner,
        [&] {
            ldr_index = (ldr_index + 1) & 3u;
            pred_x += ldr_arr[ldr_index];
        },
        2);
    bench(
        "STR",
        runner,
        [&] {
            str_index = (str_index + 1) & 3u;
            str_arr[str_index] = static_cast<int>(str_index);
        },
        2);

    Output::puts("\nBranch:\n");
    bench(
        "Predicted",
        runner,
        [&] {
            if (pred_x >= 0)
                pred_x = pred_x + 1;
        },
        2);
    bench(
        "Mispredict",
        runner,
        [&] {
            if (mispred_x & 1)
                mispred_x = mispred_x + 1;
            else
                mispred_x = mispred_x - 1;
        },
        3);

    Output::puts("\nPipeline:\n");
    bench(
        "Independent",
        runner,
        [&] {
            indep_a += 1;
            indep_b += 2;
            indep_c += 3;
            indep_d += 4;
        },
        0);
    bench("Dependent", runner, [&] { dep_x = dep_x * 2 + 1; }, 0);

    Output::puts("\nFloat:\n");
    bench("FADD", runner, [&] { fadd_x += 1.0f; }, 1);
    bench("FMUL", runner, [&] { fmul_x *= 1.1f; }, 1);
    bench("FDIV", runner, [&] { fdiv_x = fdiv_x / fdiv_y; }, 14);

    Output::puts("\n=== Done ===\n");
    Platform::halt();
    return 0;
}
