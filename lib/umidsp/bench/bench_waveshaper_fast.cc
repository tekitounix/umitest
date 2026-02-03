/**
 * TB-303 Wave Shaper Benchmark for Cortex-M4
 *
 * Tests current tb303_waveshaper.hpp implementations:
 * - Newton2: Standard 2-iteration solver
 * - Fast1: 1 iteration with relaxed damping
 * - Fast2: 2 iterations with relaxed damping
 * - Turbo: 2 iterations with E-B junction reuse (3 exp calls)
 * - TurboLite: Turbo with Jacobian element reuse
 *
 * Build: xmake build bench_waveshaper_fast
 * Run:   xmake run bench_waveshaper_fast (via Renode)
 */

#include <algorithm>
#include <cmath>
#include <cstdint>

// ============================================================================
// Baremetal stubs
// ============================================================================
#ifdef __arm__
extern "C" {
void __cxa_pure_virtual() {
    while (1)
        ;
}
}
void operator delete(void*, unsigned int) noexcept {}
void operator delete(void*) noexcept {}
#endif

// ============================================================================
// Startup Code and UART for Cortex-M4
// ============================================================================
#ifdef __arm__

extern "C" {
extern uint32_t _sidata;
extern uint32_t _sdata;
extern uint32_t _edata;
extern uint32_t _sbss;
extern uint32_t _ebss;
extern uint32_t _estack;

void Reset_Handler();
void Default_Handler();
int main();
}

__attribute__((section(".isr_vector"), used)) const void* const g_vector_table[] = {
    reinterpret_cast<const void*>(&_estack),
    reinterpret_cast<const void*>(Reset_Handler),
    reinterpret_cast<const void*>(Default_Handler),
    reinterpret_cast<const void*>(Default_Handler),
    reinterpret_cast<const void*>(Default_Handler),
    reinterpret_cast<const void*>(Default_Handler),
    reinterpret_cast<const void*>(Default_Handler),
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    reinterpret_cast<const void*>(Default_Handler),
    nullptr,
    nullptr,
    reinterpret_cast<const void*>(Default_Handler),
    reinterpret_cast<const void*>(Default_Handler),
};

extern "C" __attribute__((noreturn)) void Reset_Handler() {
    uint32_t* src = &_sidata;
    uint32_t* dst = &_sdata;
    while (dst < &_edata) {
        *dst++ = *src++;
    }

    dst = &_sbss;
    while (dst < &_ebss) {
        *dst++ = 0;
    }

    volatile uint32_t& CPACR = *reinterpret_cast<volatile uint32_t*>(0xE000ED88);
    CPACR |= (0xFU << 20);
    asm volatile("dsb\n isb" ::: "memory");

    extern void (*__init_array_start[])();
    extern void (*__init_array_end[])();
    for (auto fn = __init_array_start; fn < __init_array_end; ++fn) {
        (*fn)();
    }

    main();
    while (true) {
        asm volatile("wfi");
    }
}

extern "C" void Default_Handler() {
    while (true) {
        asm volatile("bkpt #0");
    }
}

extern "C" __attribute__((alias("Reset_Handler"))) void _start();

namespace uart {
constexpr uint32_t USART2_BASE = 0x40004400UL;
constexpr uint32_t RCC_APB1ENR = 0x40023840UL;

inline void init() {
    auto* rcc = reinterpret_cast<volatile uint32_t*>(RCC_APB1ENR);
    *rcc |= (1 << 17);

    auto* cr1 = reinterpret_cast<volatile uint32_t*>(USART2_BASE + 0x0C);
    auto* brr = reinterpret_cast<volatile uint32_t*>(USART2_BASE + 0x08);
    *cr1 = 0;
    *brr = 0x0683;
    *cr1 = (1 << 13) | (1 << 3);
}

inline void putc(char c) {
    auto* sr = reinterpret_cast<volatile uint32_t*>(USART2_BASE + 0x00);
    auto* dr = reinterpret_cast<volatile uint32_t*>(USART2_BASE + 0x04);
    while (!(*sr & (1 << 7))) {
    }
    *dr = static_cast<uint32_t>(c);
}

inline void puts(const char* s) {
    while (*s) {
        if (*s == '\n') {
            putc('\r');
        }
        putc(*s++);
    }
}

inline void print_uint(uint32_t v) {
    char buf[12];
    int i = 0;
    if (v == 0) {
        buf[i++] = '0';
    } else {
        while (v > 0) {
            buf[i++] = '0' + (v % 10);
            v /= 10;
        }
    }
    while (i > 0) {
        putc(buf[--i]);
    }
}

inline void print_float(float v, int decimals = 2) {
    if (v < 0) {
        putc('-');
        v = -v;
    }
    auto int_part = static_cast<uint32_t>(v);
    print_uint(int_part);
    putc('.');
    v -= static_cast<float>(int_part);
    for (int d = 0; d < decimals; ++d) {
        v *= 10.0f;
        auto digit = static_cast<int>(v);
        putc('0' + digit);
        v -= static_cast<float>(digit);
    }
}
} // namespace uart

