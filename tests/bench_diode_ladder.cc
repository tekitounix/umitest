/**
 * Diode Ladder Filter Benchmark for Cortex-M4
 *
 * Compares three implementations:
 * 1. Original (Karrikuh-style, per-sample coefficient calculation)
 * 2. Method 2 (D factorization)
 * 3. Method 3 (Fully optimized with shared sub-terms)
 *
 * Build: xmake build bench_diode_ladder
 * Run:   xmake run bench_diode_ladder (via Renode)
 */

#include <cstdint>
#include <cmath>
#include <algorithm>

// ============================================================================
// Startup Code and UART for Cortex-M4 (Standalone benchmark)
// ============================================================================
#ifdef __arm__

// Linker symbols
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

// Vector Table
__attribute__((section(".isr_vector"), used))
const void* const g_vector_table[] = {
    reinterpret_cast<const void*>(&_estack),
    reinterpret_cast<const void*>(Reset_Handler),
    reinterpret_cast<const void*>(Default_Handler),  // NMI
    reinterpret_cast<const void*>(Default_Handler),  // HardFault
    reinterpret_cast<const void*>(Default_Handler),  // MemManage
    reinterpret_cast<const void*>(Default_Handler),  // BusFault
    reinterpret_cast<const void*>(Default_Handler),  // UsageFault
    nullptr, nullptr, nullptr, nullptr,
    reinterpret_cast<const void*>(Default_Handler),  // SVC
    nullptr, nullptr,
    reinterpret_cast<const void*>(Default_Handler),  // PendSV
    reinterpret_cast<const void*>(Default_Handler),  // SysTick
};

extern "C" __attribute__((noreturn)) void Reset_Handler() {
    // Copy .data
    uint32_t* src = &_sidata;
    uint32_t* dst = &_sdata;
    while (dst < &_edata) { *dst++ = *src++; }

    // Zero .bss
    dst = &_sbss;
    while (dst < &_ebss) { *dst++ = 0; }

    // Enable FPU (Cortex-M4F)
    volatile uint32_t& CPACR = *reinterpret_cast<volatile uint32_t*>(0xE000ED88);
    CPACR |= (0xFU << 20);
    asm volatile("dsb\n isb" ::: "memory");

    // Call global constructors
    extern void (*__init_array_start[])();
    extern void (*__init_array_end[])();
    for (auto fn = __init_array_start; fn < __init_array_end; ++fn) { (*fn)(); }

    main();
    while (true) { asm volatile("wfi"); }
}

extern "C" void Default_Handler() {
    while (true) { asm volatile("bkpt #0"); }
}

// Provide _start as alias
extern "C" __attribute__((alias("Reset_Handler"))) void _start();

// UART2 output
namespace uart {
constexpr uint32_t USART2_BASE = 0x40004400UL;
constexpr uint32_t RCC_APB1ENR = 0x40023840UL;

inline void init() {
    auto* rcc = reinterpret_cast<volatile uint32_t*>(RCC_APB1ENR);
    *rcc |= (1 << 17);  // Enable USART2 clock

    auto* cr1 = reinterpret_cast<volatile uint32_t*>(USART2_BASE + 0x0C);
    auto* brr = reinterpret_cast<volatile uint32_t*>(USART2_BASE + 0x08);
    *cr1 = 0;
    *brr = 0x0683;       // 115200 baud @ 16MHz
    *cr1 = (1 << 13) | (1 << 3);  // UE | TE
}

inline void putc(char c) {
    auto* sr = reinterpret_cast<volatile uint32_t*>(USART2_BASE + 0x00);
    auto* dr = reinterpret_cast<volatile uint32_t*>(USART2_BASE + 0x04);
    while (!(*sr & (1 << 7))) {}  // Wait TXE
    *dr = static_cast<uint32_t>(c);
}

inline void puts(const char* s) {
    while (*s) {
        if (*s == '\n') { putc('\r'); }
        putc(*s++);
    }
}

inline void print_uint(uint32_t v) {
    char buf[12];
    int i = 0;
    if (v == 0) { buf[i++] = '0'; }
    else {
        while (v > 0) { buf[i++] = '0' + (v % 10); v /= 10; }
    }
    while (i > 0) { putc(buf[--i]); }
}

inline void print_float(float v, int decimals = 1) {
    if (v < 0) { putc('-'); v = -v; }
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

#endif // __arm__

// ============================================================================
// Cortex-M4 Cycle Counter (DWT)
// ============================================================================
namespace dwt {

#ifdef __arm__
inline volatile uint32_t* const DWT_CTRL   = reinterpret_cast<volatile uint32_t*>(0xE0001000);
inline volatile uint32_t* const DWT_CYCCNT = reinterpret_cast<volatile uint32_t*>(0xE0001004);
inline volatile uint32_t* const SCB_DEMCR  = reinterpret_cast<volatile uint32_t*>(0xE000EDFC);

inline void enable() {
    *SCB_DEMCR |= 0x01000000;  // Enable DWT
    *DWT_CYCCNT = 0;
    *DWT_CTRL |= 1;            // Enable cycle counter
}

inline uint32_t cycles() { return *DWT_CYCCNT; }
inline void reset() { *DWT_CYCCNT = 0; }
#else
// Host fallback (no cycle counter)
inline void enable() {}
inline uint32_t cycles() { return 0; }
inline void reset() {}
#endif

} // namespace dwt

// ============================================================================
// Common
// ============================================================================
constexpr float PI = 3.14159265358979323846f;

inline float clip(float x) {
    return x / (1.0f + std::abs(x));
}

// ============================================================================
// s0 Calculation Benchmark - Isolated s0 derivation only
// ============================================================================

// Shared state for all methods
struct FilterState {
    float z[4] = {0.1f, 0.2f, 0.3f, 0.4f};  // Non-zero for realistic test
};

// Method 1: Original (Karrikuh expanded form)
// s0 = (z[0]*a²*a + z[1]*a²*b + z[2]*(b²-2a²)*a + z[3]*(b²-3a²)*b) * c
struct S0_Original {
    static float compute(float a, float a2, float b, float b2, float c, const float* z) {
        return (a2 * a * z[0]
              + a2 * b * z[1]
              + z[2] * (b2 - 2.0f * a2) * a
              + z[3] * (b2 - 3.0f * a2) * b) * c;
    }
};

// Method 2: D Factorization
// D = b² - 2a², s0 = c * (a² * (a*z0 + b*(z1-z3)) + D * (a*z2 + b*z3))
struct S0_DFactor {
    static float compute(float a, float a2, float b, float b2, float c, const float* z) {
        float D = b2 - 2.0f * a2;
        float term1 = a * z[0] + b * (z[1] - z[3]);
        float term2 = a * z[2] + b * z[3];
        return c * (a2 * term1 + D * term2);
    }
};

// ============================================================================
// 近似逆数を使った実装
// ============================================================================

// Fast inverse square root (Quake III style) adapted for reciprocal
// Initial approximation using integer bit manipulation
inline float fast_recip_initial(float x) {
    union { float f; uint32_t i; } u = {x};
    // Magic number for reciprocal: 0x7EF311C3
    u.i = 0x7EF311C3 - u.i;
    return u.f;
}

// Fast reciprocal with 1 Newton-Raphson iteration
// Error: ~0.1%
inline float fast_recip(float x) {
    float r = fast_recip_initial(x);
    // Newton-Raphson: r = r * (2 - x * r)
    r = r * (2.0f - x * r);
    return r;
}

// Fast reciprocal with 2 Newton-Raphson iterations
// Error: ~0.0001%
inline float fast_recip_2nr(float x) {
    float r = fast_recip_initial(x);
    r = r * (2.0f - x * r);
    r = r * (2.0f - x * r);
    return r;
}

// Fast reciprocal without Newton-Raphson (lowest precision)
// Error: ~12%
inline float fast_recip_approx(float x) {
    return fast_recip_initial(x);
}

// Method 3: D Factorization + fast_recip (1 NR iteration)
struct S0_DFactor_FastRecip {
    static float compute(float a, float a2, float b, float b2, float /*c_unused*/, const float* z) {
        float D = b2 - 2.0f * a2;
        float denom = D * D - 2.0f * a2 * a2;
        float c = fast_recip(denom);
        float term1 = a * z[0] + b * (z[1] - z[3]);
        float term2 = a * z[2] + b * z[3];
        return c * (a2 * term1 + D * term2);
    }
};

// Method 4: D Factorization + fast_recip_approx (no NR, lowest precision)
struct S0_DFactor_Approx {
    static float compute(float a, float a2, float b, float b2, float /*c_unused*/, const float* z) {
        float D = b2 - 2.0f * a2;
        float denom = D * D - 2.0f * a2 * a2;
        float c = fast_recip_approx(denom);
        float term1 = a * z[0] + b * (z[1] - z[3]);
        float term2 = a * z[2] + b * z[3];
        return c * (a2 * term1 + D * term2);
    }
};

// ============================================================================
// Benchmark Runner - s0 calculation only
// ============================================================================
constexpr int ITERATIONS = 100000;
constexpr float TEST_FC = 0.1f;

volatile float sink = 0;  // Prevent optimization

// Prevent inlining to see actual generated code
template<typename S0Method>
__attribute__((noinline))
float compute_s0_wrapper(float a, float a2, float b, float b2, float c, float* z) {
    return S0Method::compute(a, a2, b, b2, c, z);
}

template<typename S0Method>
uint32_t benchmark_s0(const char* name) {
    // Pre-compute coefficients (same for all methods)
    volatile float fc = TEST_FC;  // volatile to prevent constant folding
    float a = PI * fc;
    float a2 = a * a;
    float b = 2.0f * a + 1.0f;
    float b2 = b * b;
    float c = 1.0f / (2.0f * a2 * a2 - 4.0f * a2 * b2 + b2 * b2);

    // State array - all elements change
    float z[4] = {0.1f, 0.2f, 0.3f, 0.4f};

    dwt::reset();
    uint32_t start = dwt::cycles();

    for (int i = 0; i < ITERATIONS; ++i) {
        float result = compute_s0_wrapper<S0Method>(a, a2, b, b2, c, z);
        sink = result;
        // Perturb ALL state elements to prevent partial optimization
        z[0] = result * 0.001f + 0.1f;
        z[1] = result * 0.002f + 0.2f;
        z[2] = result * 0.003f + 0.3f;
        z[3] = result * 0.004f + 0.4f;
    }

    uint32_t end = dwt::cycles();
    uint32_t total = end - start;
    uint32_t per_iter = total / ITERATIONS;

#ifdef __arm__
    uart::puts(name);
    uart::puts(": ");
    uart::print_uint(per_iter);
    uart::puts(" cycles/iter (total: ");
    uart::print_uint(total);
    uart::puts(")\n");
#endif

    return per_iter;
}

// Compute reference value and error
void measure_error() {
    volatile float fc = TEST_FC;
    float a = PI * fc;
    float a2 = a * a;
    float b = 2.0f * a + 1.0f;
    float b2 = b * b;
    float c = 1.0f / (2.0f * a2 * a2 - 4.0f * a2 * b2 + b2 * b2);

    float z[4] = {0.1f, 0.2f, 0.3f, 0.4f};

    // Reference (exact division)
    float ref = S0_Original::compute(a, a2, b, b2, c, z);

    // D-Factor (exact)
    float d_exact = S0_DFactor::compute(a, a2, b, b2, c, z);

    // Fast recip (1 NR)
    float d_fast = S0_DFactor_FastRecip::compute(a, a2, b, b2, c, z);

    // Approx (no NR)
    float d_approx = S0_DFactor_Approx::compute(a, a2, b, b2, c, z);

#ifdef __arm__
    uart::puts("--- Error Analysis ---\n");
    uart::puts("Reference (Original): ");
    uart::print_float(ref, 6);
    uart::puts("\n");

    uart::puts("D-Factor exact:       ");
    uart::print_float(d_exact, 6);
    uart::puts(" (err: ");
    uart::print_float((d_exact - ref) / ref * 100.0f, 6);
    uart::puts("%)\n");

    uart::puts("D-Factor + FastRecip: ");
    uart::print_float(d_fast, 6);
    uart::puts(" (err: ");
    uart::print_float((d_fast - ref) / ref * 100.0f, 6);
    uart::puts("%)\n");

    uart::puts("D-Factor + Approx:    ");
    uart::print_float(d_approx, 6);
    uart::puts(" (err: ");
    uart::print_float((d_approx - ref) / ref * 100.0f, 6);
    uart::puts("%)\n\n");
#endif
}

int main() {
#ifdef __arm__
    uart::init();

    uart::puts("\n");
    uart::puts("===========================================\n");
    uart::puts("s0 Calculation Benchmark (Cortex-M4)\n");
    uart::puts("===========================================\n");
    uart::puts("Iterations: ");
    uart::print_uint(ITERATIONS);
    uart::puts("\nCutoff fc: 0.1 (normalized)\n\n");

    // Error analysis first
    measure_error();

    dwt::enable();

    uart::puts("--- Speed Benchmark ---\n");
    uint32_t c1 = benchmark_s0<S0_Original>("Original");
    uint32_t c2 = benchmark_s0<S0_DFactor>("D-Factor");
    uint32_t c3 = benchmark_s0<S0_DFactor_FastRecip>("FastRecip");
    uint32_t c4 = benchmark_s0<S0_DFactor_Approx>("Approx");

    uart::puts("\n--- Speed Results ---\n");
    uart::puts("D-Factor vs Original:  ");
    uart::print_float(100.0f * static_cast<float>(c2) / static_cast<float>(c1), 1);
    uart::puts("% (");
    uart::print_float(static_cast<float>(c1) / static_cast<float>(c2), 2);
    uart::puts("x)\n");

    uart::puts("FastRecip vs Original: ");
    uart::print_float(100.0f * static_cast<float>(c3) / static_cast<float>(c1), 1);
    uart::puts("% (");
    uart::print_float(static_cast<float>(c1) / static_cast<float>(c3), 2);
    uart::puts("x)\n");

    uart::puts("Approx vs Original:    ");
    uart::print_float(100.0f * static_cast<float>(c4) / static_cast<float>(c1), 1);
    uart::puts("% (");
    uart::print_float(static_cast<float>(c1) / static_cast<float>(c4), 2);
    uart::puts("x)\n");

    uart::puts("\n=== BENCHMARK COMPLETE ===\n");

    // Halt for Renode
    while (true) {
        asm volatile("wfi");
    }
#endif

    return 0;
}