#else
    // Desktop stub
    #include <cstdio>
namespace uart {
inline void init() {}
inline void puts(const char* s) {
    printf("%s", s);
}
inline void print_uint(uint32_t v) {
    printf("%u", v);
}
inline void print_float(float v, int decimals = 2) {
    printf("%.*f", decimals, v);
}
} // namespace uart
#endif

// ============================================================================
// Cycle Counter
// ============================================================================
#ifdef __arm__
namespace dwt {
constexpr uint32_t DWT_CTRL = 0xE0001000UL;
constexpr uint32_t DWT_CYCCNT = 0xE0001004UL;
constexpr uint32_t DEMCR = 0xE000EDFCUL;

inline void enable() {
    auto* demcr = reinterpret_cast<volatile uint32_t*>(DEMCR);
    auto* ctrl = reinterpret_cast<volatile uint32_t*>(DWT_CTRL);
    *demcr |= (1 << 24);
    *ctrl |= 1;
}

inline void reset() {
    auto* cyccnt = reinterpret_cast<volatile uint32_t*>(DWT_CYCCNT);
    *cyccnt = 0;
}

inline uint32_t read() {
    return *reinterpret_cast<volatile uint32_t*>(DWT_CYCCNT);
}
} // namespace dwt
#else
    #include <chrono>
namespace dwt {
static std::chrono::high_resolution_clock::time_point start_time;
inline void enable() {}
inline void reset() {
    start_time = std::chrono::high_resolution_clock::now();
}
inline uint32_t read() {
    auto now = std::chrono::high_resolution_clock::now();
    return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(now - start_time).count());
}
} // namespace dwt
#endif

// ============================================================================
// Include the waveshaper implementations
// ============================================================================
#include "../docs/dsp/tb303/vco/code/tb303_waveshaper.hpp"

// ============================================================================
// Test signal generation
// ============================================================================
constexpr float SAMPLE_RATE = 48000.0f;
constexpr int N_SAMPLES = 4800; // 100ms @ 48kHz

// ============================================================================
// Benchmark template
// ============================================================================
template <typename WaveShaper>
struct BenchResult {
    uint32_t cycles;
    float max_error;
    float rms_error;
};

template <typename WaveShaper>
BenchResult<WaveShaper>
benchmark(WaveShaper& ws, const float* input, float* output, const float* reference, int n_samples) {
    ws.setSampleRate(SAMPLE_RATE);
    ws.reset();

    // Warm-up
    for (int i = 0; i < 100; ++i) {
        volatile float dummy = ws.process(input[i % n_samples]);
        (void)dummy;
    }
    ws.reset();

    // Benchmark
    dwt::reset();
    for (int i = 0; i < n_samples; ++i) {
        output[i] = ws.process(input[i]);
    }
    uint32_t cycles = dwt::read();

    // Calculate error
    float sum_sq = 0.0f;
    float max_err = 0.0f;
    for (int i = 0; i < n_samples; ++i) {
        float err = output[i] - reference[i];
        sum_sq += err * err;
        float abs_err = err < 0 ? -err : err;
        if (abs_err > max_err)
            max_err = abs_err;
    }
    float rms = std::sqrt(sum_sq / n_samples) * 1000.0f; // mV
    max_err *= 1000.0f;                                  // mV

    return {cycles / static_cast<uint32_t>(n_samples), max_err, rms};
}

// ============================================================================
// Main
// ============================================================================
int main() {
    uart::init();
    dwt::enable();

    uart::puts("\n");
    uart::puts("========================================\n");
    uart::puts("TB-303 WaveShaper Fast Benchmark\n");
    uart::puts("(tb303_waveshaper.hpp)\n");
    uart::puts("========================================\n\n");

    uart::puts("Sample rate: ");
    uart::print_uint(static_cast<uint32_t>(SAMPLE_RATE));
    uart::puts(" Hz\n");
    uart::puts("Test samples: ");
    uart::print_uint(N_SAMPLES);
    uart::puts(" (");
    uart::print_float(N_SAMPLES / SAMPLE_RATE * 1000.0f, 0);
    uart::puts(" ms)\n\n");

    // Generate input signal (440Hz sawtooth)
    static float input[N_SAMPLES];
    static float output[N_SAMPLES];
    static float reference[N_SAMPLES];

    for (int i = 0; i < N_SAMPLES; ++i) {
        input[i] = generate_saw(i, 440.0f);
    }

    // Generate reference (100 iterations, std::exp)
    uart::puts("Generating reference (100 iterations)...\n");
    {
        tb303::WaveShaperReference ref;
        ref.setSampleRate(SAMPLE_RATE);
        ref.reset();
        for (int i = 0; i < N_SAMPLES; ++i) {
            reference[i] = ref.process(input[i]);
        }
    }

    uart::puts("\n");
    uart::puts("Model           Cycles/Sample  RMS [mV]  Max [mV]\n");
    uart::puts("------------------------------------------------\n");

    // Newton2
    {
        tb303::WaveShaperNewton<2> ws;
        auto r = benchmark(ws, input, output, reference, N_SAMPLES);
        uart::puts("Newton2         ");
        uart::print_uint(r.cycles);
        uart::puts("            ");
        uart::print_float(r.rms_error, 2);
        uart::puts("     ");
        uart::print_float(r.max_error, 2);
        uart::puts("\n");
    }

    // Fast1
    {
        tb303::WaveShaperFast1 ws;
        auto r = benchmark(ws, input, output, reference, N_SAMPLES);
        uart::puts("Fast1           ");
        uart::print_uint(r.cycles);
        uart::puts("            ");
        uart::print_float(r.rms_error, 2);
        uart::puts("     ");
        uart::print_float(r.max_error, 2);
        uart::puts("\n");
    }

    // Fast2
    {
        tb303::WaveShaperFast2 ws;
        auto r = benchmark(ws, input, output, reference, N_SAMPLES);
        uart::puts("Fast2           ");
        uart::print_uint(r.cycles);
        uart::puts("            ");
        uart::print_float(r.rms_error, 2);
        uart::puts("     ");
        uart::print_float(r.max_error, 2);
        uart::puts("\n");
    }

    // Hybrid
    {
        tb303::WaveShaperHybrid ws;
        auto r = benchmark(ws, input, output, reference, N_SAMPLES);
        uart::puts("Hybrid          ");
        uart::print_uint(r.cycles);
        uart::puts("            ");
        uart::print_float(r.rms_error, 2);
        uart::puts("     ");
        uart::print_float(r.max_error, 2);
        uart::puts("\n");
    }

    // Turbo
    {
        tb303::WaveShaperTurbo ws;
        auto r = benchmark(ws, input, output, reference, N_SAMPLES);
        uart::puts("Turbo           ");
        uart::print_uint(r.cycles);
        uart::puts("            ");
        uart::print_float(r.rms_error, 2);
        uart::puts("     ");
        uart::print_float(r.max_error, 2);
        uart::puts("\n");
    }

    // TurboLite
    {
        tb303::WaveShaperTurboLite ws;
        auto r = benchmark(ws, input, output, reference, N_SAMPLES);
        uart::puts("TurboLite       ");
        uart::print_uint(r.cycles);
        uart::puts("            ");
        uart::print_float(r.rms_error, 2);
        uart::puts("     ");
        uart::print_float(r.max_error, 2);
        uart::puts("\n");
    }

    uart::puts("\n");
    uart::puts("========================================\n");
    uart::puts("Benchmark complete\n");
    uart::puts("========================================\n");

    while (true) {
#ifdef __arm__
        asm volatile("wfi");
#endif
    }

    return 0;
}
