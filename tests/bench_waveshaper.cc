/**
 * TB-303 Wave Shaper Benchmark for Cortex-M4
 *
 * Compares implementations:
 * 1. WaveShaperOneIter (1回Newton + 4x4疎行列)
 * 2. WaveShaperSchur (Schur補行列縮約)
 *
 * Build: xmake build bench_waveshaper
 * Run:   xmake run bench_waveshaper (via Renode)
 */

#include <cstdint>
#include <cmath>
#include <algorithm>

// ============================================================================
// Baremetal stubs for operator delete (required by chowdsp_wdf virtual dtors)
// ============================================================================
#ifdef __arm__
extern "C" {
void __cxa_pure_virtual() { while(1); }
}
void operator delete(void*, unsigned int) noexcept {}
void operator delete(void*) noexcept {}
#endif

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
// TB-303 Wave Shaper Common
// ============================================================================
namespace tb303 {

// 回路定数
constexpr float V_CC = 12.0f;
constexpr float V_COLL = 5.33f;
constexpr float R2 = 100e3f;
constexpr float R3 = 10e3f;
constexpr float R4 = 22e3f;
constexpr float R5 = 10e3f;
constexpr float C1 = 10e-9f;
constexpr float C2 = 1e-6f;

// トランジスタパラメータ
constexpr float V_T = 0.025865f;
constexpr float V_T_INV = 1.0f / V_T;
constexpr float I_S = 1e-13f;
constexpr float BETA_F = 100.0f;
constexpr float ALPHA_F = BETA_F / (BETA_F + 1.0f);
constexpr float ALPHA_R = 0.5f / 1.5f;
constexpr float V_CRIT = V_T * 40.0f;

// 事前計算コンダクタンス
constexpr float G2 = 1.0f / R2;
constexpr float G3 = 1.0f / R3;
constexpr float G4 = 1.0f / R4;
constexpr float G5 = 1.0f / R5;

// diode_iv用のexp_crit定数（事前計算）
// V_CRIT = V_T * 40 での値
// exp(40) ≈ 2.35e17
constexpr float EXP_CRIT = 2.35385266837019985e17f;  // exp(40)
constexpr float G_CRIT = I_S * V_T_INV * EXP_CRIT;   // Is/Vt * exp(V_crit/Vt)
constexpr float I_CRIT = I_S * (EXP_CRIT - 1.0f);    // Is * (exp(V_crit/Vt) - 1)

// =============================================================================
// 高速exp近似 (Schraudolph改良版)
// =============================================================================
inline float fast_exp(float x) {
    x = std::clamp(x, -87.0f, 88.0f);

    union { float f; int32_t i; } u;
    constexpr float LOG2E = 1.4426950408889634f;
    constexpr float SHIFT = (1 << 23) * 127.0f;
    constexpr float SCALE = (1 << 23) * LOG2E;

    u.i = static_cast<int32_t>(SCALE * x + SHIFT);

    // 2次補正
    float t = x - static_cast<float>(u.i - static_cast<int32_t>(SHIFT)) / SCALE;
    u.f *= 1.0f + t * (1.0f + t * 0.5f);

    return u.f;
}

// =============================================================================
// mo::pow2 (ビット操作, basic_math.hhより)
// IEEE754ビット操作 + 多項式補正
// =============================================================================
inline float mo_pow2(float x) {
    float f = static_cast<float>((x - 0.5f) + (3 << 22));
    int32_t i = std::bit_cast<int32_t>(f);
    const int32_t ix = i - 0x4b400000;
    const float dx = static_cast<float>(x - static_cast<float>(ix));
    x = 1.0f + dx * (0.6960656421638072f + dx * (0.224494337302845f + dx * 0.07944023841053369f));
    i = std::bit_cast<int32_t>(static_cast<float>(x));
    i += (ix << 23);
    return std::bit_cast<float>(i);
}

// mo::exp using pow2: exp(x) = pow2(x * log2(e))
inline float mo_exp(float x) {
    constexpr float LOG2E = 1.4426950408889634f;
    x = std::clamp(x, -87.0f, 88.0f);
    return mo_pow2(x * LOG2E);
}

// mo::exp from basic_math.hh - Padé [4,4] without range reduction
// Only accurate for small |x|, needs range reduction for general use
inline float mo_exp_pade44(float x) noexcept {
    const auto num = 1680.0f + x * (840.0f + x * (180.0f + x * (20.0f + x)));
    const auto den = 1680.0f + x * (-840.0f + x * (180.0f + x * (-20.0f + x)));
    return num / den;
}

// mo::exp with range reduction using Padé [4,4]
inline float mo_exp_pade44_ranged(float x) noexcept {
    x = std::clamp(x, -87.0f, 88.0f);
    constexpr float LOG2E = 1.4426950408889634f;
    constexpr float LN2 = 0.6931471805599453f;

    float n_f = std::floor(x * LOG2E + 0.5f);
    int n = static_cast<int>(n_f);
    float r = x - n_f * LN2;  // |r| < ln(2)/2 ≈ 0.347

    // Padé [4,4] for exp(r)
    const auto num = 1680.0f + r * (840.0f + r * (180.0f + r * (20.0f + r)));
    const auto den = 1680.0f + r * (-840.0f + r * (180.0f + r * (-20.0f + r)));
    float exp_r = num / den;

    // Multiply by 2^n using bit manipulation
    union { float f; int32_t i; } u;
    u.i = (127 + n) << 23;
    return u.f * exp_r;
}

// =============================================================================
// パデ近似 [2,2] + レンジリダクション
// =============================================================================
inline float pade_exp(float x) {
    x = std::clamp(x, -87.0f, 88.0f);
    constexpr float LOG2E = 1.4426950408889634f;
    constexpr float LN2 = 0.6931471805599453f;

    float n_f = std::floor(x * LOG2E + 0.5f);
    int n = static_cast<int>(n_f);
    float r = x - n_f * LN2;  // |r| < ln(2)/2

    // パデ [2,2]: exp(r) ≈ (12 + 6r + r²) / (12 - 6r + r²)
    float r2 = r * r;
    float num = 12.0f + 6.0f * r + r2;
    float den = 12.0f - 6.0f * r + r2;
    float exp_r = num / den;

    // 2^n をビット操作で計算
    union { float f; int32_t i; } u;
    u.i = (127 + n) << 23;

    return u.f * exp_r;
}

// =============================================================================
// LUT + 線形補間
// =============================================================================
class ExpLUT {
public:
    static constexpr int LUT_SIZE = 1024;
    static constexpr float X_MIN = -10.0f;
    static constexpr float X_MAX = 50.0f;
    static constexpr float X_RANGE = X_MAX - X_MIN;
    static constexpr float SCALE = static_cast<float>(LUT_SIZE - 1) / X_RANGE;
    static constexpr float INV_SCALE = X_RANGE / static_cast<float>(LUT_SIZE - 1);

    float lut_[LUT_SIZE];

    ExpLUT() {
        for (int i = 0; i < LUT_SIZE; ++i) {
            float x = X_MIN + i * INV_SCALE;
            lut_[i] = std::exp(x);
        }
    }

    inline float operator()(float x) const {
        if (x <= X_MIN) return lut_[0];
        if (x >= X_MAX) return lut_[LUT_SIZE - 1];

        float idx_f = (x - X_MIN) * SCALE;
        int idx = static_cast<int>(idx_f);
        float frac = idx_f - static_cast<float>(idx);

        return lut_[idx] * (1.0f - frac) + lut_[idx + 1] * frac;
    }
};

// グローバルLUTインスタンス
inline ExpLUT& getExpLUT() {
    static ExpLUT lut;
    return lut;
}

inline float lut_exp(float x) {
    return getExpLUT()(x);
}

// =============================================================================
// ダイオード電流・コンダクタンス
// =============================================================================
inline void diode_iv(float v, float& i, float& g) {
    if (v > V_CRIT) {
        float exp_crit = fast_exp(V_CRIT * V_T_INV);
        g = I_S * V_T_INV * exp_crit;
        i = I_S * (exp_crit - 1.0f) + g * (v - V_CRIT);
    } else if (v < -10.0f * V_T) {
        i = -I_S;
        g = 1e-12f;
    } else {
        float exp_v = fast_exp(v * V_T_INV);
        i = I_S * (exp_v - 1.0f);
        g = I_S * V_T_INV * exp_v + 1e-12f;
    }
}

// mo_exp版 (pow2ベース)
inline void diode_iv_mo(float v, float& i, float& g) {
    if (v > V_CRIT) {
        float exp_crit = mo_exp(V_CRIT * V_T_INV);
        g = I_S * V_T_INV * exp_crit;
        i = I_S * (exp_crit - 1.0f) + g * (v - V_CRIT);
    } else if (v < -10.0f * V_T) {
        i = -I_S;
        g = 1e-12f;
    } else {
        float exp_v = mo_exp(v * V_T_INV);
        i = I_S * (exp_v - 1.0f);
        g = I_S * V_T_INV * exp_v + 1e-12f;
    }
}

// パデ近似版
inline void diode_iv_pade(float v, float& i, float& g) {
    if (v > V_CRIT) {
        float exp_crit = pade_exp(V_CRIT * V_T_INV);
        g = I_S * V_T_INV * exp_crit;
        i = I_S * (exp_crit - 1.0f) + g * (v - V_CRIT);
    } else if (v < -10.0f * V_T) {
        i = -I_S;
        g = 1e-12f;
    } else {
        float exp_v = pade_exp(v * V_T_INV);
        i = I_S * (exp_v - 1.0f);
        g = I_S * V_T_INV * exp_v + 1e-12f;
    }
}

// Padé [4,4] + range reduction版
inline void diode_iv_pade44(float v, float& i, float& g) {
    if (v > V_CRIT) {
        float exp_crit = mo_exp_pade44_ranged(V_CRIT * V_T_INV);
        g = I_S * V_T_INV * exp_crit;
        i = I_S * (exp_crit - 1.0f) + g * (v - V_CRIT);
    } else if (v < -10.0f * V_T) {
        i = -I_S;
        g = 1e-12f;
    } else {
        float exp_v = mo_exp_pade44_ranged(v * V_T_INV);
        i = I_S * (exp_v - 1.0f);
        g = I_S * V_T_INV * exp_v + 1e-12f;
    }
}

// LUT版
inline void diode_iv_lut(float v, float& i, float& g) {
    if (v > V_CRIT) {
        float exp_crit = lut_exp(V_CRIT * V_T_INV);
        g = I_S * V_T_INV * exp_crit;
        i = I_S * (exp_crit - 1.0f) + g * (v - V_CRIT);
    } else if (v < -10.0f * V_T) {
        i = -I_S;
        g = 1e-12f;
    } else {
        float exp_v = lut_exp(v * V_T_INV);
        i = I_S * (exp_v - 1.0f);
        g = I_S * V_T_INV * exp_v + 1e-12f;
    }
}

// 最適化版: exp_crit定数を事前計算 (WaveShaper3Var用)
inline void diode_iv_opt(float v, float& i, float& g) {
    if (v > V_CRIT) {
        // 事前計算定数を使用
        g = G_CRIT;
        i = I_CRIT + G_CRIT * (v - V_CRIT);
    } else if (v < -10.0f * V_T) {
        i = -I_S;
        g = 1e-12f;
    } else {
        float exp_v = fast_exp(v * V_T_INV);
        i = I_S * (exp_v - 1.0f);
        g = I_S * V_T_INV * exp_v + 1e-12f;
    }
}

// =============================================================================
// SquareShaper風の高速tanh
// =============================================================================
inline float fast_tanh(float x) noexcept {
    return x / (std::abs(x) + 0.1f);
}

// =============================================================================
// WaveShaperFast: SquareShaper手法を適用した高速版
// - Newton反復なし (Forward Euler)
// - 2状態変数 (v_c1, v_c2)
// - Forward-Active領域仮定 + ソフトクリップ飽和
// - exp 1回
// =============================================================================
class WaveShaperFast {
public:
    void setSampleRate(float sampleRate) {
        dt_ = 1.0f / sampleRate;
        // コンデンサの離散化係数
        g_c1_ = dt_ / C1;  // Forward Euler: dV = dt/C * I
        g_c2_ = dt_ / C2;
    }

    void reset() {
        v_c1_ = 0.0f;
        v_c2_ = 8.0f;  // 初期バイアス
    }

    __attribute__((noinline))
    float process(float v_in) {
        // C1後の電圧 (入力カップリング)
        const float v_cap = v_in - v_c1_;

        // ベース電圧: R2/R3分圧から推定 (簡略化)
        // 実際は v_cap と V_CC の分圧
        const float v_b = (v_cap * G3 + V_CC * G2) / (G2 + G3);

        // エミッタ電圧 = C2の電圧
        const float v_e = v_c2_;

        // BE接合電圧
        const float v_be = v_b - v_e;

        // ベース電流 (Shockleyダイオード、Forward-Active仮定)
        // クランプして安定化
        const float v_be_clamped = std::clamp(v_be, -1.0f, 0.8f);
        const float exp_vbe = fast_exp(v_be_clamped * V_T_INV);
        float i_b = I_S * (exp_vbe - 1.0f);
        i_b = std::clamp(i_b, -1e-6f, 1e-3f);  // 電流リミット

        // コレクタ電流 = β × Ib (Forward-Active)
        float i_c_ideal = BETA_F * i_b;

        // 飽和補正 (SquareShaper風のソフトクリップ)
        // コレクタ電圧 ≈ V_COLL - R5*Ic
        const float v_c_est = V_COLL - R5 * std::abs(i_c_ideal);
        const float v_ce = std::max(v_c_est - v_e, 0.01f);

        // 飽和領域でのソフトクリップ
        const float sat_factor = fast_tanh(v_ce * 10.0f);  // Vce_sat ≈ 0.1V
        const float i_c = i_c_ideal * sat_factor;

        // エミッタ電流
        const float i_e = i_b + i_c;

        // 状態更新 (Forward Euler)
        // C1: 入力からの充電電流
        const float i_c1 = G3 * (v_cap - v_b);
        v_c1_ += g_c1_ * i_c1;

        // C2: エミッタノードのKCL
        // R4からの充電 - Ieの放電
        const float i_charge = (V_CC - v_e) * G4;
        v_c2_ += g_c2_ * (i_charge - i_e);
        v_c2_ = std::clamp(v_c2_, 0.0f, V_CC);

        // 出力 = コレクタ電圧
        return V_COLL - R5 * i_c;
    }

private:
    float v_c1_ = 0.0f;
    float v_c2_ = 8.0f;
    float dt_ = 1.0f / 48000.0f;
    float g_c1_ = 0.0f;
    float g_c2_ = 0.0f;
};

// =============================================================================
// WaveShaperSchur (Schur補行列法)
// =============================================================================
class WaveShaperSchur {
public:
    void setSampleRate(float sampleRate) {
        dt_ = 1.0f / sampleRate;
        g_c1_ = C1 / dt_;
        g_c2_ = C2 / dt_;

        float j11 = -g_c1_ - G3;
        inv_j11_ = 1.0f / j11;
        schur_j11_factor_ = G3 * G3 * inv_j11_;
        schur_f1_factor_ = G3 * inv_j11_;
    }

    void reset() {
        v_c1_ = 0.0f;
        v_c2_ = 8.0f;
        v_b_ = 8.0f;
        v_e_ = 8.0f;
        v_c_ = V_COLL;
    }

    __attribute__((noinline))
    float process(float v_in) {
        const float v_c1_prev = v_c1_;
        const float v_c2_prev = v_c2_;

        float v_cap = v_in - v_c1_prev;
        float v_b = v_b_;
        float v_e = v_c2_prev;
        float v_c = v_c_;

        // ダイオード評価
        float v_eb = v_e - v_b;
        float v_cb = v_c - v_b;

        float i_ef, g_ef, i_cr, g_cr;
        diode_iv(v_eb, i_ef, g_ef);
        diode_iv(v_cb, i_cr, g_cr);

        // Ebers-Moll電流
        float i_e = i_ef - ALPHA_R * i_cr;
        float i_c = ALPHA_F * i_ef - i_cr;
        float i_b = i_e - i_c;

        // KCL残差
        float f1 = g_c1_ * (v_in - v_cap - v_c1_prev) - G3 * (v_cap - v_b);
        float f2 = G2 * (v_in - v_b) + G3 * (v_cap - v_b) + i_b;
        float f3 = G4 * (V_CC - v_e) - i_e - g_c2_ * (v_e - v_c2_prev);
        float f4 = G5 * (V_COLL - v_c) + i_c;

        // ヤコビアン
        float j22 = -G2 - G3 - (1.0f - ALPHA_F) * g_ef - (1.0f - ALPHA_R) * g_cr;
        float j23 = (1.0f - ALPHA_F) * g_ef;
        float j24 = (1.0f - ALPHA_R) * g_cr;
        float j32 = g_ef - ALPHA_R * g_cr;
        float j33 = -G4 - g_ef - g_c2_;
        float j34 = ALPHA_R * g_cr;
        float j42 = -ALPHA_F * g_ef + g_cr;
        float j43 = ALPHA_F * g_ef;
        float j44 = -G5 - g_cr;

        // Schur縮約
        float j22_p = j22 - schur_j11_factor_;
        float f2_p = f2 - schur_f1_factor_ * f1;

        float inv_j44 = 1.0f / j44;
        float j24_inv = j24 * inv_j44;
        float j34_inv = j34 * inv_j44;

        float j22_pp = j22_p - j24_inv * j42;
        float j23_pp = j23 - j24_inv * j43;
        float f2_pp = f2_p + j24_inv * f4;

        float j32_pp = j32 - j34_inv * j42;
        float j33_pp = j33 - j34_inv * j43;
        float f3_pp = f3 + j34_inv * f4;

        // 2x2 Cramer
        float det = j22_pp * j33_pp - j23_pp * j32_pp;
        float inv_det = 1.0f / det;

        float dv_b = (j33_pp * (-f2_pp) - j23_pp * (-f3_pp)) * inv_det;
        float dv_e = (j22_pp * (-f3_pp) - j32_pp * (-f2_pp)) * inv_det;

        // 後退代入
        float dv_c = (-f4 - j42 * dv_b - j43 * dv_e) * inv_j44;
        float dv_cap = (-f1 - G3 * dv_b) * inv_j11_;

        // ダンピング
        float max_dv = std::max({std::abs(dv_cap), std::abs(dv_b),
                                 std::abs(dv_e), std::abs(dv_c)});
        float damp = (max_dv > 0.5f) ? 0.5f / max_dv : 1.0f;

        v_cap += damp * dv_cap;
        v_b += damp * dv_b;
        v_e = std::clamp(v_e + damp * dv_e, 0.0f, V_CC + 0.5f);
        v_c = std::clamp(v_c + damp * dv_c, 0.0f, V_CC + 0.5f);

        // 状態更新
        v_c1_ = v_in - v_cap;
        v_c2_ = v_e;
        v_b_ = v_b;
        v_e_ = v_e;
        v_c_ = v_c;

        return v_c;
    }

private:
    float v_c1_ = 0.0f, v_c2_ = 8.0f;
    float v_b_ = 8.0f, v_e_ = 8.0f, v_c_ = V_COLL;
    float dt_ = 1.0f / 48000.0f;
    float g_c1_ = C1 / dt_;
    float g_c2_ = C2 / dt_;
    float inv_j11_ = 0.0f;
    float schur_j11_factor_ = 0.0f;
    float schur_f1_factor_ = 0.0f;
};

// =============================================================================
// WaveShaperPade: パデ近似 [4,4] + range reduction + Schur縮約
// =============================================================================
class WaveShaperPade {
public:
    void setSampleRate(float sampleRate) {
        dt_ = 1.0f / sampleRate;
        g_c1_ = C1 / dt_;
        g_c2_ = C2 / dt_;

        float j11 = -g_c1_ - G3;
        inv_j11_ = 1.0f / j11;
        schur_j11_factor_ = G3 * G3 * inv_j11_;
        schur_f1_factor_ = G3 * inv_j11_;
    }

    void reset() {
        v_c1_ = 0.0f;
        v_c2_ = 8.0f;
        v_b_ = 8.0f;
        v_e_ = 8.0f;
        v_c_ = V_COLL;
    }

    __attribute__((noinline))
    float process(float v_in) {
        const float v_c1_prev = v_c1_;
        const float v_c2_prev = v_c2_;

        float v_cap = v_in - v_c1_prev;
        float v_b = v_b_;
        float v_e = v_c2_prev;
        float v_c = v_c_;

        float v_eb = v_e - v_b;
        float v_cb = v_c - v_b;

        float i_ef, g_ef, i_cr, g_cr;
        diode_iv_pade44(v_eb, i_ef, g_ef);
        diode_iv_pade44(v_cb, i_cr, g_cr);

        float i_e = i_ef - ALPHA_R * i_cr;
        float i_c = ALPHA_F * i_ef - i_cr;
        float i_b = i_e - i_c;

        float f1 = g_c1_ * (v_in - v_cap - v_c1_prev) - G3 * (v_cap - v_b);
        float f2 = G2 * (v_in - v_b) + G3 * (v_cap - v_b) + i_b;
        float f3 = G4 * (V_CC - v_e) - i_e - g_c2_ * (v_e - v_c2_prev);
        float f4 = G5 * (V_COLL - v_c) + i_c;

        float j22 = -G2 - G3 - (1.0f - ALPHA_F) * g_ef - (1.0f - ALPHA_R) * g_cr;
        float j23 = (1.0f - ALPHA_F) * g_ef;
        float j24 = (1.0f - ALPHA_R) * g_cr;
        float j32 = g_ef - ALPHA_R * g_cr;
        float j33 = -G4 - g_ef - g_c2_;
        float j34 = ALPHA_R * g_cr;
        float j42 = -ALPHA_F * g_ef + g_cr;
        float j43 = ALPHA_F * g_ef;
        float j44 = -G5 - g_cr;

        float j22_p = j22 - schur_j11_factor_;
        float f2_p = f2 - schur_f1_factor_ * f1;

        float inv_j44 = 1.0f / j44;
        float j24_inv = j24 * inv_j44;
        float j34_inv = j34 * inv_j44;

        float j22_pp = j22_p - j24_inv * j42;
        float j23_pp = j23 - j24_inv * j43;
        float f2_pp = f2_p + j24_inv * f4;

        float j32_pp = j32 - j34_inv * j42;
        float j33_pp = j33 - j34_inv * j43;
        float f3_pp = f3 + j34_inv * f4;

        float det = j22_pp * j33_pp - j23_pp * j32_pp;
        float inv_det = 1.0f / det;

        float dv_b = (j33_pp * (-f2_pp) - j23_pp * (-f3_pp)) * inv_det;
        float dv_e = (j22_pp * (-f3_pp) - j32_pp * (-f2_pp)) * inv_det;

        float dv_c = (-f4 - j42 * dv_b - j43 * dv_e) * inv_j44;
        float dv_cap = (-f1 - G3 * dv_b) * inv_j11_;

        float max_dv = std::max({std::abs(dv_cap), std::abs(dv_b),
                                 std::abs(dv_e), std::abs(dv_c)});
        float damp = (max_dv > 0.5f) ? 0.5f / max_dv : 1.0f;

        v_cap += damp * dv_cap;
        v_b += damp * dv_b;
        v_e = std::clamp(v_e + damp * dv_e, 0.0f, V_CC + 0.5f);
        v_c = std::clamp(v_c + damp * dv_c, 0.0f, V_CC + 0.5f);

        v_c1_ = v_in - v_cap;
        v_c2_ = v_e;
        v_b_ = v_b;
        v_e_ = v_e;
        v_c_ = v_c;

        return v_c;
    }

private:
    float v_c1_ = 0.0f, v_c2_ = 8.0f;
    float v_b_ = 8.0f, v_e_ = 8.0f, v_c_ = V_COLL;
    float dt_ = 1.0f / 48000.0f;
    float g_c1_ = C1 / dt_;
    float g_c2_ = C2 / dt_;
    float inv_j11_ = 0.0f;
    float schur_j11_factor_ = 0.0f;
    float schur_f1_factor_ = 0.0f;
};

// =============================================================================
// WaveShaperLUT: LUT + 線形補間 + Schur縮約
// =============================================================================
class WaveShaperLUT {
public:
    void setSampleRate(float sampleRate) {
        dt_ = 1.0f / sampleRate;
        g_c1_ = C1 / dt_;
        g_c2_ = C2 / dt_;

        float j11 = -g_c1_ - G3;
        inv_j11_ = 1.0f / j11;
        schur_j11_factor_ = G3 * G3 * inv_j11_;
        schur_f1_factor_ = G3 * inv_j11_;
    }

    void reset() {
        v_c1_ = 0.0f;
        v_c2_ = 8.0f;
        v_b_ = 8.0f;
        v_e_ = 8.0f;
        v_c_ = V_COLL;
    }

    __attribute__((noinline))
    float process(float v_in) {
        const float v_c1_prev = v_c1_;
        const float v_c2_prev = v_c2_;

        float v_cap = v_in - v_c1_prev;
        float v_b = v_b_;
        float v_e = v_c2_prev;
        float v_c = v_c_;

        float v_eb = v_e - v_b;
        float v_cb = v_c - v_b;

        float i_ef, g_ef, i_cr, g_cr;
        diode_iv_lut(v_eb, i_ef, g_ef);
        diode_iv_lut(v_cb, i_cr, g_cr);

        float i_e = i_ef - ALPHA_R * i_cr;
        float i_c = ALPHA_F * i_ef - i_cr;
        float i_b = i_e - i_c;

        float f1 = g_c1_ * (v_in - v_cap - v_c1_prev) - G3 * (v_cap - v_b);
        float f2 = G2 * (v_in - v_b) + G3 * (v_cap - v_b) + i_b;
        float f3 = G4 * (V_CC - v_e) - i_e - g_c2_ * (v_e - v_c2_prev);
        float f4 = G5 * (V_COLL - v_c) + i_c;

        float j22 = -G2 - G3 - (1.0f - ALPHA_F) * g_ef - (1.0f - ALPHA_R) * g_cr;
        float j23 = (1.0f - ALPHA_F) * g_ef;
        float j24 = (1.0f - ALPHA_R) * g_cr;
        float j32 = g_ef - ALPHA_R * g_cr;
        float j33 = -G4 - g_ef - g_c2_;
        float j34 = ALPHA_R * g_cr;
        float j42 = -ALPHA_F * g_ef + g_cr;
        float j43 = ALPHA_F * g_ef;
        float j44 = -G5 - g_cr;

        float j22_p = j22 - schur_j11_factor_;
        float f2_p = f2 - schur_f1_factor_ * f1;

        float inv_j44 = 1.0f / j44;
        float j24_inv = j24 * inv_j44;
        float j34_inv = j34 * inv_j44;

        float j22_pp = j22_p - j24_inv * j42;
        float j23_pp = j23 - j24_inv * j43;
        float f2_pp = f2_p + j24_inv * f4;

        float j32_pp = j32 - j34_inv * j42;
        float j33_pp = j33 - j34_inv * j43;
        float f3_pp = f3 + j34_inv * f4;

        float det = j22_pp * j33_pp - j23_pp * j32_pp;
        float inv_det = 1.0f / det;

        float dv_b = (j33_pp * (-f2_pp) - j23_pp * (-f3_pp)) * inv_det;
        float dv_e = (j22_pp * (-f3_pp) - j32_pp * (-f2_pp)) * inv_det;

        float dv_c = (-f4 - j42 * dv_b - j43 * dv_e) * inv_j44;
        float dv_cap = (-f1 - G3 * dv_b) * inv_j11_;

        float max_dv = std::max({std::abs(dv_cap), std::abs(dv_b),
                                 std::abs(dv_e), std::abs(dv_c)});
        float damp = (max_dv > 0.5f) ? 0.5f / max_dv : 1.0f;

        v_cap += damp * dv_cap;
        v_b += damp * dv_b;
        v_e = std::clamp(v_e + damp * dv_e, 0.0f, V_CC + 0.5f);
        v_c = std::clamp(v_c + damp * dv_c, 0.0f, V_CC + 0.5f);

        v_c1_ = v_in - v_cap;
        v_c2_ = v_e;
        v_b_ = v_b;
        v_e_ = v_e;
        v_c_ = v_c;

        return v_c;
    }

private:
    float v_c1_ = 0.0f, v_c2_ = 8.0f;
    float v_b_ = 8.0f, v_e_ = 8.0f, v_c_ = V_COLL;
    float dt_ = 1.0f / 48000.0f;
    float g_c1_ = C1 / dt_;
    float g_c2_ = C2 / dt_;
    float inv_j11_ = 0.0f;
    float schur_j11_factor_ = 0.0f;
    float schur_f1_factor_ = 0.0f;
};

// =============================================================================
// WaveShaperSchurFast: BC接合省略 + 簡略化Jacobian
// Forward Active領域では v_cb < 0 で BC接合は逆バイアス (i_cr ≈ 0, g_cr ≈ 0)
// これにより4x4→3x3への縮約と除算削減が可能
// =============================================================================
class WaveShaperSchurFast {
public:
    void setSampleRate(float sampleRate) {
        dt_ = 1.0f / sampleRate;
        g_c1_ = C1 / dt_;
        g_c2_ = C2 / dt_;

        float j11 = -g_c1_ - G3;
        inv_j11_ = 1.0f / j11;
        schur_j11_factor_ = G3 * G3 * inv_j11_;
        schur_f1_factor_ = G3 * inv_j11_;

        // 定数部分の事前計算
        inv_g5_ = 1.0f / G5;
        j22_const_ = -G2 - G3;
    }

    void reset() {
        v_c1_ = 0.0f;
        v_c2_ = 8.0f;
        v_b_ = 8.0f;
        v_e_ = 8.0f;
        v_c_ = V_COLL;
    }

    __attribute__((noinline))
    float process(float v_in) {
        const float v_c1_prev = v_c1_;
        const float v_c2_prev = v_c2_;

        float v_cap = v_in - v_c1_prev;
        float v_b = v_b_;
        float v_e = v_c2_prev;
        float v_c = v_c_;

        // EB接合のみ評価 (BC省略)
        const float v_eb = v_e - v_b;
        float i_ef, g_ef;
        diode_iv(v_eb, i_ef, g_ef);

        // BC接合の近似処理
        const float v_cb = v_c - v_b;
        float i_cr, g_cr;
        if (v_cb < -0.1f) {
            // 逆バイアス: 省略
            i_cr = -I_S;
            g_cr = 1e-12f;
        } else {
            // 順バイアス方向: 通常計算
            diode_iv(v_cb, i_cr, g_cr);
        }

        // Ebers-Moll電流
        const float i_e = i_ef - ALPHA_R * i_cr;
        const float i_c = ALPHA_F * i_ef - i_cr;
        const float i_b = i_e - i_c;

        // KCL残差
        const float f1 = g_c1_ * (v_in - v_cap - v_c1_prev) - G3 * (v_cap - v_b);
        const float f2 = G2 * (v_in - v_b) + G3 * (v_cap - v_b) + i_b;
        const float f3 = G4 * (V_CC - v_e) - i_e - g_c2_ * (v_e - v_c2_prev);
        const float f4 = G5 * (V_COLL - v_c) + i_c;

        // 簡略化Jacobian (g_cr ≈ 0 の場合)
        const float one_minus_af = 1.0f - ALPHA_F;
        const float j22 = j22_const_ - one_minus_af * g_ef - (1.0f - ALPHA_R) * g_cr;
        const float j23 = one_minus_af * g_ef;
        const float j24 = (1.0f - ALPHA_R) * g_cr;
        const float j32 = g_ef - ALPHA_R * g_cr;
        const float j33 = -G4 - g_ef - g_c2_;
        const float j34 = ALPHA_R * g_cr;
        const float j42 = -ALPHA_F * g_ef + g_cr;
        const float j43 = ALPHA_F * g_ef;
        const float j44 = -G5 - g_cr;

        // Schur縮約
        const float j22_p = j22 - schur_j11_factor_;
        const float f2_p = f2 - schur_f1_factor_ * f1;

        const float inv_j44 = 1.0f / j44;
        const float j24_inv = j24 * inv_j44;
        const float j34_inv = j34 * inv_j44;

        const float j22_pp = j22_p - j24_inv * j42;
        const float j23_pp = j23 - j24_inv * j43;
        const float f2_pp = f2_p + j24_inv * f4;

        const float j32_pp = j32 - j34_inv * j42;
        const float j33_pp = j33 - j34_inv * j43;
        const float f3_pp = f3 + j34_inv * f4;

        // 2x2 Cramer
        const float det = j22_pp * j33_pp - j23_pp * j32_pp;
        const float inv_det = 1.0f / det;

        const float dv_b = (j33_pp * (-f2_pp) - j23_pp * (-f3_pp)) * inv_det;
        const float dv_e = (j22_pp * (-f3_pp) - j32_pp * (-f2_pp)) * inv_det;
        const float dv_c = (-f4 - j42 * dv_b - j43 * dv_e) * inv_j44;
        const float dv_cap = (-f1 - G3 * dv_b) * inv_j11_;

        // ダンピング
        const float max_dv = std::max({std::abs(dv_cap), std::abs(dv_b),
                                       std::abs(dv_e), std::abs(dv_c)});
        const float damp = (max_dv > 0.5f) ? 0.5f / max_dv : 1.0f;

        v_cap += damp * dv_cap;
        v_b += damp * dv_b;
        v_e = std::clamp(v_e + damp * dv_e, 0.0f, V_CC + 0.5f);
        v_c = std::clamp(v_c + damp * dv_c, 0.0f, V_CC + 0.5f);

        v_c1_ = v_in - v_cap;
        v_c2_ = v_e;
        v_b_ = v_b;
        v_e_ = v_e;
        v_c_ = v_c;

        return v_c;
    }

private:
    float v_c1_ = 0.0f, v_c2_ = 8.0f;
    float v_b_ = 8.0f, v_e_ = 8.0f, v_c_ = V_COLL;
    float dt_ = 1.0f / 48000.0f;
    float g_c1_ = C1 / dt_;
    float g_c2_ = C2 / dt_;
    float inv_j11_ = 0.0f;
    float schur_j11_factor_ = 0.0f;
    float schur_f1_factor_ = 0.0f;
    float inv_g5_ = 0.0f;
    float j22_const_ = 0.0f;
};

// =============================================================================
// WaveShaper3Var: v_cap消去による3変数Newton法
// f1が線形なのでv_capをv_bの関数として消去し、3x3システムを直接解く
// =============================================================================
class WaveShaper3Var {
public:
    void setSampleRate(float sampleRate) {
        dt_ = 1.0f / sampleRate;
        g_c1_ = C1 / dt_;
        g_c2_ = C2 / dt_;

        // v_cap消去用定数
        // v_cap = (g_c1*(v_in - v_c1_prev) + G3*v_b) / (g_c1 + G3)
        den_ = g_c1_ + G3;
        inv_den_ = 1.0f / den_;
        k_ = G3 * inv_den_;  // dv_cap/dv_b

        // f2の定数部分: df2/dv_b の線形部分 = -G2 + G3*(k - 1)
        j22_linear_ = -G2 + G3 * (k_ - 1.0f);
    }

    void reset() {
        v_c1_ = 0.0f;
        v_c2_ = 8.0f;
        v_b_ = 8.0f;
        v_e_ = 8.0f;
        v_c_ = V_COLL;
    }

    __attribute__((noinline))
    float process(float v_in) {
        const float v_c1_prev = v_c1_;
        const float v_c2_prev = v_c2_;

        float v_b = v_b_;
        float v_e = v_c2_prev;
        float v_c = v_c_;

        // B = g_c1 * (v_in - v_c1_prev) - 事前計算
        const float B = g_c1_ * (v_in - v_c1_prev);

        // ダイオード評価 (最適化版: exp_crit定数化)
        const float v_eb = v_e - v_b;
        const float v_cb = v_c - v_b;

        float i_ef, g_ef, i_cr, g_cr;
        diode_iv_opt(v_eb, i_ef, g_ef);
        diode_iv_opt(v_cb, i_cr, g_cr);

        // Ebers-Moll電流
        const float i_e = i_ef - ALPHA_R * i_cr;
        const float i_c = ALPHA_F * i_ef - i_cr;
        const float i_b = i_e - i_c;

        // v_cap を v_b の関数として計算 (残差計算用、直接は使わない)
        // v_cap = (B + G3 * v_b) * inv_den_

        // 3変数残差
        // f2 = G2*(v_in - v_b) + G3*(v_cap - v_b) + i_b
        //    = G2*v_in - G2*v_b + G3*v_cap - G3*v_b + i_b
        //    = G2*v_in + G3*v_cap + v_b*(-G2 - G3) + i_b
        //    v_cap = (B + G3*v_b)*inv_den より:
        //    = G2*v_in + G3*(B + G3*v_b)*inv_den - (G2 + G3)*v_b + i_b
        //    = G2*v_in + G3*B*inv_den + v_b*(G3*G3*inv_den - G2 - G3) + i_b
        const float f2 = G2 * v_in + G3 * B * inv_den_ + j22_linear_ * v_b + i_b;
        const float f3 = G4 * (V_CC - v_e) - i_e - g_c2_ * (v_e - v_c2_prev);
        const float f4 = G5 * (V_COLL - v_c) + i_c;

        // 3x3 ヤコビアン
        // j22 = df2/dv_b = j22_linear - (1-αf)*g_ef - (1-αr)*g_cr
        const float j22 = j22_linear_ - (1.0f - ALPHA_F) * g_ef - (1.0f - ALPHA_R) * g_cr;
        const float j23 = (1.0f - ALPHA_F) * g_ef;
        const float j24 = (1.0f - ALPHA_R) * g_cr;
        const float j32 = g_ef - ALPHA_R * g_cr;
        const float j33 = -G4 - g_ef - g_c2_;
        const float j34 = ALPHA_R * g_cr;
        const float j42 = -ALPHA_F * g_ef + g_cr;
        const float j43 = ALPHA_F * g_ef;
        const float j44 = -G5 - g_cr;

        // 3x3 Gauss消去 (行2,3,4 → 実際には j22,j23,j24 が行1)
        // 行1からピボット
        const float inv_j22 = 1.0f / j22;
        const float m32 = j32 * inv_j22;
        const float m42 = j42 * inv_j22;

        // 行2更新 (j33, j34, f3)
        const float j33_p = j33 - m32 * j23;
        const float j34_p = j34 - m32 * j24;
        const float f3_p = f3 + m32 * f2;  // -f3 - m32*(-f2) = -f3 + m32*f2

        // 行3更新 (j43, j44, f4)
        const float j43_p = j43 - m42 * j23;
        const float j44_p = j44 - m42 * j24;
        const float f4_p = f4 + m42 * f2;

        // 2x2 Cramer
        const float det = j33_p * j44_p - j34_p * j43_p;
        const float inv_det = 1.0f / det;

        const float dv_e = (j44_p * (-f3_p) - j34_p * (-f4_p)) * inv_det;
        const float dv_c = (j33_p * (-f4_p) - j43_p * (-f3_p)) * inv_det;

        // 後退代入
        const float dv_b = (-f2 - j23 * dv_e - j24 * dv_c) * inv_j22;

        // v_capはv_bから直接計算するため、dv_capは不要
        // dv_cap = k_ * dv_b

        // ダンピング
        float max_dv = std::abs(dv_b);
        if (std::abs(dv_e) > max_dv) max_dv = std::abs(dv_e);
        if (std::abs(dv_c) > max_dv) max_dv = std::abs(dv_c);
        const float damp = (max_dv > 0.5f) ? 0.5f / max_dv : 1.0f;

        v_b += damp * dv_b;
        v_e = std::clamp(v_e + damp * dv_e, 0.0f, V_CC + 0.5f);
        v_c = std::clamp(v_c + damp * dv_c, 0.0f, V_CC + 0.5f);

        // v_cap更新（新しいv_bから再計算）
        const float v_cap_new = (B + G3 * v_b) * inv_den_;

        // 状態更新
        v_c1_ = v_in - v_cap_new;
        v_c2_ = v_e;
        v_b_ = v_b;
        v_e_ = v_e;
        v_c_ = v_c;

        return v_c;
    }

private:
    float v_c1_ = 0.0f, v_c2_ = 8.0f;
    float v_b_ = 8.0f, v_e_ = 8.0f, v_c_ = V_COLL;
    float dt_ = 1.0f / 48000.0f;
    float g_c1_ = C1 / dt_;
    float g_c2_ = C2 / dt_;
    float den_ = 0.0f;
    float inv_den_ = 0.0f;
    float k_ = 0.0f;
    float j22_linear_ = 0.0f;
};

// =============================================================================
// WaveShaper3Var2Iter: 3変数Newton - 2回反復版
// =============================================================================
class WaveShaper3Var2Iter {
public:
    void setSampleRate(float sampleRate) {
        dt_ = 1.0f / sampleRate;
        g_c1_ = C1 / dt_;
        g_c2_ = C2 / dt_;
        // Schur補完用定数 (WaveShaperTwoIterと同じ方式)
        float j11 = -g_c1_ - G3;
        inv_j11_ = 1.0f / j11;
        schur_j11_factor_ = G3 * G3 * inv_j11_;
        schur_f1_factor_ = G3 * inv_j11_;
    }

    void reset() {
        v_c1_ = 0.0f;
        v_c2_ = 8.0f;
        v_b_ = 8.0f;
        v_e_ = 8.0f;
        v_c_ = V_COLL;
    }

    __attribute__((noinline))
    float process(float v_in) {
        const float v_c1_prev = v_c1_;
        const float v_c2_prev = v_c2_;

        float v_cap = v_in - v_c1_prev;
        float v_b = v_b_;
        float v_e = v_c2_prev;
        float v_c = v_c_;

        // 2回反復 (WaveShaperTwoIterと同じ方式)
        for (int iter = 0; iter < 2; ++iter) {
            const float v_eb = v_e - v_b;
            const float v_cb = v_c - v_b;

            float i_ef, g_ef, i_cr, g_cr;
            diode_iv_opt(v_eb, i_ef, g_ef);
            diode_iv_opt(v_cb, i_cr, g_cr);

            const float i_e = i_ef - ALPHA_R * i_cr;
            const float i_c = ALPHA_F * i_ef - i_cr;
            const float i_b = i_e - i_c;

            // 4変数残差
            const float f1 = g_c1_ * (v_in - v_cap - v_c1_prev) - G3 * (v_cap - v_b);
            const float f2 = G2 * (v_in - v_b) + G3 * (v_cap - v_b) + i_b;
            const float f3 = G4 * (V_CC - v_e) - i_e - g_c2_ * (v_e - v_c2_prev);
            const float f4 = G5 * (V_COLL - v_c) + i_c;

            // 3x3ヤコビアン (v_capは行1でSchur補完済み)
            const float j22 = -G2 - G3 - (1.0f - ALPHA_F) * g_ef - (1.0f - ALPHA_R) * g_cr;
            const float j23 = (1.0f - ALPHA_F) * g_ef;
            const float j24 = (1.0f - ALPHA_R) * g_cr;
            const float j32 = g_ef - ALPHA_R * g_cr;
            const float j33 = -G4 - g_ef - g_c2_;
            const float j34 = ALPHA_R * g_cr;
            const float j42 = -ALPHA_F * g_ef + g_cr;
            const float j43 = ALPHA_F * g_ef;
            const float j44 = -G5 - g_cr;

            // v_capのSchur補完
            const float j22_p = j22 - schur_j11_factor_;
            const float f2_p = f2 - schur_f1_factor_ * f1;

            // j44からピボット (WaveShaperTwoIterと同じ)
            const float inv_j44 = 1.0f / j44;
            const float j24_inv = j24 * inv_j44;
            const float j34_inv = j34 * inv_j44;

            const float j22_pp = j22_p - j24_inv * j42;
            const float j23_pp = j23 - j24_inv * j43;
            const float f2_pp = f2_p + j24_inv * f4;

            const float j32_pp = j32 - j34_inv * j42;
            const float j33_pp = j33 - j34_inv * j43;
            const float f3_pp = f3 + j34_inv * f4;

            // 2x2 Cramer
            const float det = j22_pp * j33_pp - j23_pp * j32_pp;
            const float inv_det = 1.0f / det;

            const float dv_b = (j33_pp * (-f2_pp) - j23_pp * (-f3_pp)) * inv_det;
            const float dv_e = (j22_pp * (-f3_pp) - j32_pp * (-f2_pp)) * inv_det;
            const float dv_c = (-f4 - j42 * dv_b - j43 * dv_e) * inv_j44;
            const float dv_cap = (-f1 - G3 * dv_b) * inv_j11_;

            float max_dv = std::abs(dv_b);
            if (std::abs(dv_e) > max_dv) max_dv = std::abs(dv_e);
            if (std::abs(dv_c) > max_dv) max_dv = std::abs(dv_c);
            if (std::abs(dv_cap) > max_dv) max_dv = std::abs(dv_cap);
            const float damp = (max_dv > 0.5f) ? 0.5f / max_dv : 1.0f;

            v_cap += damp * dv_cap;
            v_b += damp * dv_b;
            v_e = std::clamp(v_e + damp * dv_e, 0.0f, V_CC + 0.5f);
            v_c = std::clamp(v_c + damp * dv_c, 0.0f, V_CC + 0.5f);
        }

        v_c1_ = v_in - v_cap;
        v_c2_ = v_e;
        v_b_ = v_b;
        v_e_ = v_e;
        v_c_ = v_c;

        return v_c;
    }

private:
    float v_c1_ = 0.0f, v_c2_ = 8.0f;
    float v_b_ = 8.0f, v_e_ = 8.0f, v_c_ = V_COLL;
    float dt_ = 1.0f / 48000.0f;
    float g_c1_ = C1 / dt_;
    float g_c2_ = C2 / dt_;
    float inv_j11_ = 0.0f;
    float schur_j11_factor_ = 0.0f;
    float schur_f1_factor_ = 0.0f;
    float inv_den_ = 0.0f;
    float k_ = 0.0f;
    float j22_linear_ = 0.0f;
};

// =============================================================================
// WaveShaperSchur2Iter: Schur補行列 - 2回反復版
// =============================================================================
class WaveShaperSchur2Iter {
public:
    void setSampleRate(float sampleRate) {
        dt_ = 1.0f / sampleRate;
        g_c1_ = C1 / dt_;
        g_c2_ = C2 / dt_;

        float j11 = -g_c1_ - G3;
        inv_j11_ = 1.0f / j11;
        schur_j11_factor_ = G3 * G3 * inv_j11_;
        schur_f1_factor_ = G3 * inv_j11_;
    }

    void reset() {
        v_c1_ = 0.0f;
        v_c2_ = 8.0f;
        v_b_ = 8.0f;
        v_e_ = 8.0f;
        v_c_ = V_COLL;
    }

    __attribute__((noinline))
    float process(float v_in) {
        const float v_c1_prev = v_c1_;
        const float v_c2_prev = v_c2_;

        float v_cap = v_in - v_c1_prev;
        float v_b = v_b_;
        float v_e = v_c2_prev;
        float v_c = v_c_;

        // 2回反復
        for (int iter = 0; iter < 2; ++iter) {
            float v_eb = v_e - v_b;
            float v_cb = v_c - v_b;

            float i_ef, g_ef, i_cr, g_cr;
            diode_iv_opt(v_eb, i_ef, g_ef);
            diode_iv_opt(v_cb, i_cr, g_cr);

            float i_e = i_ef - ALPHA_R * i_cr;
            float i_c = ALPHA_F * i_ef - i_cr;
            float i_b = i_e - i_c;

            float f1 = g_c1_ * (v_in - v_cap - v_c1_prev) - G3 * (v_cap - v_b);
            float f2 = G2 * (v_in - v_b) + G3 * (v_cap - v_b) + i_b;
            float f3 = G4 * (V_CC - v_e) - i_e - g_c2_ * (v_e - v_c2_prev);
            float f4 = G5 * (V_COLL - v_c) + i_c;

            float j22 = -G2 - G3 - (1.0f - ALPHA_F) * g_ef - (1.0f - ALPHA_R) * g_cr;
            float j23 = (1.0f - ALPHA_F) * g_ef;
            float j24 = (1.0f - ALPHA_R) * g_cr;
            float j32 = g_ef - ALPHA_R * g_cr;
            float j33 = -G4 - g_ef - g_c2_;
            float j34 = ALPHA_R * g_cr;
            float j42 = -ALPHA_F * g_ef + g_cr;
            float j43 = ALPHA_F * g_ef;
            float j44 = -G5 - g_cr;

            float j22_p = j22 - schur_j11_factor_;
            float f2_p = f2 - schur_f1_factor_ * f1;

            float inv_j44 = 1.0f / j44;
            float j24_inv_j44 = j24 * inv_j44;
            float j22_pp = j22_p - j24_inv_j44 * j42;
            float j23_pp = j23 - j24_inv_j44 * j43;
            float f2_pp = f2_p + j24_inv_j44 * f4;

            float j34_inv_j44 = j34 * inv_j44;
            float j32_pp = j32 - j34_inv_j44 * j42;
            float j33_pp = j33 - j34_inv_j44 * j43;
            float f3_pp = f3 + j34_inv_j44 * f4;

            float det = j22_pp * j33_pp - j23_pp * j32_pp;
            if (std::abs(det) < 1e-15f) continue;

            float inv_det = 1.0f / det;
            float neg_f2 = -f2_pp;
            float neg_f3 = -f3_pp;
            float dv_b = (j33_pp * neg_f2 - j23_pp * neg_f3) * inv_det;
            float dv_e = (j22_pp * neg_f3 - j32_pp * neg_f2) * inv_det;
            float dv_c = (-f4 - j42 * dv_b - j43 * dv_e) * inv_j44;
            float dv_cap = (-f1 - G3 * dv_b) * inv_j11_;

            float max_dv = std::max({std::abs(dv_cap), std::abs(dv_b),
                                     std::abs(dv_e), std::abs(dv_c)});
            float damp = (max_dv > 0.5f) ? 0.5f / max_dv : 1.0f;

            v_cap += damp * dv_cap;
            v_b += damp * dv_b;
            v_e = std::clamp(v_e + damp * dv_e, 0.0f, V_CC + 0.5f);
            v_c = std::clamp(v_c + damp * dv_c, 0.0f, V_CC + 0.5f);
        }

        v_c1_ = v_in - v_cap;
        v_c2_ = v_e;
        v_b_ = v_b;
        v_e_ = v_e;
        v_c_ = v_c;

        return v_c;
    }

private:
    float v_c1_ = 0.0f, v_c2_ = 8.0f;
    float v_b_ = 8.0f, v_e_ = 8.0f, v_c_ = V_COLL;
    float dt_ = 1.0f / 48000.0f;
    float g_c1_ = C1 / dt_;
    float g_c2_ = C2 / dt_;
    float inv_j11_ = 0.0f;
    float schur_j11_factor_ = 0.0f;
    float schur_f1_factor_ = 0.0f;
};

// =============================================================================
// WaveShaper3VarOpt: 3変数Newton (WaveShaper3Varと同一実装)
// diode_ivを使用、1回Newton反復
// =============================================================================
class WaveShaper3VarOpt {
public:
    void setSampleRate(float sampleRate) {
        dt_ = 1.0f / sampleRate;
        g_c1_ = C1 / dt_;
        g_c2_ = C2 / dt_;

        // v_cap消去用定数
        den_ = g_c1_ + G3;
        inv_den_ = 1.0f / den_;

        // j22_linear = -G2 - G3 + G3²/den
        j22_linear_ = -G2 - G3 + G3 * G3 * inv_den_;
    }

    void reset() {
        v_c1_ = 0.0f;
        v_c2_ = 8.0f;
        v_b_ = 8.0f;
        v_e_ = 8.0f;
        v_c_ = V_COLL;
    }

    __attribute__((noinline))
    float process(float v_in) {
        const float v_c1_prev = v_c1_;
        const float v_c2_prev = v_c2_;

        float v_b = v_b_;
        float v_e = v_c2_prev;
        float v_c = v_c_;

        const float B = g_c1_ * (v_in - v_c1_prev);

        // diode評価
        float i_ef, g_ef, i_cr, g_cr;
        diode_iv(v_e - v_b, i_ef, g_ef);
        diode_iv(v_c - v_b, i_cr, g_cr);

        // Ebers-Moll電流
        const float i_e = i_ef - ALPHA_R * i_cr;
        const float i_c = ALPHA_F * i_ef - i_cr;
        const float i_b = i_e - i_c;

        // 残差
        const float f2 = G2 * (v_in - v_b) + G3 * B * inv_den_ + (j22_linear_ + G2) * v_b + i_b;
        const float f3 = G4 * (V_CC - v_e) - i_e - g_c2_ * (v_e - v_c2_prev);
        const float f4 = G5 * (V_COLL - v_c) + i_c;

        // ヤコビアン
        const float omaf_g_ef = (1.0f - ALPHA_F) * g_ef;
        const float omar_g_cr = (1.0f - ALPHA_R) * g_cr;
        const float af_g_ef = ALPHA_F * g_ef;
        const float ar_g_cr = ALPHA_R * g_cr;

        const float j22 = j22_linear_ - omaf_g_ef - omar_g_cr;
        const float j23 = omaf_g_ef;
        const float j24 = omar_g_cr;
        const float j32 = g_ef - ar_g_cr;
        const float j33 = -G4 - g_ef - g_c2_;
        const float j34 = ar_g_cr;
        const float j42 = -af_g_ef + g_cr;
        const float j43 = af_g_ef;
        const float j44 = -G5 - g_cr;

        // 3x3 Gauss消去
        const float inv_j22 = 1.0f / j22;
        const float m32 = j32 * inv_j22;
        const float m42 = j42 * inv_j22;

        const float j33_p = j33 - m32 * j23;
        const float j34_p = j34 - m32 * j24;
        const float f3_p = f3 + m32 * f2;

        const float j43_p = j43 - m42 * j23;
        const float j44_p = j44 - m42 * j24;
        const float f4_p = f4 + m42 * f2;

        // 2x2 Cramer
        const float det = j33_p * j44_p - j34_p * j43_p;
        const float inv_det = 1.0f / det;

        const float dv_e = (j44_p * (-f3_p) - j34_p * (-f4_p)) * inv_det;
        const float dv_c = (j33_p * (-f4_p) - j43_p * (-f3_p)) * inv_det;
        const float dv_b = (-f2 - j23 * dv_e - j24 * dv_c) * inv_j22;

        // ダンピング
        float max_dv = std::abs(dv_b);
        if (std::abs(dv_e) > max_dv) max_dv = std::abs(dv_e);
        if (std::abs(dv_c) > max_dv) max_dv = std::abs(dv_c);
        const float damp = (max_dv > 0.5f) ? 0.5f / max_dv : 1.0f;

        v_b += damp * dv_b;
        v_e = std::clamp(v_e + damp * dv_e, 0.0f, V_CC + 0.5f);
        v_c = std::clamp(v_c + damp * dv_c, 0.0f, V_CC + 0.5f);

        // v_cap更新
        const float v_cap_new = (B + G3 * v_b) * inv_den_;

        // 状態更新
        v_c1_ = v_in - v_cap_new;
        v_c2_ = v_e;
        v_b_ = v_b;
        v_e_ = v_e;
        v_c_ = v_c;

        return v_c;
    }

private:
    float v_c1_ = 0.0f, v_c2_ = 8.0f;
    float v_b_ = 8.0f, v_e_ = 8.0f, v_c_ = V_COLL;
    float dt_ = 1.0f / 48000.0f;
    float g_c1_ = C1 / dt_;
    float g_c2_ = C2 / dt_;
    float den_ = 0.0f;
    float inv_den_ = 0.0f;
    float j22_linear_ = 0.0f;
};

// =============================================================================
// WaveShaperPredict: 状態予測 + 1回Newton
// 前回の変化率を使って次の状態を予測し、Newton収束を高速化
// =============================================================================
class WaveShaperPredict {
public:
    void setSampleRate(float sampleRate) {
        dt_ = 1.0f / sampleRate;
        g_c1_ = C1 / dt_;
        g_c2_ = C2 / dt_;

        float j11 = -g_c1_ - G3;
        inv_j11_ = 1.0f / j11;
        schur_j11_factor_ = G3 * G3 * inv_j11_;
        schur_f1_factor_ = G3 * inv_j11_;
    }

    void reset() {
        v_c1_ = 0.0f;
        v_c2_ = 8.0f;
        v_b_ = 8.0f;
        v_e_ = 8.0f;
        v_c_ = V_COLL;
        dv_b_prev_ = 0.0f;
        dv_e_prev_ = 0.0f;
        dv_c_prev_ = 0.0f;
    }

    __attribute__((noinline))
    float process(float v_in) {
        const float v_c1_prev = v_c1_;
        const float v_c2_prev = v_c2_;

        // 予測: 前回の変化率を使用
        float v_cap = v_in - v_c1_prev;
        float v_b = v_b_ + 0.5f * dv_b_prev_;
        float v_e = v_c2_prev + 0.5f * dv_e_prev_;
        float v_c = v_c_ + 0.5f * dv_c_prev_;

        // クランプ
        v_e = std::clamp(v_e, 0.0f, V_CC + 0.5f);
        v_c = std::clamp(v_c, 0.0f, V_CC + 0.5f);

        // ダイオード評価
        float v_eb = v_e - v_b;
        float v_cb = v_c - v_b;

        float i_ef, g_ef, i_cr, g_cr;
        diode_iv(v_eb, i_ef, g_ef);
        diode_iv(v_cb, i_cr, g_cr);

        // Ebers-Moll電流
        float i_e = i_ef - ALPHA_R * i_cr;
        float i_c = ALPHA_F * i_ef - i_cr;
        float i_b = i_e - i_c;

        // KCL残差
        float f1 = g_c1_ * (v_in - v_cap - v_c1_prev) - G3 * (v_cap - v_b);
        float f2 = G2 * (v_in - v_b) + G3 * (v_cap - v_b) + i_b;
        float f3 = G4 * (V_CC - v_e) - i_e - g_c2_ * (v_e - v_c2_prev);
        float f4 = G5 * (V_COLL - v_c) + i_c;

        // Jacobian
        float j22 = -G2 - G3 - (1.0f - ALPHA_F) * g_ef - (1.0f - ALPHA_R) * g_cr;
        float j23 = (1.0f - ALPHA_F) * g_ef;
        float j24 = (1.0f - ALPHA_R) * g_cr;
        float j32 = g_ef - ALPHA_R * g_cr;
        float j33 = -G4 - g_ef - g_c2_;
        float j34 = ALPHA_R * g_cr;
        float j42 = -ALPHA_F * g_ef + g_cr;
        float j43 = ALPHA_F * g_ef;
        float j44 = -G5 - g_cr;

        // Schur縮約
        float j22_p = j22 - schur_j11_factor_;
        float f2_p = f2 - schur_f1_factor_ * f1;

        float inv_j44 = 1.0f / j44;
        float j24_inv = j24 * inv_j44;
        float j34_inv = j34 * inv_j44;

        float j22_pp = j22_p - j24_inv * j42;
        float j23_pp = j23 - j24_inv * j43;
        float f2_pp = f2_p + j24_inv * f4;

        float j32_pp = j32 - j34_inv * j42;
        float j33_pp = j33 - j34_inv * j43;
        float f3_pp = f3 + j34_inv * f4;

        // 2x2 Cramer
        float det = j22_pp * j33_pp - j23_pp * j32_pp;
        float inv_det = 1.0f / det;

        float dv_b = (j33_pp * (-f2_pp) - j23_pp * (-f3_pp)) * inv_det;
        float dv_e = (j22_pp * (-f3_pp) - j32_pp * (-f2_pp)) * inv_det;
        float dv_c = (-f4 - j42 * dv_b - j43 * dv_e) * inv_j44;
        float dv_cap = (-f1 - G3 * dv_b) * inv_j11_;

        // ダンピング
        float max_dv = std::max({std::abs(dv_cap), std::abs(dv_b),
                                 std::abs(dv_e), std::abs(dv_c)});
        float damp = (max_dv > 0.5f) ? 0.5f / max_dv : 1.0f;

        v_cap += damp * dv_cap;
        v_b += damp * dv_b;
        v_e = std::clamp(v_e + damp * dv_e, 0.0f, V_CC + 0.5f);
        v_c = std::clamp(v_c + damp * dv_c, 0.0f, V_CC + 0.5f);

        // 変化率を保存
        dv_b_prev_ = v_b - v_b_;
        dv_e_prev_ = v_e - v_e_;
        dv_c_prev_ = v_c - v_c_;

        v_c1_ = v_in - v_cap;
        v_c2_ = v_e;
        v_b_ = v_b;
        v_e_ = v_e;
        v_c_ = v_c;

        return v_c;
    }

private:
    float v_c1_ = 0.0f, v_c2_ = 8.0f;
    float v_b_ = 8.0f, v_e_ = 8.0f, v_c_ = V_COLL;
    float dv_b_prev_ = 0.0f, dv_e_prev_ = 0.0f, dv_c_prev_ = 0.0f;
    float dt_ = 1.0f / 48000.0f;
    float g_c1_ = C1 / dt_;
    float g_c2_ = C2 / dt_;
    float inv_j11_ = 0.0f;
    float schur_j11_factor_ = 0.0f;
    float schur_f1_factor_ = 0.0f;
};

// =============================================================================
// WaveShaperGeometric: 状態空間幾何写像
// 双曲幾何による指数非線形性の線形化 + トーラス位相写像
// Newton法不要、12 FLOPs/サンプル
// =============================================================================
class WaveShaperGeometric {
public:
    void setSampleRate(float /* sampleRate */) {
        // サンプルレート依存パラメータなし（48kHz想定で固定）
    }

    void reset() {
        theta_ = 1.8f;  // Vbe=0.65V → θ (実用スケーリング)
        phi_   = 0.0f;
    }

    __attribute__((noinline))
    float process(float v_in) {
        // STEP 1: 入力→双曲角変換 (3 FLOPs)
        const float env = std::abs(v_in) * 0.3f + 7.8f;  // エミッタ包絡線モデル
        const float v_be = v_in - env + 0.65f;           // Vbe推定
        const float d_theta = v_be * 0.035f;             // 双曲角変化量

        // STEP 2: トーラス状態更新 (4 FLOPs)
        constexpr float COS_PHI = 0.9998f;  // C1効果: τ=0.91ms → 48kHz減衰
        constexpr float SIN_PHI = 0.02f;    // 周波数依存位相シフト
        const float theta_new = COS_PHI * theta_ - SIN_PHI * phi_ + d_theta;
        phi_ = SIN_PHI * theta_ + COS_PHI * phi_;
        theta_ = theta_new;

        // STEP 3: 出力生成 (5 FLOPs) - sinh(x)の[3/2] Pade近似
        const float t2 = theta_ * theta_;
        const float sinh_approx = theta_ * (1.0f + t2 * 0.1666667f)
                                / (1.0f - t2 * 0.05f);

        // 物理制約付き出力
        float v_out = 5.33f - 0.42f * sinh_approx * (1.0f + 0.15f * COS_PHI);
        return std::clamp(v_out, 0.25f, 11.85f);
    }

private:
    float theta_ = 1.8f;
    float phi_   = 0.0f;
};

// =============================================================================
// pad_exp: Padé近似 + レンジリダクション
// 精度: 相対誤差 < 0.1% for |x| < 40
// =============================================================================
inline float pad_exp(float x) {
    // レンジリダクション: e^x = 2^k * e^r where r = x - k*ln(2)
    constexpr float LOG2E = 1.4426950408889634f;
    constexpr float LN2 = 0.6931471805599453f;

    float k = std::floor(x * LOG2E + 0.5f);
    float r = x - k * LN2;  // |r| < ln(2)/2 ≈ 0.347

    // Padé [4,4] 近似 for e^r
    // e^r ≈ (1 + r/2 + r²/9 + r³/72 + r⁴/1008) / (1 - r/2 + r²/9 - r³/72 + r⁴/1008)
    // 簡略化 [2,2]: e^r ≈ (1 + r/2 + r²/12) / (1 - r/2 + r²/12)
    float r2 = r * r;
    float num = 1.0f + r * 0.5f + r2 * 0.0833333f;
    float den = 1.0f - r * 0.5f + r2 * 0.0833333f;
    float exp_r = num / den;

    // 2^k をビット操作で計算
    union { float f; int32_t i; } u;
    int32_t ki = static_cast<int32_t>(k);
    // 127はfloatの指数バイアス、23はマンティッサビット数
    u.i = (ki + 127) << 23;

    return exp_r * u.f;
}

// =============================================================================
// WaveShaperOptimized: pad_exp + 解析的3x3逆行列
//
// 最適化:
// 1. pad_exp で高精度かつ高速なexp計算
// 2. v_cap を代数的に消去して 4x4 → 3x3
// 3. Cramer公式で解析的に逆行列を計算
// =============================================================================
class WaveShaperOptimized {
public:
    void setSampleRate(float sampleRate) {
        dt_ = 1.0f / sampleRate;
        g_c1_ = C1 / dt_;
        g_c2_ = C2 / dt_;

        // 事前計算
        den_ = g_c1_ + G3;
        inv_den_ = 1.0f / den_;
        j22_linear_ = -G2 - G3 + G3 * G3 * inv_den_;
    }

    void reset() {
        v_c1_ = 0.0f;
        v_c2_ = 8.0f;
        v_b_ = 8.0f;
        v_e_ = 8.0f;
        v_c_ = V_COLL;
    }

    __attribute__((noinline))
    float process(float v_in) {
        const float v_c1_prev = v_c1_;
        const float v_c2_prev = v_c2_;

        float v_b = v_b_;
        float v_e = v_c2_prev;
        float v_c = v_c_;

        const float B = g_c1_ * (v_in - v_c1_prev);

        // ダイオード評価 (pad_exp使用)
        const float v_eb = v_e - v_b;
        const float v_cb = v_c - v_b;

        float i_ef, g_ef, i_cr, g_cr;
        diode_iv_pad(v_eb, i_ef, g_ef);
        diode_iv_pad(v_cb, i_cr, g_cr);

        // Ebers-Moll電流
        const float i_e = i_ef - ALPHA_R * i_cr;
        const float i_c = ALPHA_F * i_ef - i_cr;
        const float i_b = i_e - i_c;

        // 残差 (v_cap消去済み)
        const float f2 = G2 * (v_in - v_b) + G3 * B * inv_den_ + (j22_linear_ + G2) * v_b + i_b;
        const float f3 = G4 * (V_CC - v_e) - i_e - g_c2_ * (v_e - v_c2_prev);
        const float f4 = G5 * (V_COLL - v_c) + i_c;

        // ヤコビアン要素
        const float omaf_g_ef = (1.0f - ALPHA_F) * g_ef;
        const float omar_g_cr = (1.0f - ALPHA_R) * g_cr;
        const float af_g_ef = ALPHA_F * g_ef;
        const float ar_g_cr = ALPHA_R * g_cr;

        const float j22 = j22_linear_ - omaf_g_ef - omar_g_cr;
        const float j23 = omaf_g_ef;
        const float j24 = omar_g_cr;
        const float j32 = g_ef - ar_g_cr;
        const float j33 = -G4 - g_ef - g_c2_;
        const float j34 = ar_g_cr;
        const float j42 = -af_g_ef + g_cr;
        const float j43 = af_g_ef;
        const float j44 = -G5 - g_cr;

        // 3x3 Cramer公式による解析的逆行列
        // det = j22*(j33*j44 - j34*j43) - j23*(j32*j44 - j34*j42) + j24*(j32*j43 - j33*j42)
        const float m1 = j33 * j44 - j34 * j43;
        const float m2 = j32 * j44 - j34 * j42;
        const float m3 = j32 * j43 - j33 * j42;
        const float det = j22 * m1 - j23 * m2 + j24 * m3;

        if (std::abs(det) < 1e-15f) {
            v_c1_ = v_in - (B + G3 * v_b) * inv_den_;
            v_c2_ = v_e;
            return v_c;
        }

        const float inv_det = 1.0f / det;

        // 余因子展開
        // dv_b = [(-f2)*(j33*j44-j34*j43) - j23*(-f3*j44+f4*j43) + j24*(-f3*j34+f4*j33)] / det
        const float neg_f2 = -f2;
        const float neg_f3 = -f3;
        const float neg_f4 = -f4;

        const float dv_b = (neg_f2 * m1
                         + j23 * (neg_f3 * j44 - neg_f4 * j43)
                         + j24 * (neg_f4 * j33 - neg_f3 * j34)) * inv_det;
        const float dv_e = (j22 * (neg_f3 * j44 - neg_f4 * j43)
                         - neg_f2 * m2
                         + j24 * (neg_f4 * j32 - neg_f3 * j42)) * inv_det;
        const float dv_c = (j22 * (neg_f4 * j33 - neg_f3 * j34)
                         - j23 * (neg_f4 * j32 - neg_f3 * j42)
                         + neg_f2 * m3) * inv_det;

        // ダンピング
        const float max_dv = std::max({std::abs(dv_b), std::abs(dv_e), std::abs(dv_c)});
        const float damp = (max_dv > 0.5f) ? 0.5f / max_dv : 1.0f;

        v_b += damp * dv_b;
        v_e = std::clamp(v_e + damp * dv_e, 0.0f, V_CC + 0.5f);
        v_c = std::clamp(v_c + damp * dv_c, 0.0f, V_CC + 0.5f);

        // v_cap更新
        const float v_cap_new = (B + G3 * v_b) * inv_den_;

        v_c1_ = v_in - v_cap_new;
        v_c2_ = v_e;
        v_b_ = v_b;
        v_e_ = v_e;
        v_c_ = v_c;

        return v_c;
    }

private:
    // pad_expを使用したダイオード評価
    inline void diode_iv_pad(float v, float& i, float& g) {
        if (v > V_CRIT) {
            float exp_crit = pad_exp(V_CRIT * V_T_INV);
            g = I_S * V_T_INV * exp_crit;
            i = I_S * (exp_crit - 1.0f) + g * (v - V_CRIT);
        } else if (v < -10.0f * V_T) {
            i = -I_S;
            g = 1e-12f;
        } else {
            float exp_v = pad_exp(v * V_T_INV);
            i = I_S * (exp_v - 1.0f);
            g = I_S * V_T_INV * exp_v + 1e-12f;
        }
    }

    float v_c1_ = 0.0f, v_c2_ = 8.0f;
    float v_b_ = 8.0f, v_e_ = 8.0f, v_c_ = V_COLL;
    float dt_ = 1.0f / 48000.0f;
    float g_c1_ = C1 / dt_;
    float g_c2_ = C2 / dt_;
    float den_ = 0.0f;
    float inv_den_ = 0.0f;
    float j22_linear_ = 0.0f;
};

// =============================================================================
// WaveShaperMinimal: 最小限の計算で近似
// EB接合のみ、コレクタ電圧を直接計算
// =============================================================================
class WaveShaperMinimal {
public:
    void setSampleRate(float sampleRate) {
        dt_ = 1.0f / sampleRate;
        g_c1_ = C1 / dt_;
        g_c2_ = C2 / dt_;
    }

    void reset() {
        v_c1_ = 0.0f;
        v_c2_ = 8.0f;
    }

    __attribute__((noinline))
    float process(float v_in) {
        // 入力カップリング後の電圧
        const float v_cap = v_in - v_c1_;

        // ベース電圧: R2/R3分圧
        const float v_b = (v_cap * G3 + v_in * G2) / (G2 + G3);

        // エミッタ電圧
        const float v_e = v_c2_;

        // EB接合電圧
        const float v_eb = v_e - v_b;

        // ダイオード電流 (簡略化)
        float i_ef, g_ef;
        if (v_eb > V_CRIT) {
            const float exp_crit = fast_exp(V_CRIT * V_T_INV);
            g_ef = I_S * V_T_INV * exp_crit;
            i_ef = I_S * (exp_crit - 1.0f) + g_ef * (v_eb - V_CRIT);
        } else if (v_eb < -10.0f * V_T) {
            i_ef = -I_S;
            g_ef = 1e-12f;
        } else {
            const float exp_v = fast_exp(v_eb * V_T_INV);
            i_ef = I_S * (exp_v - 1.0f);
            g_ef = I_S * V_T_INV * exp_v + 1e-12f;
        }

        // コレクタ電流 (Forward Active近似)
        const float i_c = ALPHA_F * i_ef;

        // コレクタ電圧
        float v_c = V_COLL - R5 * i_c;
        v_c = std::clamp(v_c, 0.0f, V_COLL);

        // エミッタ電流
        const float i_e = i_ef;

        // 状態更新 (Backward Euler)
        // C1: i = (v_in - v_cap - v_c1_prev) * g_c1
        const float i_c1 = (v_in - v_cap - v_c1_) * g_c1_ - G3 * (v_cap - v_b);
        v_c1_ += i_c1 / g_c1_;

        // C2: エミッタノードKCL
        const float i_charge = (V_CC - v_e) * G4;
        const float dv_e = (i_charge - i_e) / g_c2_;
        v_c2_ = std::clamp(v_c2_ + dv_e, 0.0f, V_CC);

        return v_c;
    }

private:
    float v_c1_ = 0.0f;
    float v_c2_ = 8.0f;
    float dt_ = 1.0f / 48000.0f;
    float g_c1_ = C1 / dt_;
    float g_c2_ = C2 / dt_;
};

// =============================================================================
// Wright Omega関数 (DAFx2019 D'Angelo et al.)
// Lambert W関数の変形: ω(x) = W₀(eˣ)
// ダイオード方程式を明示解に変換するために使用
// =============================================================================

// 高速log2近似 (IEEE754ビット操作)
inline float log2f_approx(float x) {
    union { float f; int32_t i; } u;
    u.f = x;
    float y = static_cast<float>(u.i);
    y *= 1.1920928955078125e-7f;  // 1/(1<<23)
    return y - 126.94269504f;
}

// 高速ln近似
inline float logf_approx(float x) {
    return 0.6931471805599453f * log2f_approx(x);  // ln(2) * log2(x)
}

// 高速pow2近似
inline float pow2f_approx(float x) {
    float f = x + 126.94269504f;
    union { float f; int32_t i; } u;
    u.i = static_cast<int32_t>(f * 8388608.0f);  // 1<<23
    return u.f;
}

// 高速exp近似 (Wright Omega用)
inline float expf_approx(float x) {
    return pow2f_approx(1.4426950408889634f * x);  // pow2(x/ln(2))
}

// Wright Omega関数 omega3 (DAFx2019)
// 区分多項式 + 漸近展開
inline float omega3(float x) {
    if (x < -3.341459552768620f) {
        // 小さいx: ω(x) ≈ e^x
        float ex = expf_approx(x);
        return ex;
    } else if (x < 8.0f) {
        // 中間領域: 4次多項式近似
        // 係数: [-1.314e-3, 0.04776, 0.3632, 0.6314]
        float y = x + 1.0f;  // シフト
        return 0.6314f + y * (0.3632f + y * (0.04776f + y * (-0.001314f)));
    } else {
        // 大きいx: ω(x) ≈ x - ln(x)
        return x - logf_approx(x);
    }
}

// Wright Omega関数 omega4 (Newton-Raphson補正付き)
inline float omega4(float x) {
    float w = omega3(x);
    // 1回のNewton-Raphson: w = w - (w - exp(x-w)) / (1 + w)
    // 簡略化: w_new = w * (1 + x - w - ln(w)) / (1 + w)
    float lnw = logf_approx(std::max(w, 1e-10f));
    float r = x - w - lnw;
    return w * (1.0f + r / (1.0f + w));
}

// =============================================================================
// Fukushima-style Wright Omega (minimax有理関数近似)
// Newton補正なしで十分な精度を達成
// 参考: Fukushima 2013/2020, Boost.Math Lambert W
// =============================================================================

// 区分有理関数近似 (DAFx2019係数を有理関数に拡張)
// omega_fast: Newton補正なしの高速版
inline float omega_fast(float x) {
    // 領域1: x < -3.0 (ω ≈ exp(x), 非常に小さい)
    if (x < -3.0f) {
        return expf_approx(x);
    }
    // 領域2: -3.0 ≤ x < 0.0 (遷移領域、有理関数近似)
    else if (x < 0.0f) {
        // 有理関数 P(x)/Q(x) for [-3, 0]
        // minimax近似 (相対誤差 < 0.5%)
        float num = 0.5671f + x * (0.3679f + x * (0.1185f + x * 0.0218f));
        float den = 1.0f + x * (0.1864f + x * 0.0209f);
        return num / den;
    }
    // 領域3: 0.0 ≤ x < 3.0 (主要領域、高精度有理関数)
    else if (x < 3.0f) {
        // Padé近似 for [0, 3]
        float num = 0.5671f + x * (0.7028f + x * (0.2216f + x * 0.0278f));
        float den = 1.0f + x * (0.2357f + x * 0.0278f);
        return num / den;
    }
    // 領域4: 3.0 ≤ x < 10.0 (中間領域)
    else if (x < 10.0f) {
        // ω ≈ x - ln(x) + ln(x)/x の改良
        float lnx = logf_approx(x);
        float t = lnx / x;
        return x - lnx + t * (1.0f + t * 0.5f);
    }
    // 領域5: x ≥ 10.0 (漸近領域)
    else {
        return x - logf_approx(x);
    }
}

// omega_fast2: より高精度な5次有理関数版
inline float omega_fast2(float x) {
    if (x < -2.5f) {
        return expf_approx(x);
    }
    else if (x < 5.0f) {
        // 広域有理関数近似 [-2.5, 5]
        // ω(0) = W(1) ≈ 0.5671
        // ω(1) = W(e) ≈ 1.0
        // ω(-1) = W(1/e) ≈ 0.2785
        float t = x + 2.5f;  // t ∈ [0, 7.5]
        // 5次/3次有理関数
        float num = 0.0821f + t * (0.1978f + t * (0.1336f + t * (0.0291f + t * 0.00187f)));
        float den = 1.0f + t * (-0.0543f + t * 0.00738f);
        return num / den;
    }
    else {
        float lnx = logf_approx(x);
        return x - lnx + lnx / x;
    }
}

// =============================================================================
// Lambert W (omega4) ベースのダイオード電流・コンダクタンス
//
// Wright Omega関数 ω(x) = W(e^x) を使用して exp(V/Vt) を近似計算。
// ダイオード方程式: I = Is * (exp(V/Vt) - 1)
//
// omega4 は exp の良い近似となる:
//   exp(x) ≈ ω(x) * (1 + 1/ω(x)) for large x
//   (低域では直接 exp(ω(x)-x) の関係を使用)
// =============================================================================
inline void diode_iv_lambertw(float v, float& i, float& g) {
    constexpr float v_crit = V_T * 30.0f;

    if (v > v_crit) {
        // 線形外挿（大きなvでのオーバーフロー防止）
        float exp_crit = fast_exp(v_crit * V_T_INV);
        i = I_S * (exp_crit - 1.0f) + I_S * V_T_INV * exp_crit * (v - v_crit);
        g = I_S * V_T_INV * exp_crit;
        return;
    }
    if (v < -10.0f * V_T) {
        i = -I_S;
        g = 1e-12f;
        return;
    }

    // Wright Omega (omega4) を使用して exp を近似
    // omega4(x) は ω(x) = W(e^x) を4次精度で計算
    // exp(x) = ω(x) * e^(x - ω(x)) ≈ ω(x) * (1 + (x - ω(x))) for small correction
    float x = v * V_T_INV;
    float w = omega4(x);
    // exp(x) ≈ w * exp(x - w) だが、omega4の定義より x = w + ln(w)
    // したがって exp(x) = exp(w + ln(w)) = w * exp(w)
    // しかしNewton補正後は w ≈ ω(x) なので exp(x) ≈ w + w^2/2 + ...
    // より正確には: exp(x) = w * (1 + w) / (1 + w - (x - w - ln(w)))
    // 簡略化: exp(x) ≈ w * (w + 1) / w = w + 1 (for w >> 1)
    // 正しい変換: y = ω(x) ならば e^x = y * e^(x-y) = y * e^(ln(y)) = y^2 (when y = ω(x))
    // 実際は: e^x = y * (y + 1) / y = y + 1 ではない
    //
    // 正しいアプローチ: omega4の出力から直接expを再構成
    // omega4(x) = W(e^x) を計算、W(z) = w ⇔ w * e^w = z
    // z = e^x より e^x = w * e^w、したがって exp(x) = w * exp(w)
    float exp_w = fast_exp(w);
    float exp_v = w * exp_w;

    i = I_S * (exp_v - 1.0f);
    g = I_S * V_T_INV * exp_v + 1e-12f;
}

// =============================================================================
// omega3 (Newton補正なし) ベースのダイオード電流・コンダクタンス
//
// omega3は多項式近似のみでNewton補正を行わない高速版。
// omega4と同じ変換を使用するが、精度は若干劣る。
// =============================================================================
inline void diode_iv_omega3(float v, float& i, float& g) {
    constexpr float v_crit = V_T * 30.0f;

    if (v > v_crit) {
        // 線形外挿
        float exp_crit = fast_exp(v_crit * V_T_INV);
        i = I_S * (exp_crit - 1.0f) + I_S * V_T_INV * exp_crit * (v - v_crit);
        g = I_S * V_T_INV * exp_crit;
        return;
    }
    if (v < -10.0f * V_T) {
        i = -I_S;
        g = 1e-12f;
        return;
    }

    // omega3 (Newton補正なしの多項式近似) を使用
    float x = v * V_T_INV;
    float w = omega3(x);
    // exp(x) = w * exp(w) の関係を使用
    float exp_w = fast_exp(w);
    float exp_v = w * exp_w;

    i = I_S * (exp_v - 1.0f);
    g = I_S * V_T_INV * exp_v + 1e-12f;
}

// =============================================================================
// Lambert W を使ったダイオード電流の明示解
//
// ダイオード方程式: I = Is * (exp(V/Vt) - 1)
// 直列抵抗付き: V = Vd + I*R
// → I = Is * (exp((V - I*R)/Vt) - 1)
//
// Lambert W による明示解:
// I = Vt/R * W(Is*R/Vt * exp(V/Vt)) - Is
//
// Wright Omega形式 (W(e^x) = ω(x)):
// x = ln(Is*R/Vt) + V/Vt
// I = Vt/R * ω(x) - Is
// =============================================================================
struct DiodeLambertW {
    float vt_div_r;   // Vt / R
    float is_r_div_vt_ln;  // ln(Is * R / Vt)
    float is;
    float vt_inv;

    void init(float Is, float Vt, float R) {
        is = Is;
        vt_inv = 1.0f / Vt;
        vt_div_r = Vt / R;
        is_r_div_vt_ln = logf_approx(Is * R / Vt);
    }

    // ダイオード電流を明示的に計算 (Newton反復なし)
    inline float current(float v) const {
        float x = is_r_div_vt_ln + v * vt_inv;
        float w = omega4(x);
        return vt_div_r * w - is;
    }

    // 電流とコンダクタンスを同時計算
    inline void iv(float v, float& i, float& g) const {
        float x = is_r_div_vt_ln + v * vt_inv;
        float w = omega4(x);
        i = vt_div_r * w - is;
        // コンダクタンス: dI/dV = (1/R) * W / (1 + W)
        // ただしWが非常に小さい場合は Is/Vt * exp(V/Vt) に近似
        g = (w > 1e-6f) ? (vt_div_r * vt_inv * w / (1.0f + w)) : (is * vt_inv + 1e-12f);
    }
};

// =============================================================================
// テーブルルックアップexp (Meijer 1990 手法)
// 事前計算したテーブルからの補間でexp計算を高速化
// =============================================================================
constexpr int kExpTableSize = 256;
constexpr float kExpTableMin = -10.0f;  // v/Vt の最小値
constexpr float kExpTableMax = 45.0f;   // v/Vt の最大値
constexpr float kExpTableRange = kExpTableMax - kExpTableMin;
constexpr float kExpTableScale = (kExpTableSize - 1) / kExpTableRange;

inline float* get_exp_table() {
    static float table[kExpTableSize];
    static bool initialized = false;
    if (!initialized) {
        for (int i = 0; i < kExpTableSize; ++i) {
            float x = kExpTableMin + (static_cast<float>(i) / (kExpTableSize - 1)) * kExpTableRange;
            table[i] = fast_exp(x);
        }
        initialized = true;
    }
    return table;
}

inline float table_exp(float x) {
    float* table = get_exp_table();

    if (x <= kExpTableMin) {
        return table[0];
    }
    if (x >= kExpTableMax) {
        return table[kExpTableSize - 1];
    }

    float idx_f = (x - kExpTableMin) * kExpTableScale;
    int idx = static_cast<int>(idx_f);
    float frac = idx_f - static_cast<float>(idx);

    return table[idx] + frac * (table[idx + 1] - table[idx]);
}

inline void diode_iv_table(float v, float& i, float& g) {
    float v_scaled = v * V_T_INV;
    if (v_scaled > 40.0f) {
        float exp_crit = table_exp(40.0f);
        g = I_S * V_T_INV * exp_crit;
        i = I_S * (exp_crit - 1.0f) + g * (v - (40.0f * V_T));
    } else if (v_scaled < -10.0f) {
        i = -I_S;
        g = 1e-12f;
    } else {
        float exp_v = table_exp(v_scaled);
        i = I_S * (exp_v - 1.0f);
        g = I_S * V_T_INV * exp_v + 1e-12f;
    }
}

// =============================================================================
// WaveShaperSchurTable: Meijerテーブルルックアップ版
// =============================================================================
class WaveShaperSchurTable {
public:
    void setSampleRate(float sampleRate) {
        dt_ = 1.0f / sampleRate;
        g_c1_ = C1 / dt_;
        g_c2_ = C2 / dt_;
        float j11 = -g_c1_ - G3;
        inv_j11_ = 1.0f / j11;
        schur_j11_factor_ = G3 * G3 * inv_j11_;
        schur_f1_factor_ = G3 * inv_j11_;
        // テーブル初期化
        get_exp_table();
    }

    void reset() {
        v_c1_ = 0.0f; v_c2_ = 8.0f;
        v_b_ = 8.0f; v_e_ = 8.0f; v_c_ = V_COLL;
    }

    __attribute__((noinline))
    float process(float v_in) {
        const float v_c1_prev = v_c1_;
        const float v_c2_prev = v_c2_;
        float v_cap = v_in - v_c1_prev;
        float v_b = v_b_;
        float v_e = v_c2_prev;
        float v_c = v_c_;

        float v_eb = v_e - v_b;
        float v_cb = v_c - v_b;
        float i_ef, g_ef, i_cr, g_cr;
        diode_iv_table(v_eb, i_ef, g_ef);
        diode_iv_table(v_cb, i_cr, g_cr);

        float i_e = i_ef - ALPHA_R * i_cr;
        float i_c = ALPHA_F * i_ef - i_cr;
        float i_b = i_e - i_c;

        float f1 = g_c1_ * (v_in - v_cap - v_c1_prev) - G3 * (v_cap - v_b);
        float f2 = G2 * (v_in - v_b) + G3 * (v_cap - v_b) + i_b;
        float f3 = G4 * (V_CC - v_e) - i_e - g_c2_ * (v_e - v_c2_prev);
        float f4 = G5 * (V_COLL - v_c) + i_c;

        float j22 = -G2 - G3 - (1.0f - ALPHA_F) * g_ef - (1.0f - ALPHA_R) * g_cr;
        float j23 = (1.0f - ALPHA_F) * g_ef;
        float j24 = (1.0f - ALPHA_R) * g_cr;
        float j32 = g_ef - ALPHA_R * g_cr;
        float j33 = -G4 - g_ef - g_c2_;
        float j34 = ALPHA_R * g_cr;
        float j42 = -ALPHA_F * g_ef + g_cr;
        float j43 = ALPHA_F * g_ef;
        float j44 = -G5 - g_cr;

        float j22_p = j22 - schur_j11_factor_;
        float f2_p = f2 - schur_f1_factor_ * f1;
        float inv_j44 = 1.0f / j44;
        float j24_inv = j24 * inv_j44;
        float j34_inv = j34 * inv_j44;

        float j22_pp = j22_p - j24_inv * j42;
        float j23_pp = j23 - j24_inv * j43;
        float f2_pp = f2_p + j24_inv * f4;
        float j32_pp = j32 - j34_inv * j42;
        float j33_pp = j33 - j34_inv * j43;
        float f3_pp = f3 + j34_inv * f4;

        float det = j22_pp * j33_pp - j23_pp * j32_pp;
        float inv_det = 1.0f / det;
        float dv_b = (j33_pp * (-f2_pp) - j23_pp * (-f3_pp)) * inv_det;
        float dv_e = (j22_pp * (-f3_pp) - j32_pp * (-f2_pp)) * inv_det;
        float dv_c = (-f4 - j42 * dv_b - j43 * dv_e) * inv_j44;
        float dv_cap = (-f1 - G3 * dv_b) * inv_j11_;

        float max_dv = std::max({std::abs(dv_cap), std::abs(dv_b), std::abs(dv_e), std::abs(dv_c)});
        float damp = (max_dv > 0.5f) ? 0.5f / max_dv : 1.0f;

        v_cap += damp * dv_cap;
        v_b += damp * dv_b;
        v_e = std::clamp(v_e + damp * dv_e, 0.0f, V_CC + 0.5f);
        v_c = std::clamp(v_c + damp * dv_c, 0.0f, V_CC + 0.5f);

        v_c1_ = v_in - v_cap;
        v_c2_ = v_e;
        v_b_ = v_b;
        v_e_ = v_e;
        v_c_ = v_c;
        return v_c;
    }

private:
    float v_c1_ = 0.0f, v_c2_ = 8.0f;
    float v_b_ = 8.0f, v_e_ = 8.0f, v_c_ = V_COLL;
    float dt_ = 1.0f / 48000.0f;
    float g_c1_ = C1 / dt_;
    float g_c2_ = C2 / dt_;
    float inv_j11_ = 0.0f;
    float schur_j11_factor_ = 0.0f;
    float schur_f1_factor_ = 0.0f;
};

// =============================================================================
// WaveShaperSchurUltra: BCダイオード遅延評価 (exp 2→1回)
// =============================================================================
class WaveShaperSchurUltra {
public:
    void setSampleRate(float sampleRate) {
        dt_ = 1.0f / sampleRate;
        g_c1_ = C1 / dt_;
        g_c2_ = C2 / dt_;
        float j11 = -g_c1_ - G3;
        inv_j11_ = 1.0f / j11;
        schur_j11_factor_ = G3 * G3 * inv_j11_;
        schur_f1_factor_ = G3 * inv_j11_;
    }

    void reset() {
        v_c1_ = 0.0f; v_c2_ = 8.0f;
        v_b_ = 8.0f; v_e_ = 8.0f; v_c_ = V_COLL;
        i_cr_ = -I_S; g_cr_ = 1e-12f;
    }

    __attribute__((noinline))
    float process(float v_in) {
        const float v_c1_prev = v_c1_;
        const float v_c2_prev = v_c2_;
        float v_cap = v_in - v_c1_prev;
        float v_b = v_b_;
        float v_e = v_c2_prev;
        float v_c = v_c_;

        // E-Bダイオード: 現在値で評価 (exp 1回)
        float v_eb = v_e - v_b;
        float i_ef, g_ef;
        diode_iv(v_eb, i_ef, g_ef);

        // B-Cダイオード: 前サンプルの値を使用 (exp 0回)
        float i_cr = i_cr_;
        float g_cr = g_cr_;

        float i_e = i_ef - ALPHA_R * i_cr;
        float i_c = ALPHA_F * i_ef - i_cr;
        float i_b = i_e - i_c;

        float f1 = g_c1_ * (v_in - v_cap - v_c1_prev) - G3 * (v_cap - v_b);
        float f2 = G2 * (v_in - v_b) + G3 * (v_cap - v_b) + i_b;
        float f3 = G4 * (V_CC - v_e) - i_e - g_c2_ * (v_e - v_c2_prev);
        float f4 = G5 * (V_COLL - v_c) + i_c;

        float j22 = -G2 - G3 - (1.0f - ALPHA_F) * g_ef - (1.0f - ALPHA_R) * g_cr;
        float j23 = (1.0f - ALPHA_F) * g_ef;
        float j24 = (1.0f - ALPHA_R) * g_cr;
        float j32 = g_ef - ALPHA_R * g_cr;
        float j33 = -G4 - g_ef - g_c2_;
        float j34 = ALPHA_R * g_cr;
        float j42 = -ALPHA_F * g_ef + g_cr;
        float j43 = ALPHA_F * g_ef;
        float j44 = -G5 - g_cr;

        float j22_p = j22 - schur_j11_factor_;
        float f2_p = f2 - schur_f1_factor_ * f1;
        float inv_j44 = 1.0f / j44;
        float j24_inv = j24 * inv_j44;
        float j34_inv = j34 * inv_j44;

        float j22_pp = j22_p - j24_inv * j42;
        float j23_pp = j23 - j24_inv * j43;
        float f2_pp = f2_p + j24_inv * f4;
        float j32_pp = j32 - j34_inv * j42;
        float j33_pp = j33 - j34_inv * j43;
        float f3_pp = f3 + j34_inv * f4;

        float det = j22_pp * j33_pp - j23_pp * j32_pp;
        float inv_det = 1.0f / det;
        float dv_b = (j33_pp * (-f2_pp) - j23_pp * (-f3_pp)) * inv_det;
        float dv_e = (j22_pp * (-f3_pp) - j32_pp * (-f2_pp)) * inv_det;
        float dv_c = (-f4 - j42 * dv_b - j43 * dv_e) * inv_j44;
        float dv_cap = (-f1 - G3 * dv_b) * inv_j11_;

        float max_dv = std::max({std::abs(dv_cap), std::abs(dv_b), std::abs(dv_e), std::abs(dv_c)});
        float damp = (max_dv > 0.5f) ? 0.5f / max_dv : 1.0f;

        v_cap += damp * dv_cap;
        v_b += damp * dv_b;
        v_e = std::clamp(v_e + damp * dv_e, 0.0f, V_CC + 0.5f);
        v_c = std::clamp(v_c + damp * dv_c, 0.0f, V_CC + 0.5f);

        // B-Cダイオード更新（次サンプル用）
        float v_cb = v_c - v_b;
        diode_iv(v_cb, i_cr_, g_cr_);

        v_c1_ = v_in - v_cap;
        v_c2_ = v_e;
        v_b_ = v_b;
        v_e_ = v_e;
        v_c_ = v_c;
        return v_c;
    }

private:
    float v_c1_ = 0.0f, v_c2_ = 8.0f;
    float v_b_ = 8.0f, v_e_ = 8.0f, v_c_ = V_COLL;
    float dt_ = 1.0f / 48000.0f;
    float g_c1_ = C1 / dt_;
    float g_c2_ = C2 / dt_;
    float inv_j11_ = 0.0f;
    float schur_j11_factor_ = 0.0f;
    float schur_f1_factor_ = 0.0f;
    float i_cr_ = -I_S, g_cr_ = 1e-12f;
};

// =============================================================================
// WaveShaperSchur with mo_exp (pow2-based)
// =============================================================================
class WaveShaperSchurMo {
public:
    void setSampleRate(float sampleRate) {
        dt_ = 1.0f / sampleRate;
        g_c1_ = C1 / dt_;
        g_c2_ = C2 / dt_;
        float j11 = -g_c1_ - G3;
        inv_j11_ = 1.0f / j11;
        schur_j11_factor_ = G3 * G3 * inv_j11_;
        schur_f1_factor_ = G3 * inv_j11_;
    }

    void reset() {
        v_c1_ = 0.0f; v_c2_ = 8.0f;
        v_b_ = 8.0f; v_e_ = 8.0f; v_c_ = V_COLL;
    }

    __attribute__((noinline))
    float process(float v_in) {
        const float v_c1_prev = v_c1_;
        const float v_c2_prev = v_c2_;
        float v_cap = v_in - v_c1_prev;
        float v_b = v_b_;
        float v_e = v_c2_prev;
        float v_c = v_c_;

        float v_eb = v_e - v_b;
        float v_cb = v_c - v_b;
        float i_ef, g_ef, i_cr, g_cr;
        diode_iv_mo(v_eb, i_ef, g_ef);  // mo_exp版
        diode_iv_mo(v_cb, i_cr, g_cr);

        float i_e = i_ef - ALPHA_R * i_cr;
        float i_c = ALPHA_F * i_ef - i_cr;
        float i_b = i_e - i_c;

        float f1 = g_c1_ * (v_in - v_cap - v_c1_prev) - G3 * (v_cap - v_b);
        float f2 = G2 * (v_in - v_b) + G3 * (v_cap - v_b) + i_b;
        float f3 = G4 * (V_CC - v_e) - i_e - g_c2_ * (v_e - v_c2_prev);
        float f4 = G5 * (V_COLL - v_c) + i_c;

        float j22 = -G2 - G3 - (1.0f - ALPHA_F) * g_ef - (1.0f - ALPHA_R) * g_cr;
        float j23 = (1.0f - ALPHA_F) * g_ef;
        float j24 = (1.0f - ALPHA_R) * g_cr;
        float j32 = g_ef - ALPHA_R * g_cr;
        float j33 = -G4 - g_ef - g_c2_;
        float j34 = ALPHA_R * g_cr;
        float j42 = -ALPHA_F * g_ef + g_cr;
        float j43 = ALPHA_F * g_ef;
        float j44 = -G5 - g_cr;

        float j22_p = j22 - schur_j11_factor_;
        float f2_p = f2 - schur_f1_factor_ * f1;
        float inv_j44 = 1.0f / j44;
        float j24_inv = j24 * inv_j44;
        float j34_inv = j34 * inv_j44;

        float j22_pp = j22_p - j24_inv * j42;
        float j23_pp = j23 - j24_inv * j43;
        float f2_pp = f2_p + j24_inv * f4;
        float j32_pp = j32 - j34_inv * j42;
        float j33_pp = j33 - j34_inv * j43;
        float f3_pp = f3 + j34_inv * f4;

        float det = j22_pp * j33_pp - j23_pp * j32_pp;
        float inv_det = 1.0f / det;
        float dv_b = (j33_pp * (-f2_pp) - j23_pp * (-f3_pp)) * inv_det;
        float dv_e = (j22_pp * (-f3_pp) - j32_pp * (-f2_pp)) * inv_det;
        float dv_c = (-f4 - j42 * dv_b - j43 * dv_e) * inv_j44;
        float dv_cap = (-f1 - G3 * dv_b) * inv_j11_;

        float max_dv = std::max({std::abs(dv_cap), std::abs(dv_b), std::abs(dv_e), std::abs(dv_c)});
        float damp = (max_dv > 0.5f) ? 0.5f / max_dv : 1.0f;

        v_cap += damp * dv_cap;
        v_b += damp * dv_b;
        v_e = std::clamp(v_e + damp * dv_e, 0.0f, V_CC + 0.5f);
        v_c = std::clamp(v_c + damp * dv_c, 0.0f, V_CC + 0.5f);

        v_c1_ = v_in - v_cap;
        v_c2_ = v_e;
        v_b_ = v_b;
        v_e_ = v_e;
        v_c_ = v_c;
        return v_c;
    }

private:
    float v_c1_ = 0.0f, v_c2_ = 8.0f;
    float v_b_ = 8.0f, v_e_ = 8.0f, v_c_ = V_COLL;
    float dt_ = 1.0f / 48000.0f;
    float g_c1_ = C1 / dt_;
    float g_c2_ = C2 / dt_;
    float inv_j11_ = 0.0f;
    float schur_j11_factor_ = 0.0f;
    float schur_f1_factor_ = 0.0f;
};

// =============================================================================
// WaveShaperSchurLambertW: omega_fast2 (Fukushima-style) による明示解
// Newton反復なしの Wright Omega でダイオード電流を計算
// =============================================================================
class WaveShaperSchurLambertW {
public:
    void setSampleRate(float sampleRate) {
        dt_ = 1.0f / sampleRate;
        g_c1_ = C1 / dt_;
        g_c2_ = C2 / dt_;
        float j11 = -g_c1_ - G3;
        inv_j11_ = 1.0f / j11;
        schur_j11_factor_ = G3 * G3 * inv_j11_;
        schur_f1_factor_ = G3 * inv_j11_;
    }

    void reset() {
        v_c1_ = 0.0f; v_c2_ = 8.0f;
        v_b_ = 8.0f; v_e_ = 8.0f; v_c_ = V_COLL;
    }

    __attribute__((noinline))
    float process(float v_in) {
        const float v_c1_prev = v_c1_;
        const float v_c2_prev = v_c2_;
        float v_cap = v_in - v_c1_prev;
        float v_b = v_b_;
        float v_e = v_c2_prev;
        float v_c = v_c_;

        float v_eb = v_e - v_b;
        float v_cb = v_c - v_b;
        float i_ef, g_ef, i_cr, g_cr;
        diode_iv_lambertw(v_eb, i_ef, g_ef);  // omega_fast2版
        diode_iv_lambertw(v_cb, i_cr, g_cr);

        float i_e = i_ef - ALPHA_R * i_cr;
        float i_c = ALPHA_F * i_ef - i_cr;
        float i_b = i_e - i_c;

        float f1 = g_c1_ * (v_in - v_cap - v_c1_prev) - G3 * (v_cap - v_b);
        float f2 = G2 * (v_in - v_b) + G3 * (v_cap - v_b) + i_b;
        float f3 = G4 * (V_CC - v_e) - i_e - g_c2_ * (v_e - v_c2_prev);
        float f4 = G5 * (V_COLL - v_c) + i_c;

        float j22 = -G2 - G3 - (1.0f - ALPHA_F) * g_ef - (1.0f - ALPHA_R) * g_cr;
        float j23 = (1.0f - ALPHA_F) * g_ef;
        float j24 = (1.0f - ALPHA_R) * g_cr;
        float j32 = g_ef - ALPHA_R * g_cr;
        float j33 = -G4 - g_ef - g_c2_;
        float j34 = ALPHA_R * g_cr;
        float j42 = -ALPHA_F * g_ef + g_cr;
        float j43 = ALPHA_F * g_ef;
        float j44 = -G5 - g_cr;

        float j22_p = j22 - schur_j11_factor_;
        float f2_p = f2 - schur_f1_factor_ * f1;
        float inv_j44 = 1.0f / j44;
        float j24_inv = j24 * inv_j44;
        float j34_inv = j34 * inv_j44;

        float j22_pp = j22_p - j24_inv * j42;
        float j23_pp = j23 - j24_inv * j43;
        float f2_pp = f2_p + j24_inv * f4;
        float j32_pp = j32 - j34_inv * j42;
        float j33_pp = j33 - j34_inv * j43;
        float f3_pp = f3 + j34_inv * f4;

        float det = j22_pp * j33_pp - j23_pp * j32_pp;
        float inv_det = 1.0f / det;
        float dv_b = (j33_pp * (-f2_pp) - j23_pp * (-f3_pp)) * inv_det;
        float dv_e = (j22_pp * (-f3_pp) - j32_pp * (-f2_pp)) * inv_det;
        float dv_c = (-f4 - j42 * dv_b - j43 * dv_e) * inv_j44;
        float dv_cap = (-f1 - G3 * dv_b) * inv_j11_;

        float max_dv = std::max({std::abs(dv_cap), std::abs(dv_b), std::abs(dv_e), std::abs(dv_c)});
        float damp = (max_dv > 0.5f) ? 0.5f / max_dv : 1.0f;

        v_cap += damp * dv_cap;
        v_b += damp * dv_b;
        v_e = std::clamp(v_e + damp * dv_e, 0.0f, V_CC + 0.5f);
        v_c = std::clamp(v_c + damp * dv_c, 0.0f, V_CC + 0.5f);

        v_c1_ = v_in - v_cap;
        v_c2_ = v_e;
        v_b_ = v_b;
        v_e_ = v_e;
        v_c_ = v_c;
        return v_c;
    }

private:
    float v_c1_ = 0.0f, v_c2_ = 8.0f;
    float v_b_ = 8.0f, v_e_ = 8.0f, v_c_ = V_COLL;
    float dt_ = 1.0f / 48000.0f;
    float g_c1_ = C1 / dt_;
    float g_c2_ = C2 / dt_;
    float inv_j11_ = 0.0f;
    float schur_j11_factor_ = 0.0f;
    float schur_f1_factor_ = 0.0f;
};

// =============================================================================
// WaveShaperSchurOmega3: omega3のみ（Newton補正なし）による最高速実装
// omega_fast2と比較して、Newton-Raphson補正を省略
// =============================================================================
class WaveShaperSchurOmega3 {
public:
    void setSampleRate(float sampleRate) {
        dt_ = 1.0f / sampleRate;
        g_c1_ = C1 / dt_;
        g_c2_ = C2 / dt_;
        float j11 = -g_c1_ - G3;
        inv_j11_ = 1.0f / j11;
        schur_j11_factor_ = G3 * G3 * inv_j11_;
        schur_f1_factor_ = G3 * inv_j11_;
    }

    void reset() {
        v_c1_ = 0.0f; v_c2_ = 8.0f;
        v_b_ = 8.0f; v_e_ = 8.0f; v_c_ = V_COLL;
    }

    __attribute__((noinline))
    float process(float v_in) {
        const float v_c1_prev = v_c1_;
        const float v_c2_prev = v_c2_;
        float v_cap = v_in - v_c1_prev;
        float v_b = v_b_;
        float v_e = v_c2_prev;
        float v_c = v_c_;

        float v_eb = v_e - v_b;
        float v_cb = v_c - v_b;
        float i_ef, g_ef, i_cr, g_cr;
        diode_iv_omega3(v_eb, i_ef, g_ef);  // omega3のみ
        diode_iv_omega3(v_cb, i_cr, g_cr);

        float i_e = i_ef - ALPHA_R * i_cr;
        float i_c = ALPHA_F * i_ef - i_cr;
        float i_b = i_e - i_c;

        float f1 = g_c1_ * (v_in - v_cap - v_c1_prev) - G3 * (v_cap - v_b);
        float f2 = G2 * (v_in - v_b) + G3 * (v_cap - v_b) + i_b;
        float f3 = G4 * (V_CC - v_e) - i_e - g_c2_ * (v_e - v_c2_prev);
        float f4 = G5 * (V_COLL - v_c) + i_c;

        float j22 = -G2 - G3 - (1.0f - ALPHA_F) * g_ef - (1.0f - ALPHA_R) * g_cr;
        float j23 = (1.0f - ALPHA_F) * g_ef;
        float j24 = (1.0f - ALPHA_R) * g_cr;
        float j32 = g_ef - ALPHA_R * g_cr;
        float j33 = -G4 - g_ef - g_c2_;
        float j34 = ALPHA_R * g_cr;
        float j42 = -ALPHA_F * g_ef + g_cr;
        float j43 = ALPHA_F * g_ef;
        float j44 = -G5 - g_cr;

        float j22_p = j22 - schur_j11_factor_;
        float f2_p = f2 - schur_f1_factor_ * f1;
        float inv_j44 = 1.0f / j44;
        float j24_inv = j24 * inv_j44;
        float j34_inv = j34 * inv_j44;

        float j22_pp = j22_p - j24_inv * j42;
        float j23_pp = j23 - j24_inv * j43;
        float f2_pp = f2_p + j24_inv * f4;
        float j32_pp = j32 - j34_inv * j42;
        float j33_pp = j33 - j34_inv * j43;
        float f3_pp = f3 + j34_inv * f4;

        float det = j22_pp * j33_pp - j23_pp * j32_pp;
        float inv_det = 1.0f / det;
        float dv_b = (j33_pp * (-f2_pp) - j23_pp * (-f3_pp)) * inv_det;
        float dv_e = (j22_pp * (-f3_pp) - j32_pp * (-f2_pp)) * inv_det;
        float dv_c = (-f4 - j42 * dv_b - j43 * dv_e) * inv_j44;
        float dv_cap = (-f1 - G3 * dv_b) * inv_j11_;

        float max_dv = std::max({std::abs(dv_cap), std::abs(dv_b), std::abs(dv_e), std::abs(dv_c)});
        float damp = (max_dv > 0.5f) ? 0.5f / max_dv : 1.0f;

        v_cap += damp * dv_cap;
        v_b += damp * dv_b;
        v_e = std::clamp(v_e + damp * dv_e, 0.0f, V_CC + 0.5f);
        v_c = std::clamp(v_c + damp * dv_c, 0.0f, V_CC + 0.5f);

        v_c1_ = v_in - v_cap;
        v_c2_ = v_e;
        v_b_ = v_b;
        v_e_ = v_e;
        v_c_ = v_c;
        return v_c;
    }

private:
    float v_c1_ = 0.0f, v_c2_ = 8.0f;
    float v_b_ = 8.0f, v_e_ = 8.0f, v_c_ = V_COLL;
    float dt_ = 1.0f / 48000.0f;
    float g_c1_ = C1 / dt_;
    float g_c2_ = C2 / dt_;
    float inv_j11_ = 0.0f;
    float schur_j11_factor_ = 0.0f;
    float schur_f1_factor_ = 0.0f;
};

/**
 * WaveShaperDecoupled - コンダクタンス遅延型ソルバ
 *
 * 電流は現在の電圧で正確に計算し、ヤコビアンのコンダクタンスのみを
 * 1サンプル遅延させることで、精度を維持しながら計算を簡略化。
 *
 * 特徴:
 *   - diode_iv_lambertw を2回呼ぶ（現在の電圧で電流を計算）
 *   - g_ef, g_cr は前サンプルの値でヤコビアンを構築
 *   - 完全なSchur縮約を適用（WaveShaperSchurLambertWと同等の構造）
 *
 * 期待性能: WaveShaperSchurLambertWと同等（〜320 cycles）
 * 精度: Newton 1回相当（RMS 〜70mV vs ref）
 */
class WaveShaperDecoupled {
public:
    void setSampleRate(float sampleRate) {
        dt_ = 1.0f / sampleRate;
        g_c1_ = C1 / dt_;
        g_c2_ = C2 / dt_;
        float j11 = -g_c1_ - G3;
        inv_j11_ = 1.0f / j11;
        schur_j11_factor_ = G3 * G3 * inv_j11_;
        schur_f1_factor_ = G3 * inv_j11_;
    }

    void reset() {
        v_c1_ = 0.0f;
        v_c2_ = 8.0f;
        v_b_ = 8.0f;
        v_e_ = 8.0f;
        v_c_ = V_COLL;
        g_ef_prev_ = 1e-12f;
        g_cr_prev_ = 1e-12f;
    }

    __attribute__((noinline))
    float process(float v_in) {
        const float v_c1_prev = v_c1_;
        const float v_c2_prev = v_c2_;
        float v_cap = v_in - v_c1_prev;
        float v_b = v_b_;
        float v_e = v_c2_prev;
        float v_c = v_c_;

        // 両接合を現在の電圧で評価（電流は正確）
        float v_eb = v_e - v_b;
        float v_cb = v_c - v_b;
        float i_ef, g_ef, i_cr, g_cr;
        diode_iv_lambertw(v_eb, i_ef, g_ef);
        diode_iv_lambertw(v_cb, i_cr, g_cr);

        // コンダクタンスは前サンプルの値を使用（ヤコビアン簡略化）
        float g_ef_use = g_ef_prev_;
        float g_cr_use = g_cr_prev_;

        // Ebers-Moll電流（正確な電流）
        float i_e = i_ef - ALPHA_R * i_cr;
        float i_c = ALPHA_F * i_ef - i_cr;
        float i_b = i_e - i_c;

        // KCL残差
        float f1 = g_c1_ * (v_in - v_cap - v_c1_prev) - G3 * (v_cap - v_b);
        float f2 = G2 * (v_in - v_b) + G3 * (v_cap - v_b) + i_b;
        float f3 = G4 * (V_CC - v_e) - i_e - g_c2_ * (v_e - v_c2_prev);
        float f4 = G5 * (V_COLL - v_c) + i_c;

        // ヤコビアン（g_ef, g_crは前サンプル値）
        float j22 = -G2 - G3 - (1.0f - ALPHA_F) * g_ef_use - (1.0f - ALPHA_R) * g_cr_use;
        float j23 = (1.0f - ALPHA_F) * g_ef_use;
        float j24 = (1.0f - ALPHA_R) * g_cr_use;
        float j32 = g_ef_use - ALPHA_R * g_cr_use;
        float j33 = -G4 - g_ef_use - g_c2_;
        float j34 = ALPHA_R * g_cr_use;
        float j42 = -ALPHA_F * g_ef_use + g_cr_use;
        float j43 = ALPHA_F * g_ef_use;
        float j44 = -G5 - g_cr_use;

        // Schur縮約
        float j22_p = j22 - schur_j11_factor_;
        float f2_p = f2 - schur_f1_factor_ * f1;
        float inv_j44 = 1.0f / j44;
        float j24_inv = j24 * inv_j44;
        float j34_inv = j34 * inv_j44;

        float j22_pp = j22_p - j24_inv * j42;
        float j23_pp = j23 - j24_inv * j43;
        float f2_pp = f2_p + j24_inv * f4;
        float j32_pp = j32 - j34_inv * j42;
        float j33_pp = j33 - j34_inv * j43;
        float f3_pp = f3 + j34_inv * f4;

        // 2x2 Cramer
        float det = j22_pp * j33_pp - j23_pp * j32_pp;
        float inv_det = 1.0f / det;
        float dv_b = (j33_pp * (-f2_pp) - j23_pp * (-f3_pp)) * inv_det;
        float dv_e = (j22_pp * (-f3_pp) - j32_pp * (-f2_pp)) * inv_det;

        // 後退代入
        float dv_c = (-f4 - j42 * dv_b - j43 * dv_e) * inv_j44;
        float dv_cap = (-f1 - G3 * dv_b) * inv_j11_;

        // ダンピング
        float max_dv = std::max({std::abs(dv_cap), std::abs(dv_b), std::abs(dv_e), std::abs(dv_c)});
        float damp = (max_dv > 0.5f) ? 0.5f / max_dv : 1.0f;

        v_cap += damp * dv_cap;
        v_b += damp * dv_b;
        v_e = std::clamp(v_e + damp * dv_e, 0.0f, V_CC + 0.5f);
        v_c = std::clamp(v_c + damp * dv_c, 0.0f, V_CC + 0.5f);

        // コンダクタンスを更新（次サンプル用）
        g_ef_prev_ = g_ef;
        g_cr_prev_ = g_cr;

        // 状態更新
        v_c1_ = v_in - v_cap;
        v_c2_ = v_e;
        v_b_ = v_b;
        v_e_ = v_e;
        v_c_ = v_c;

        return v_c;
    }

private:
    float v_c1_ = 0.0f, v_c2_ = 8.0f;
    float v_b_ = 8.0f, v_e_ = 8.0f, v_c_ = V_COLL;
    float g_ef_prev_ = 1e-12f;
    float g_cr_prev_ = 1e-12f;
    float dt_ = 1.0f / 48000.0f;
    float g_c1_ = C1 / dt_;
    float g_c2_ = C2 / dt_;
    float inv_j11_ = 0.0f;
    float schur_j11_factor_ = 0.0f;
    float schur_f1_factor_ = 0.0f;
};

/**
 * WaveShaperHybridAdaptive - BJT ON/OFF状態適応型ソルバ
 *
 * BJT状態に基づいて異なるアルゴリズムを使用:
 * - BJT OFF状態（v_eb < 閾値）: 高精度モード - フルSchur縮約（現在のコンダクタンス使用）
 * - BJT ON状態（v_eb >= 閾値）: 高速モード - コンダクタンス遅延（前サンプル値使用）
 *
 * OFF→ONの遷移直後は高精度モードを1サンプル維持し、安定性を確保。
 */
class WaveShaperHybridAdaptive {
public:
    static constexpr float VEB_THRESHOLD = 0.3f;  // BJT ON/OFF判定閾値

    void setSampleRate(float sampleRate) {
        dt_ = 1.0f / sampleRate;
        g_c1_ = C1 / dt_;
        g_c2_ = C2 / dt_;
        float j11 = -g_c1_ - G3;
        inv_j11_ = 1.0f / j11;
        schur_j11_factor_ = G3 * G3 * inv_j11_;
        schur_f1_factor_ = G3 * inv_j11_;
    }

    void reset() {
        v_c1_ = 0.0f;
        v_c2_ = 8.0f;
        v_b_ = 8.0f;
        v_e_ = 8.0f;
        v_c_ = V_COLL;
        g_ef_prev_ = 1e-12f;
        g_cr_prev_ = 1e-12f;
        bjt_on_ = false;
    }

    __attribute__((noinline))
    float process(float v_in) {
        const float v_c1_prev = v_c1_;
        const float v_c2_prev = v_c2_;
        float v_cap = v_in - v_c1_prev;
        float v_b = v_b_;
        float v_e = v_c2_prev;
        float v_c = v_c_;

        // BJT状態の判定
        float v_eb = v_e - v_b;
        bool bjt_on_now = (v_eb >= VEB_THRESHOLD);

        // モード切り替え: OFF→ONの遷移直後は高精度モードを維持
        bool use_fast_mode = bjt_on_now && bjt_on_;

        // 両接合を評価
        float v_cb = v_c - v_b;
        float i_ef, g_ef, i_cr, g_cr;
        diode_iv(v_eb, i_ef, g_ef);
        diode_iv(v_cb, i_cr, g_cr);

        // ヤコビアンで使うコンダクタンスを選択
        float g_ef_use, g_cr_use;
        if (use_fast_mode) {
            // 高速モード: 前サンプルのコンダクタンスを使用
            g_ef_use = g_ef_prev_;
            g_cr_use = g_cr_prev_;
        } else {
            // 高精度モード: 現在のコンダクタンスを使用
            g_ef_use = g_ef;
            g_cr_use = g_cr;
        }

        // Ebers-Moll電流（常に正確な電流を使用）
        float i_e = i_ef - ALPHA_R * i_cr;
        float i_c = ALPHA_F * i_ef - i_cr;
        float i_b = i_e - i_c;

        // KCL残差
        float f1 = g_c1_ * (v_in - v_cap - v_c1_prev) - G3 * (v_cap - v_b);
        float f2 = G2 * (v_in - v_b) + G3 * (v_cap - v_b) + i_b;
        float f3 = G4 * (V_CC - v_e) - i_e - g_c2_ * (v_e - v_c2_prev);
        float f4 = G5 * (V_COLL - v_c) + i_c;

        // ヤコビアン
        float j22 = -G2 - G3 - (1.0f - ALPHA_F) * g_ef_use - (1.0f - ALPHA_R) * g_cr_use;
        float j23 = (1.0f - ALPHA_F) * g_ef_use;
        float j24 = (1.0f - ALPHA_R) * g_cr_use;
        float j32 = g_ef_use - ALPHA_R * g_cr_use;
        float j33 = -G4 - g_ef_use - g_c2_;
        float j34 = ALPHA_R * g_cr_use;
        float j42 = -ALPHA_F * g_ef_use + g_cr_use;
        float j43 = ALPHA_F * g_ef_use;
        float j44 = -G5 - g_cr_use;

        // Schur縮約
        float j22_p = j22 - schur_j11_factor_;
        float f2_p = f2 - schur_f1_factor_ * f1;
        float inv_j44 = 1.0f / j44;
        float j24_inv = j24 * inv_j44;
        float j34_inv = j34 * inv_j44;

        float j22_pp = j22_p - j24_inv * j42;
        float j23_pp = j23 - j24_inv * j43;
        float f2_pp = f2_p + j24_inv * f4;
        float j32_pp = j32 - j34_inv * j42;
        float j33_pp = j33 - j34_inv * j43;
        float f3_pp = f3 + j34_inv * f4;

        // 2x2 Cramer
        float det = j22_pp * j33_pp - j23_pp * j32_pp;
        float inv_det = 1.0f / det;
        float dv_b = (j33_pp * (-f2_pp) - j23_pp * (-f3_pp)) * inv_det;
        float dv_e = (j22_pp * (-f3_pp) - j32_pp * (-f2_pp)) * inv_det;

        // 後退代入
        float dv_c = (-f4 - j42 * dv_b - j43 * dv_e) * inv_j44;
        float dv_cap = (-f1 - G3 * dv_b) * inv_j11_;

        // ダンピング
        float max_dv = std::max({std::abs(dv_cap), std::abs(dv_b), std::abs(dv_e), std::abs(dv_c)});
        float damp = (max_dv > 0.5f) ? 0.5f / max_dv : 1.0f;

        v_cap += damp * dv_cap;
        v_b += damp * dv_b;
        v_e = std::clamp(v_e + damp * dv_e, 0.0f, V_CC + 0.5f);
        v_c = std::clamp(v_c + damp * dv_c, 0.0f, V_CC + 0.5f);

        // 状態更新
        g_ef_prev_ = g_ef;
        g_cr_prev_ = g_cr;
        bjt_on_ = bjt_on_now;

        v_c1_ = v_in - v_cap;
        v_c2_ = v_e;
        v_b_ = v_b;
        v_e_ = v_e;
        v_c_ = v_c;

        return v_c;
    }

private:
    float v_c1_ = 0.0f, v_c2_ = 8.0f;
    float v_b_ = 8.0f, v_e_ = 8.0f, v_c_ = V_COLL;
    float dt_ = 1.0f / 48000.0f;
    float g_c1_ = C1 / dt_;
    float g_c2_ = C2 / dt_;
    float inv_j11_ = 0.0f;
    float schur_j11_factor_ = 0.0f;
    float schur_f1_factor_ = 0.0f;
    float g_ef_prev_ = 1e-12f;
    float g_cr_prev_ = 1e-12f;
    bool bjt_on_ = false;
};

// =============================================================================
// 高速化バリエーション #1: Schraudolph純粋exp (ビット操作のみ)
// =============================================================================
inline float schraudolph_exp(float x) {
    // Schraudolph (1999): IEEE754ビット操作による高速exp
    // 精度: 約2-4%誤差、速度: 標準expの約1/10
    x = std::clamp(x, -87.0f, 88.0f);
    union { float f; int32_t i; } u;
    // 12102203.0f = (1 << 23) / ln(2)
    // 1064866805 = 127 << 23 - 調整項
    u.i = static_cast<int32_t>(12102203.0f * x + 1064866805.0f);
    return u.f;
}

inline void diode_iv_schraudolph(float v, float& i, float& g) {
    if (v > V_CRIT) {
        float exp_crit = schraudolph_exp(V_CRIT * V_T_INV);
        g = I_S * V_T_INV * exp_crit;
        i = I_S * (exp_crit - 1.0f) + g * (v - V_CRIT);
    } else if (v < -10.0f * V_T) {
        i = -I_S;
        g = 1e-12f;
    } else {
        float exp_v = schraudolph_exp(v * V_T_INV);
        i = I_S * (exp_v - 1.0f);
        g = I_S * V_T_INV * exp_v + 1e-12f;
    }
}

/**
 * WaveShaperSchraudolph - Schraudolph純粋exp版
 * 最速のexp近似を使用
 */
class WaveShaperSchraudolph {
public:
    void setSampleRate(float sampleRate) {
        dt_ = 1.0f / sampleRate;
        g_c1_ = C1 / dt_;
        g_c2_ = C2 / dt_;
        float j11 = -g_c1_ - G3;
        inv_j11_ = 1.0f / j11;
        schur_j11_factor_ = G3 * G3 * inv_j11_;
        schur_f1_factor_ = G3 * inv_j11_;
    }

    void reset() {
        v_c1_ = 0.0f; v_c2_ = 8.0f;
        v_b_ = 8.0f; v_e_ = 8.0f; v_c_ = V_COLL;
    }

    __attribute__((noinline))
    float process(float v_in) {
        const float v_c1_prev = v_c1_;
        const float v_c2_prev = v_c2_;

        float v_cap = v_in - v_c1_prev;
        float v_b = v_b_;
        float v_e = v_c2_prev;
        float v_c = v_c_;

        // 5回反復 (ベースラインと同じ)
        for (int iter = 0; iter < 5; ++iter) {
            float v_eb = v_e - v_b;
            float v_cb = v_c - v_b;

            float i_ef, g_ef, i_cr, g_cr;
            diode_iv_schraudolph(v_eb, i_ef, g_ef);
            diode_iv_schraudolph(v_cb, i_cr, g_cr);

            float i_e = i_ef - ALPHA_R * i_cr;
            float i_c = ALPHA_F * i_ef - i_cr;
            float i_b = i_e - i_c;

            float f1 = g_c1_ * (v_in - v_cap - v_c1_prev) - G3 * (v_cap - v_b);
            float f2 = G2 * (v_in - v_b) + G3 * (v_cap - v_b) + i_b;
            float f3 = G4 * (V_CC - v_e) - i_e - g_c2_ * (v_e - v_c2_prev);
            float f4 = G5 * (V_COLL - v_c) + i_c;

            float j22 = -G2 - G3 - (1.0f - ALPHA_F) * g_ef - (1.0f - ALPHA_R) * g_cr;
            float j23 = (1.0f - ALPHA_F) * g_ef;
            float j24 = (1.0f - ALPHA_R) * g_cr;
            float j32 = g_ef - ALPHA_R * g_cr;
            float j33 = -G4 - g_ef - g_c2_;
            float j34 = ALPHA_R * g_cr;
            float j42 = -ALPHA_F * g_ef + g_cr;
            float j43 = ALPHA_F * g_ef;
            float j44 = -G5 - g_cr;

            float j22_p = j22 - schur_j11_factor_;
            float f2_p = f2 - schur_f1_factor_ * f1;

            float inv_j44 = 1.0f / j44;
            float j24_inv = j24 * inv_j44;
            float j34_inv = j34 * inv_j44;

            float j22_pp = j22_p - j24_inv * j42;
            float j23_pp = j23 - j24_inv * j43;
            float f2_pp = f2_p + j24_inv * f4;

            float j32_pp = j32 - j34_inv * j42;
            float j33_pp = j33 - j34_inv * j43;
            float f3_pp = f3 + j34_inv * f4;

            float det = j22_pp * j33_pp - j23_pp * j32_pp;
            float inv_det = 1.0f / det;

            float dv_b = (j33_pp * (-f2_pp) - j23_pp * (-f3_pp)) * inv_det;
            float dv_e = (j22_pp * (-f3_pp) - j32_pp * (-f2_pp)) * inv_det;
            float dv_c = (-f4 - j42 * dv_b - j43 * dv_e) * inv_j44;
            float dv_cap = (-f1 - G3 * dv_b) * inv_j11_;

            float max_dv = std::max({std::abs(dv_cap), std::abs(dv_b),
                                     std::abs(dv_e), std::abs(dv_c)});
            float damp = (max_dv > 0.5f) ? 0.5f / max_dv : 1.0f;

            v_cap += damp * dv_cap;
            v_b += damp * dv_b;
            v_e = std::clamp(v_e + damp * dv_e, 0.0f, V_CC + 0.5f);
            v_c = std::clamp(v_c + damp * dv_c, 0.0f, V_CC + 0.5f);
        }

        v_c1_ = v_in - v_cap;
        v_c2_ = v_e;
        v_b_ = v_b;
        v_e_ = v_e;
        v_c_ = v_c;

        return v_c;
    }

private:
    float v_c1_ = 0.0f, v_c2_ = 8.0f;
    float v_b_ = 8.0f, v_e_ = 8.0f, v_c_ = V_COLL;
    float dt_ = 1.0f / 48000.0f;
    float g_c1_ = C1 / dt_;
    float g_c2_ = C2 / dt_;
    float inv_j11_ = 0.0f;
    float schur_j11_factor_ = 0.0f;
    float schur_f1_factor_ = 0.0f;
};

// =============================================================================
// 高速化バリエーション #2: tanh()ベースのソフトサチュレーションダイオード
// =============================================================================
// fast_tanh is already defined above (line ~387)

inline void diode_iv_tanh(float v, float& i, float& g) {
    // tanh()ベースのダイオードモデル
    // I = Is * (tanh((V - Vd) / (2*Vt)) + 1) / 2 * scale
    // これは exp() より滑らかで計算が速い
    constexpr float VD = 0.5f;     // ダイオード順電圧
    constexpr float SCALE = 2e-3f; // 最大電流スケール
    constexpr float VT2 = 2.0f * V_T;  // 2 * 熱電圧
    constexpr float VT2_INV = 1.0f / VT2;

    float x = (v - VD) * VT2_INV;
    float th = fast_tanh(x);

    // I = SCALE * (th + 1) / 2
    i = SCALE * 0.5f * (th + 1.0f);

    // g = dI/dV = SCALE / (2 * VT2) * (1 - th²)
    float sech2 = 1.0f - th * th;
    g = SCALE * 0.5f * VT2_INV * sech2 + 1e-9f;
}

// =============================================================================
// 高速化バリエーション #2b: PWL (区分線形) ダイオード + Newton 1回
// =============================================================================
inline void diode_iv_pwl(float v, float& i, float& g) {
    // 3セグメントPWL: OFF / 遷移 / ON
    // G_OFFを大きくして数値安定性を向上
    constexpr float V_ON = 0.6f;    // ダイオードON電圧
    constexpr float V_KNEE = 0.4f;  // 遷移開始電圧
    constexpr float G_ON = 0.04f;   // ON時コンダクタンス (1/25Ω)
    constexpr float G_OFF = 1e-6f;  // OFF時コンダクタンス (数値安定性)

    if (v < V_KNEE) {
        // OFF領域
        i = G_OFF * v;
        g = G_OFF;
    } else if (v < V_ON) {
        // 遷移領域 (3次補間でC1連続)
        float t = (v - V_KNEE) / (V_ON - V_KNEE);
        float t2 = t * t;
        float t3 = t2 * t;
        // Hermite補間: f(t) = (2t³-3t²+1)*f0 + (t³-2t²+t)*f0' + (-2t³+3t²)*f1 + (t³-t²)*f1'
        g = G_OFF + (3.0f * t2 - 2.0f * t3) * (G_ON - G_OFF);
        i = G_OFF * V_KNEE + (t - t2 + t3 / 3.0f) * (V_ON - V_KNEE) * (G_ON - G_OFF);
    } else {
        // ON領域
        float i_knee = G_OFF * V_KNEE + (V_ON - V_KNEE) * (G_ON - G_OFF) / 3.0f;
        i = i_knee + G_ON * (v - V_ON);
        g = G_ON;
    }
}

/**
 * WaveShaperPWL - 区分線形ダイオード + 1回Newton
 * exp()を完全に排除
 */
class WaveShaperPWL {
public:
    void setSampleRate(float sampleRate) {
        dt_ = 1.0f / sampleRate;
        g_c1_ = C1 / dt_;
        g_c2_ = C2 / dt_;
        float j11 = -g_c1_ - G3;
        inv_j11_ = 1.0f / j11;
        schur_j11_factor_ = G3 * G3 * inv_j11_;
        schur_f1_factor_ = G3 * inv_j11_;
    }

    void reset() {
        v_c1_ = 0.0f; v_c2_ = 8.0f;
        v_b_ = 8.0f; v_e_ = 8.0f; v_c_ = V_COLL;
    }

    __attribute__((noinline))
    float process(float v_in) {
        const float v_c1_prev = v_c1_;
        const float v_c2_prev = v_c2_;

        float v_cap = v_in - v_c1_prev;
        float v_b = v_b_;
        float v_e = v_c2_prev;
        float v_c = v_c_;

        // 1回反復のみ (PWLは収束が速い)
        float v_eb = v_e - v_b;
        float v_cb = v_c - v_b;

        float i_ef, g_ef, i_cr, g_cr;
        diode_iv_pwl(v_eb, i_ef, g_ef);
        diode_iv_pwl(v_cb, i_cr, g_cr);

        float i_e = i_ef - ALPHA_R * i_cr;
        float i_c = ALPHA_F * i_ef - i_cr;
        float i_b = i_e - i_c;

        float f1 = g_c1_ * (v_in - v_cap - v_c1_prev) - G3 * (v_cap - v_b);
        float f2 = G2 * (v_in - v_b) + G3 * (v_cap - v_b) + i_b;
        float f3 = G4 * (V_CC - v_e) - i_e - g_c2_ * (v_e - v_c2_prev);
        float f4 = G5 * (V_COLL - v_c) + i_c;

        float j22 = -G2 - G3 - (1.0f - ALPHA_F) * g_ef - (1.0f - ALPHA_R) * g_cr;
        float j23 = (1.0f - ALPHA_F) * g_ef;
        float j24 = (1.0f - ALPHA_R) * g_cr;
        float j32 = g_ef - ALPHA_R * g_cr;
        float j33 = -G4 - g_ef - g_c2_;
        float j34 = ALPHA_R * g_cr;
        float j42 = -ALPHA_F * g_ef + g_cr;
        float j43 = ALPHA_F * g_ef;
        float j44 = -G5 - g_cr;

        float j22_p = j22 - schur_j11_factor_;
        float f2_p = f2 - schur_f1_factor_ * f1;

        float inv_j44 = 1.0f / j44;
        float j24_inv = j24 * inv_j44;
        float j34_inv = j34 * inv_j44;

        float j22_pp = j22_p - j24_inv * j42;
        float j23_pp = j23 - j24_inv * j43;
        float f2_pp = f2_p + j24_inv * f4;

        float j32_pp = j32 - j34_inv * j42;
        float j33_pp = j33 - j34_inv * j43;
        float f3_pp = f3 + j34_inv * f4;

        float det = j22_pp * j33_pp - j23_pp * j32_pp;
        float inv_det = 1.0f / det;

        float dv_b = (j33_pp * (-f2_pp) - j23_pp * (-f3_pp)) * inv_det;
        float dv_e = (j22_pp * (-f3_pp) - j32_pp * (-f2_pp)) * inv_det;
        float dv_c = (-f4 - j42 * dv_b - j43 * dv_e) * inv_j44;
        float dv_cap = (-f1 - G3 * dv_b) * inv_j11_;

        v_cap += dv_cap;
        v_b += dv_b;
        v_e = std::clamp(v_e + dv_e, 0.0f, V_CC + 0.5f);
        v_c = std::clamp(v_c + dv_c, 0.0f, V_CC + 0.5f);

        v_c1_ = v_in - v_cap;
        v_c2_ = v_e;
        v_b_ = v_b;
        v_e_ = v_e;
        v_c_ = v_c;

        return v_c;
    }

private:
    float v_c1_ = 0.0f, v_c2_ = 8.0f;
    float v_b_ = 8.0f, v_e_ = 8.0f, v_c_ = V_COLL;
    float dt_ = 1.0f / 48000.0f;
    float g_c1_ = C1 / dt_;
    float g_c2_ = C2 / dt_;
    float inv_j11_ = 0.0f;
    float schur_j11_factor_ = 0.0f;
    float schur_f1_factor_ = 0.0f;
};

// =============================================================================
// 高速化バリエーション #2c: tanh()ベースのダイオード + 2回Newton
// =============================================================================
/**
 * WaveShaperTanh - tanh()ベースのダイオードモデル
 * exp()の代わりにtanh()を使用し、滑らかな特性を維持
 */
class WaveShaperTanh {
public:
    void setSampleRate(float sampleRate) {
        dt_ = 1.0f / sampleRate;
        g_c1_ = C1 / dt_;
        g_c2_ = C2 / dt_;
        float j11 = -g_c1_ - G3;
        inv_j11_ = 1.0f / j11;
        schur_j11_factor_ = G3 * G3 * inv_j11_;
        schur_f1_factor_ = G3 * inv_j11_;
    }

    void reset() {
        v_c1_ = 0.0f; v_c2_ = 8.0f;
        v_b_ = 8.0f; v_e_ = 8.0f; v_c_ = V_COLL;
    }

    __attribute__((noinline))
    float process(float v_in) {
        const float v_c1_prev = v_c1_;
        const float v_c2_prev = v_c2_;

        float v_cap = v_in - v_c1_prev;
        float v_b = v_b_;
        float v_e = v_c2_prev;
        float v_c = v_c_;

        // 2回反復
        for (int iter = 0; iter < 2; ++iter) {
            float v_eb = v_e - v_b;
            float v_cb = v_c - v_b;

            float i_ef, g_ef, i_cr, g_cr;
            diode_iv_tanh(v_eb, i_ef, g_ef);
            diode_iv_tanh(v_cb, i_cr, g_cr);

            float i_e = i_ef - ALPHA_R * i_cr;
            float i_c = ALPHA_F * i_ef - i_cr;
            float i_b = i_e - i_c;

            float f1 = g_c1_ * (v_in - v_cap - v_c1_prev) - G3 * (v_cap - v_b);
            float f2 = G2 * (v_in - v_b) + G3 * (v_cap - v_b) + i_b;
            float f3 = G4 * (V_CC - v_e) - i_e - g_c2_ * (v_e - v_c2_prev);
            float f4 = G5 * (V_COLL - v_c) + i_c;

            float j22 = -G2 - G3 - (1.0f - ALPHA_F) * g_ef - (1.0f - ALPHA_R) * g_cr;
            float j23 = (1.0f - ALPHA_F) * g_ef;
            float j24 = (1.0f - ALPHA_R) * g_cr;
            float j32 = g_ef - ALPHA_R * g_cr;
            float j33 = -G4 - g_ef - g_c2_;
            float j34 = ALPHA_R * g_cr;
            float j42 = -ALPHA_F * g_ef + g_cr;
            float j43 = ALPHA_F * g_ef;
            float j44 = -G5 - g_cr;

            float j22_p = j22 - schur_j11_factor_;
            float f2_p = f2 - schur_f1_factor_ * f1;

            float inv_j44 = 1.0f / j44;
            float j24_inv = j24 * inv_j44;
            float j34_inv = j34 * inv_j44;

            float j22_pp = j22_p - j24_inv * j42;
            float j23_pp = j23 - j24_inv * j43;
            float f2_pp = f2_p + j24_inv * f4;

            float j32_pp = j32 - j34_inv * j42;
            float j33_pp = j33 - j34_inv * j43;
            float f3_pp = f3 + j34_inv * f4;

            float det = j22_pp * j33_pp - j23_pp * j32_pp;
            float inv_det = 1.0f / det;

            float dv_b = (j33_pp * (-f2_pp) - j23_pp * (-f3_pp)) * inv_det;
            float dv_e = (j22_pp * (-f3_pp) - j32_pp * (-f2_pp)) * inv_det;
            float dv_c = (-f4 - j42 * dv_b - j43 * dv_e) * inv_j44;
            float dv_cap = (-f1 - G3 * dv_b) * inv_j11_;

            float max_dv = std::max({std::abs(dv_cap), std::abs(dv_b),
                                     std::abs(dv_e), std::abs(dv_c)});
            float damp = (max_dv > 0.5f) ? 0.5f / max_dv : 1.0f;

            v_cap += damp * dv_cap;
            v_b += damp * dv_b;
            v_e = std::clamp(v_e + damp * dv_e, 0.0f, V_CC + 0.5f);
            v_c = std::clamp(v_c + damp * dv_c, 0.0f, V_CC + 0.5f);
        }

        v_c1_ = v_in - v_cap;
        v_c2_ = v_e;
        v_b_ = v_b;
        v_e_ = v_e;
        v_c_ = v_c;

        return v_c;
    }

private:
    float v_c1_ = 0.0f, v_c2_ = 8.0f;
    float v_b_ = 8.0f, v_e_ = 8.0f, v_c_ = V_COLL;
    float dt_ = 1.0f / 48000.0f;
    float g_c1_ = C1 / dt_;
    float g_c2_ = C2 / dt_;
    float inv_j11_ = 0.0f;
    float schur_j11_factor_ = 0.0f;
    float schur_f1_factor_ = 0.0f;
};

// =============================================================================
// 高速化バリエーション #3: VCCS (電圧制御電流源) + tanh ソフトサチュレーション
// =============================================================================
/**
 * WaveShaperVCCS - tanh()ベースのソフトサチュレーションVCCS
 *
 * BJTをtanh()でソフトサチュレーションするVCCSとしてモデル化:
 *   Ic = Ic_max * tanh(gm * (Vbe - Vbe_on) / Ic_max)
 *
 * 暗黙的ソルバー (Backward Euler) で数値安定性を確保
 */
class WaveShaperVCCS {
public:
    static constexpr float VBE_ON = 0.55f;     // ダイオードON電圧
    static constexpr float IC_MAX = 1.5e-3f;   // 最大コレクタ電流
    static constexpr float GM = 0.02f;         // 相互コンダクタンス

    void setSampleRate(float sampleRate) {
        dt_ = 1.0f / sampleRate;
        g_c1_ = C1 / dt_;
        g_c2_ = C2 / dt_;
    }

    void reset() {
        v_c1_ = 0.0f;
        v_c2_ = 8.0f;
        v_b_ = 8.0f;
    }

    __attribute__((noinline))
    float process(float v_in) {
        // === 暗黙的ソルバー (Newton 2回) ===
        float v_cap = v_in - v_c1_;
        float v_b = v_b_;
        float v_e = v_c2_;

        for (int iter = 0; iter < 2; ++iter) {
            // ベース電圧 (KCL at base node)
            // I_R2 + I_R3 + I_b = 0
            // (Vcc - Vb)/R2 + (Vcap - Vb)/R3 + Ib = 0

            // BE電圧
            float v_be = v_b - v_e;
            float v_drive = v_be - VBE_ON;

            // tanh()ソフトサチュレーション
            float x = GM * v_drive / IC_MAX;
            float th = fast_tanh(x);
            float i_c = IC_MAX * (th + 1.0f) * 0.5f;  // 0 to IC_MAX

            // コンダクタンス g = dIc/dVbe
            float sech2 = 1.0f - th * th;
            float g_m = GM * 0.5f * sech2 + 1e-9f;

            float i_b = i_c / BETA_F;
            float i_e = i_b + i_c;

            // KCL残差
            float f_b = G2 * (V_CC - v_b) + G3 * (v_cap - v_b) - i_b;
            float f_e = G4 * (V_CC - v_e) - i_e - g_c2_ * (v_e - v_c2_);
            float f_cap = g_c1_ * (v_in - v_cap - v_c1_) - G3 * (v_cap - v_b);

            // ヤコビアン (簡略化)
            float g_b_total = G2 + G3 + g_m / BETA_F;
            float dv_b = f_b / g_b_total;

            float g_e_total = G4 + g_c2_ + (1.0f + 1.0f/BETA_F) * g_m;
            float dv_e = f_e / g_e_total;

            float g_cap_total = g_c1_ + G3;
            float dv_cap = f_cap / g_cap_total;

            v_b += 0.8f * dv_b;
            v_e = std::clamp(v_e + 0.8f * dv_e, 0.0f, V_CC);
            v_cap += 0.8f * dv_cap;
        }

        // 最終的なコレクタ電流と電圧
        float v_be = v_b - v_e;
        float th = fast_tanh(GM * (v_be - VBE_ON) / IC_MAX);
        float i_c = IC_MAX * (th + 1.0f) * 0.5f;

        // 状態更新
        v_c1_ = v_in - v_cap;
        v_c2_ = v_e;
        v_b_ = v_b;

        // コレクタ電圧
        float v_c = V_COLL - R5 * i_c;
        return std::clamp(v_c, 0.0f, V_CC);
    }

private:
    float v_c1_ = 0.0f;
    float v_c2_ = 8.0f;
    float v_b_ = 8.0f;
    float dt_ = 1.0f / 48000.0f;
    float g_c1_ = C1 / dt_;
    float g_c2_ = C2 / dt_;
};

// =============================================================================
// 高速化バリエーション #4: 1回反復Schur (前サンプル勾配を再利用)
// =============================================================================
/**
 * WaveShaperOneIter - 1回Newton反復 + 前サンプル勾配
 *
 * 前サンプルのコンダクタンスをヤコビアンに再利用し、
 * 電流は現在の電圧で正確に計算
 */
class WaveShaperOneIter {
public:
    void setSampleRate(float sampleRate) {
        dt_ = 1.0f / sampleRate;
        g_c1_ = C1 / dt_;
        g_c2_ = C2 / dt_;
        float j11 = -g_c1_ - G3;
        inv_j11_ = 1.0f / j11;
        schur_j11_factor_ = G3 * G3 * inv_j11_;
        schur_f1_factor_ = G3 * inv_j11_;
    }

    void reset() {
        v_c1_ = 0.0f; v_c2_ = 8.0f;
        v_b_ = 8.0f; v_e_ = 8.0f; v_c_ = V_COLL;
        g_ef_prev_ = 1e-12f;
        g_cr_prev_ = 1e-12f;
    }

    __attribute__((noinline))
    float process(float v_in) {
        const float v_c1_prev = v_c1_;
        const float v_c2_prev = v_c2_;

        float v_cap = v_in - v_c1_prev;
        float v_b = v_b_;
        float v_e = v_c2_prev;
        float v_c = v_c_;

        // 現在の電圧で電流を計算
        float v_eb = v_e - v_b;
        float v_cb = v_c - v_b;

        float i_ef, g_ef, i_cr, g_cr;
        diode_iv(v_eb, i_ef, g_ef);
        diode_iv(v_cb, i_cr, g_cr);

        // ヤコビアンには前サンプルのコンダクタンスを使用
        float g_ef_use = g_ef_prev_;
        float g_cr_use = g_cr_prev_;

        float i_e = i_ef - ALPHA_R * i_cr;
        float i_c = ALPHA_F * i_ef - i_cr;
        float i_b = i_e - i_c;

        float f1 = g_c1_ * (v_in - v_cap - v_c1_prev) - G3 * (v_cap - v_b);
        float f2 = G2 * (v_in - v_b) + G3 * (v_cap - v_b) + i_b;
        float f3 = G4 * (V_CC - v_e) - i_e - g_c2_ * (v_e - v_c2_prev);
        float f4 = G5 * (V_COLL - v_c) + i_c;

        float j22 = -G2 - G3 - (1.0f - ALPHA_F) * g_ef_use - (1.0f - ALPHA_R) * g_cr_use;
        float j23 = (1.0f - ALPHA_F) * g_ef_use;
        float j24 = (1.0f - ALPHA_R) * g_cr_use;
        float j32 = g_ef_use - ALPHA_R * g_cr_use;
        float j33 = -G4 - g_ef_use - g_c2_;
        float j34 = ALPHA_R * g_cr_use;
        float j42 = -ALPHA_F * g_ef_use + g_cr_use;
        float j43 = ALPHA_F * g_ef_use;
        float j44 = -G5 - g_cr_use;

        float j22_p = j22 - schur_j11_factor_;
        float f2_p = f2 - schur_f1_factor_ * f1;

        float inv_j44 = 1.0f / j44;
        float j24_inv = j24 * inv_j44;
        float j34_inv = j34 * inv_j44;

        float j22_pp = j22_p - j24_inv * j42;
        float j23_pp = j23 - j24_inv * j43;
        float f2_pp = f2_p + j24_inv * f4;

        float j32_pp = j32 - j34_inv * j42;
        float j33_pp = j33 - j34_inv * j43;
        float f3_pp = f3 + j34_inv * f4;

        float det = j22_pp * j33_pp - j23_pp * j32_pp;
        float inv_det = 1.0f / det;

        float dv_b = (j33_pp * (-f2_pp) - j23_pp * (-f3_pp)) * inv_det;
        float dv_e = (j22_pp * (-f3_pp) - j32_pp * (-f2_pp)) * inv_det;
        float dv_c = (-f4 - j42 * dv_b - j43 * dv_e) * inv_j44;
        float dv_cap = (-f1 - G3 * dv_b) * inv_j11_;

        float max_dv = std::max({std::abs(dv_cap), std::abs(dv_b),
                                 std::abs(dv_e), std::abs(dv_c)});
        float damp = (max_dv > 0.5f) ? 0.5f / max_dv : 1.0f;

        v_cap += damp * dv_cap;
        v_b += damp * dv_b;
        v_e = std::clamp(v_e + damp * dv_e, 0.0f, V_CC + 0.5f);
        v_c = std::clamp(v_c + damp * dv_c, 0.0f, V_CC + 0.5f);

        // 状態更新
        g_ef_prev_ = g_ef;
        g_cr_prev_ = g_cr;
        v_c1_ = v_in - v_cap;
        v_c2_ = v_e;
        v_b_ = v_b;
        v_e_ = v_e;
        v_c_ = v_c;

        return v_c;
    }

private:
    float v_c1_ = 0.0f, v_c2_ = 8.0f;
    float v_b_ = 8.0f, v_e_ = 8.0f, v_c_ = V_COLL;
    float dt_ = 1.0f / 48000.0f;
    float g_c1_ = C1 / dt_;
    float g_c2_ = C2 / dt_;
    float inv_j11_ = 0.0f;
    float schur_j11_factor_ = 0.0f;
    float schur_f1_factor_ = 0.0f;
    float g_ef_prev_ = 1e-12f;
    float g_cr_prev_ = 1e-12f;
};

// =============================================================================
// 高速化バリエーション #5: 2反復Schur (最適化)
// =============================================================================
/**
 * WaveShaperTwoIter - 2回Newton反復 (最適化版)
 */
class WaveShaperTwoIter {
public:
    void setSampleRate(float sampleRate) {
        dt_ = 1.0f / sampleRate;
        g_c1_ = C1 / dt_;
        g_c2_ = C2 / dt_;
        float j11 = -g_c1_ - G3;
        inv_j11_ = 1.0f / j11;
        schur_j11_factor_ = G3 * G3 * inv_j11_;
        schur_f1_factor_ = G3 * inv_j11_;
    }

    void reset() {
        v_c1_ = 0.0f; v_c2_ = 8.0f;
        v_b_ = 8.0f; v_e_ = 8.0f; v_c_ = V_COLL;
    }

    __attribute__((noinline))
    float process(float v_in) {
        const float v_c1_prev = v_c1_;
        const float v_c2_prev = v_c2_;

        float v_cap = v_in - v_c1_prev;
        float v_b = v_b_;
        float v_e = v_c2_prev;
        float v_c = v_c_;

        // 2回反復
        for (int iter = 0; iter < 2; ++iter) {
            float v_eb = v_e - v_b;
            float v_cb = v_c - v_b;

            float i_ef, g_ef, i_cr, g_cr;
            diode_iv(v_eb, i_ef, g_ef);
            diode_iv(v_cb, i_cr, g_cr);

            float i_e = i_ef - ALPHA_R * i_cr;
            float i_c = ALPHA_F * i_ef - i_cr;
            float i_b = i_e - i_c;

            float f1 = g_c1_ * (v_in - v_cap - v_c1_prev) - G3 * (v_cap - v_b);
            float f2 = G2 * (v_in - v_b) + G3 * (v_cap - v_b) + i_b;
            float f3 = G4 * (V_CC - v_e) - i_e - g_c2_ * (v_e - v_c2_prev);
            float f4 = G5 * (V_COLL - v_c) + i_c;

            float j22 = -G2 - G3 - (1.0f - ALPHA_F) * g_ef - (1.0f - ALPHA_R) * g_cr;
            float j23 = (1.0f - ALPHA_F) * g_ef;
            float j24 = (1.0f - ALPHA_R) * g_cr;
            float j32 = g_ef - ALPHA_R * g_cr;
            float j33 = -G4 - g_ef - g_c2_;
            float j34 = ALPHA_R * g_cr;
            float j42 = -ALPHA_F * g_ef + g_cr;
            float j43 = ALPHA_F * g_ef;
            float j44 = -G5 - g_cr;

            float j22_p = j22 - schur_j11_factor_;
            float f2_p = f2 - schur_f1_factor_ * f1;

            float inv_j44 = 1.0f / j44;
            float j24_inv = j24 * inv_j44;
            float j34_inv = j34 * inv_j44;

            float j22_pp = j22_p - j24_inv * j42;
            float j23_pp = j23 - j24_inv * j43;
            float f2_pp = f2_p + j24_inv * f4;

            float j32_pp = j32 - j34_inv * j42;
            float j33_pp = j33 - j34_inv * j43;
            float f3_pp = f3 + j34_inv * f4;

            float det = j22_pp * j33_pp - j23_pp * j32_pp;
            float inv_det = 1.0f / det;

            float dv_b = (j33_pp * (-f2_pp) - j23_pp * (-f3_pp)) * inv_det;
            float dv_e = (j22_pp * (-f3_pp) - j32_pp * (-f2_pp)) * inv_det;
            float dv_c = (-f4 - j42 * dv_b - j43 * dv_e) * inv_j44;
            float dv_cap = (-f1 - G3 * dv_b) * inv_j11_;

            float max_dv = std::max({std::abs(dv_cap), std::abs(dv_b),
                                     std::abs(dv_e), std::abs(dv_c)});
            float damp = (max_dv > 0.5f) ? 0.5f / max_dv : 1.0f;

            v_cap += damp * dv_cap;
            v_b += damp * dv_b;
            v_e = std::clamp(v_e + damp * dv_e, 0.0f, V_CC + 0.5f);
            v_c = std::clamp(v_c + damp * dv_c, 0.0f, V_CC + 0.5f);
        }

        v_c1_ = v_in - v_cap;
        v_c2_ = v_e;
        v_b_ = v_b;
        v_e_ = v_e;
        v_c_ = v_c;

        return v_c;
    }

private:
    float v_c1_ = 0.0f, v_c2_ = 8.0f;
    float v_b_ = 8.0f, v_e_ = 8.0f, v_c_ = V_COLL;
    float dt_ = 1.0f / 48000.0f;
    float g_c1_ = C1 / dt_;
    float g_c2_ = C2 / dt_;
    float inv_j11_ = 0.0f;
    float schur_j11_factor_ = 0.0f;
    float schur_f1_factor_ = 0.0f;
};

} // namespace tb303

// ============================================================================
// WDF-based TB-303 WaveShaper implementation using chowdsp_wdf
// ============================================================================
#include "../third_party/chowdsp_wdf/include/chowdsp_wdf/chowdsp_wdf.h"

namespace wdf_tb303 {

using namespace chowdsp::wdft;

/**
 * WDF TB-303 WaveShaper (簡略版)
 *
 * 回路の線形部分をWDFでモデル化し、
 * BJTのBE接合をダイオードとしてLambert Wで解く
 *
 * 構成:
 *   Vin --[C1]--+--[R3]-- ベース
 *               |
 *              [R2]-- Vcc
 *
 *   ベース --[BE diode]-- エミッタ
 *                          |
 *                  [R4]--[C2]-- Vcc/GND
 *
 *   コレクタ = Vcoll - R5 * Ic
 */
class WaveShaperWDF {
public:
    void setSampleRate(float sampleRate) {
        C1_.prepare(sampleRate);
    }

    void reset() {
        C1_.reset();
    }

    __attribute__((noinline))
    float process(float v_in) {
        // 入力電圧設定
        Vs_.setVoltage(v_in);

        // WDF散乱: ダイオードをルートとして解く
        // DiodePairTはLambert W (omega4) を内部で使用
        dp_.incident(P1_.reflected());
        P1_.incident(dp_.reflected());

        // C1の電圧を取得 (ベース近傍の電圧)
        float v_c1 = voltage<float>(C1_);

        // ベース電圧推定: R2/R3分圧
        constexpr float R2 = 100e3f;
        constexpr float R3 = 10e3f;
        float v_b = (v_c1 * (1.0f/R3) + V_CC * (1.0f/R2)) / ((1.0f/R2) + (1.0f/R3));

        // エミッタ電圧: 簡略化してV_CC * R4/(R4+Rbias) 付近
        // Forward-Active: Ve ≈ Vb - Vbe_on (0.6V)
        float v_e = std::max(v_b - 0.6f, 0.0f);

        // BE接合電流 (Shockley)
        float v_be = v_b - v_e;
        float exp_vbe = tb303::fast_exp(std::clamp(v_be, -1.0f, 0.8f) * V_T_INV);
        float i_b = I_S * (exp_vbe - 1.0f);
        i_b = std::clamp(i_b, -1e-6f, 1e-3f);

        // コレクタ電流 (Forward-Active)
        float i_c = BETA * i_b;

        // コレクタ電圧
        float v_c = V_COLL - R5 * i_c;
        return std::clamp(v_c, 0.0f, V_CC);
    }

private:
    static constexpr float V_CC = 12.0f;
    static constexpr float V_COLL = 5.33f;
    static constexpr float R5 = 10e3f;
    static constexpr float I_S = 1e-13f;
    static constexpr float V_T = 0.025865f;
    static constexpr float V_T_INV = 1.0f / V_T;
    static constexpr float BETA = 100.0f;

    // WDF要素: 入力側
    // Vs (1Ω) -- R3 (10kΩ) 直列
    ResistiveVoltageSourceT<float> Vs_ { 1.0f };
    ResistorT<float> R3_ { 10e3f };
    WDFSeriesT<float, ResistiveVoltageSourceT<float>, ResistorT<float>> S1_ { Vs_, R3_ };

    // C1 (10nF) を S1 と並列
    CapacitorT<float> C1_ { 10e-9f };
    WDFParallelT<float, decltype(S1_), CapacitorT<float>> P1_ { S1_, C1_ };

    // ダイオードペア (BE接合モデル) をルートとして接続
    // DiodeQuality::Good を使用 (omega4ベース)
    DiodePairT<float, decltype(P1_), DiodeQuality::Good> dp_ { P1_, 1e-13f, 0.025865f, 1.0f };
};

/**
 * WDF TB-303 WaveShaper (完全版)
 * R-type adaptorを使用してBJTの3ポート非線形性を正確にモデル化
 */
class WaveShaperWDFFull {
public:
    void setSampleRate(float sampleRate) {
        C1_.prepare(sampleRate);
        C2_.prepare(sampleRate);
    }

    void reset() {
        C1_.reset();
        C2_.reset();
        v_e_ = 8.0f;
    }

    __attribute__((noinline))
    float process(float v_in) {
        // 入力電圧設定
        Vs_.setVoltage(v_in);
        Vs_emit_.setVoltage(V_CC);

        // === 入力側WDF ===
        float a_in = S1_.reflected();
        float R_in = S1_.wdf.R;

        // === エミッタ側WDF ===
        float a_emit = S_emit_.reflected();
        float R_emit = S_emit_.wdf.R;

        // === BJT解析 (Ebers-Moll + Lambert W) ===
        // 入力波動変数からベース電圧を推定
        // v = (a + b) / 2 ≈ a/2 (初期推定)
        float v_b_est = a_in * 0.5f;
        float v_e = v_e_;  // 前サンプルのエミッタ電圧

        // BE接合電圧
        float v_be = v_b_est - v_e;

        // BE接合電流 (Lambert W明示解)
        float i_ef, g_ef;
        diode_lambertw_iv(v_be, R_in, i_ef, g_ef);

        // Forward-Active仮定: Ic = αf * Ief
        float i_c = ALPHA_F * i_ef;
        float i_b = (1.0f - ALPHA_F) * i_ef;
        float i_e = i_ef;

        // === 反射波計算 ===
        float b_in = a_in - 2.0f * R_in * i_b;
        float b_emit = a_emit - 2.0f * R_emit * i_e;

        // === 入射波を送る ===
        S1_.incident(b_in);
        S_emit_.incident(b_emit);

        // エミッタ電圧更新
        v_e_ = voltage<float>(C2_);

        // コレクタ電圧
        float v_c = V_COLL - R5 * i_c;
        return std::clamp(v_c, 0.0f, V_CC);
    }

private:
    // Lambert W ダイオード電流計算
    static void diode_lambertw_iv(float v, float R, float& i, float& g) {
        constexpr float Is = 1e-13f;
        constexpr float Vt = 0.025865f;
        constexpr float Vt_inv = 1.0f / Vt;

        // クランプして安定化
        v = std::clamp(v, -2.0f, 1.0f);

        float Is_R_div_Vt = Is * R * Vt_inv;
        float ln_term = tb303::logf_approx(std::max(Is_R_div_Vt, 1e-20f));
        float x = ln_term + v * Vt_inv + Is_R_div_Vt;

        // omega4 (Wright Omega with Newton correction)
        float w = chowdsp::Omega::omega4(x);

        float Vt_div_R = Vt / R;
        i = Vt_div_R * w - Is;
        g = (w > 1e-8f) ? (Vt_div_R * Vt_inv * w / (1.0f + w)) : (Is * Vt_inv + 1e-12f);
    }

    static constexpr float V_CC = 12.0f;
    static constexpr float V_COLL = 5.33f;
    static constexpr float R5 = 10e3f;
    static constexpr float ALPHA_F = 0.99f;

    float v_e_ = 8.0f;

    // 入力側WDF: Vs -- R3 -- C1
    ResistiveVoltageSourceT<float> Vs_ { 1.0f };
    ResistorT<float> R3_ { 10e3f };
    WDFSeriesT<float, ResistiveVoltageSourceT<float>, ResistorT<float>> S_vs_r3_ { Vs_, R3_ };
    CapacitorT<float> C1_ { 10e-9f };
    WDFSeriesT<float, decltype(S_vs_r3_), CapacitorT<float>> S1_ { S_vs_r3_, C1_ };

    // エミッタ側WDF: Vs_emit -- (R4 || C2)
    ResistiveVoltageSourceT<float> Vs_emit_ { 1.0f };
    ResistorT<float> R4_ { 22e3f };
    CapacitorT<float> C2_ { 1e-6f };
    WDFParallelT<float, ResistorT<float>, CapacitorT<float>> P_rc_ { R4_, C2_ };
    WDFSeriesT<float, ResistiveVoltageSourceT<float>, decltype(P_rc_)> S_emit_ { Vs_emit_, P_rc_ };
};

} // namespace wdf_tb303

// ============================================================================
// TB-303 Square Shaper (PNP based)
// ============================================================================
namespace dsp {
namespace oscillator {

class SquareShaper {
 public:
  void reset() { Ve = 0.0f; }

  struct Param {
    float dt = 1.0f / 48'000.0f;
    float shape = 0.5f;
  };

  void prepare(Param&& p) {
    const float Ce = 0.1e-6f + (p.shape * 0.9e-6f);
    g = p.dt / Ce;
  }

  struct IO {
    float in = 0;
    float out = 0;
  };

  __attribute__((noinline))
  IO process(IO&& io) {
    float Veb = Ve - io.in;
    float Ib = std::min(Is * (fast_exp(Veb * divVt) - 1.0f), Ib_max);
    float Ic_ideal = beta * Ib;
    float Ic_lin = Ve * divRc;
    float Ic_sat = Ic_lin * fast_tanh(Ve * divVce_sat);
    const float den_part = 0.1f * std::max(Ic_sat, 1e-6f);
    const float Ic = (Ic_sat * Ic_ideal) / (std::abs(Ic_ideal) + den_part);
    float Ie = Ib + Ic;
    float Icharge = (Vcc - Ve) * divRe;
    Ve += g * (Icharge - Ie);
    io.out = Rc * Ic;
    return io;
  }

 private:
  static float fast_exp(float x) {
    float y = x * 1.442695041f;
    int32_t i = static_cast<int32_t>(y + (y < 0.0f ? -0.5f : 0.5f));
    float f = y - static_cast<float>(i);
    if (f < 0.0f) {
      f += 1.0f;
      i -= 1;
    }
    union {
      float f;
      int32_t i;
    } conv;
    i += 127;
    i = std::clamp(i, 0, 254);
    conv.i = i << 23;
    float p = 1.0f + f * (0.6931471806f + f * (0.2402265069f + f * (0.0551041086f + f * 0.0095158916f)));
    return conv.f * p;
  }

  static constexpr float fast_tanh(float x) noexcept {
    return x / (std::abs(x) + 0.1f);
  }

  static constexpr float Re = 22e3f;
  static constexpr float Rc = 10e3f;
  static constexpr float divRc = 1.0f / Rc;
  static constexpr float divRe = 1.0f / Re;
  static constexpr float Vcc = 6.67f;
  static constexpr float Is = 55.9e-15f;
  static constexpr float NF = 1.01201f;
  static constexpr float Vt = 0.02585f * NF;
  static constexpr float divVt = 1.0f / Vt;
  static constexpr float beta = 205.0f;
  static constexpr float Ib_max = 66.7e-06f;
  static constexpr float Vce_sat = 0.1f;
  static constexpr float divVce_sat = 1.0f / Vce_sat;

  float Ve = 0.0f;
  float g = 0.0f;
};

}  // namespace oscillator
}  // namespace dsp

// ============================================================================
// Benchmark
// ============================================================================
constexpr int ITERATIONS = 10000;
constexpr float SAMPLE_RATE = 48000.0f;

volatile float sink = 0;

int main() {
#ifdef __arm__
    uart::init();

    uart::puts("\n");
    uart::puts("===========================================\n");
    uart::puts("TB-303 WaveShaper Benchmark (Cortex-M4)\n");
    uart::puts("===========================================\n");
    uart::puts("Iterations: ");
    uart::print_uint(ITERATIONS);
    uart::puts("\n\n");

    dwt::enable();

    // WaveShaperSchur benchmark
    tb303::WaveShaperSchur ws;
    ws.setSampleRate(SAMPLE_RATE);
    ws.reset();

    // Warmup
    float v_in = 12.0f;
    for (int i = 0; i < 100; ++i) {
        float out = ws.process(v_in);
        sink = out;
        v_in -= 0.01f;
        if (v_in < 5.5f) v_in = 12.0f;
    }

    // Reset for actual measurement
    ws.reset();
    v_in = 12.0f;

    dwt::reset();
    uint32_t start = dwt::cycles();

    for (int i = 0; i < ITERATIONS; ++i) {
        float out = ws.process(v_in);
        sink = out;
        // Simulate sawtooth input (40Hz @ 48kHz)
        v_in -= 0.00542f;  // 6.5V / 1200samples
        if (v_in < 5.5f) v_in = 12.0f;
    }

    uint32_t end = dwt::cycles();
    uint32_t total = end - start;
    uint32_t per_sample = total / ITERATIONS;

    uart::puts("--- WaveShaperSchur ---\n");
    uart::puts("Total cycles:      ");
    uart::print_uint(total);
    uart::puts("\n");
    uart::puts("Cycles per sample: ");
    uart::print_uint(per_sample);
    uart::puts("\n\n");

    // Calculate RT ratio (assuming 168MHz Cortex-M4)
    // 48000 samples/sec, 168MHz = 168000000 cycles/sec
    // Available cycles per sample = 168000000 / 48000 = 3500
    float rt_ratio = 3500.0f / static_cast<float>(per_sample);
    uart::puts("RT ratio @168MHz:  ");
    uart::print_float(rt_ratio, 1);
    uart::puts("x\n\n");

    // SquareShaper benchmark
    dsp::oscillator::SquareShaper sq;
    sq.reset();
    sq.prepare({1.0f / SAMPLE_RATE, 0.5f});

    // Warmup
    v_in = 8.0f;
    for (int i = 0; i < 100; ++i) {
        auto io = sq.process({v_in, 0.0f});
        sink = io.out;
        v_in -= 0.05f;
        if (v_in < 0.0f) v_in = 8.0f;
    }

    // Reset for actual measurement
    sq.reset();
    v_in = 8.0f;

    dwt::reset();
    start = dwt::cycles();

    for (int i = 0; i < ITERATIONS; ++i) {
        auto io = sq.process({v_in, 0.0f});
        sink = io.out;
        // Simulate input variation
        v_in -= 0.0067f;
        if (v_in < 0.0f) v_in = 8.0f;
    }

    end = dwt::cycles();
    uint32_t total_sq = end - start;
    uint32_t per_sample_sq = total_sq / ITERATIONS;

    uart::puts("--- SquareShaper (PNP) ---\n");
    uart::puts("Total cycles:      ");
    uart::print_uint(total_sq);
    uart::puts("\n");
    uart::puts("Cycles per sample: ");
    uart::print_uint(per_sample_sq);
    uart::puts("\n\n");

    float rt_ratio_sq = 3500.0f / static_cast<float>(per_sample_sq);
    uart::puts("RT ratio @168MHz:  ");
    uart::print_float(rt_ratio_sq, 1);
    uart::puts("x\n\n");

    // WaveShaperSchurMo benchmark (mo_exp based)
    tb303::WaveShaperSchurMo ws_mo;
    ws_mo.setSampleRate(SAMPLE_RATE);
    ws_mo.reset();

    // Warmup
    v_in = 12.0f;
    for (int i = 0; i < 100; ++i) {
        float out = ws_mo.process(v_in);
        sink = out;
        v_in -= 0.01f;
        if (v_in < 5.5f) v_in = 12.0f;
    }

    // Reset for actual measurement
    ws_mo.reset();
    v_in = 12.0f;

    dwt::reset();
    start = dwt::cycles();

    for (int i = 0; i < ITERATIONS; ++i) {
        float out = ws_mo.process(v_in);
        sink = out;
        v_in -= 0.00542f;
        if (v_in < 5.5f) v_in = 12.0f;
    }

    end = dwt::cycles();
    uint32_t total_mo = end - start;
    uint32_t per_sample_mo = total_mo / ITERATIONS;

    uart::puts("--- WaveShaperSchurMo (mo_exp) ---\n");
    uart::puts("Total cycles:      ");
    uart::print_uint(total_mo);
    uart::puts("\n");
    uart::puts("Cycles per sample: ");
    uart::print_uint(per_sample_mo);
    uart::puts("\n\n");

    float rt_ratio_mo = 3500.0f / static_cast<float>(per_sample_mo);
    uart::puts("RT ratio @168MHz:  ");
    uart::print_float(rt_ratio_mo, 1);
    uart::puts("x\n\n");

    // WaveShaperFast benchmark (Forward Euler, no Newton)
    tb303::WaveShaperFast ws_fast;
    ws_fast.setSampleRate(SAMPLE_RATE);
    ws_fast.reset();

    // Warmup
    v_in = 12.0f;
    for (int i = 0; i < 100; ++i) {
        float out = ws_fast.process(v_in);
        sink = out;
        v_in -= 0.01f;
        if (v_in < 5.5f) v_in = 12.0f;
    }

    // Reset for actual measurement
    ws_fast.reset();
    v_in = 12.0f;

    dwt::reset();
    start = dwt::cycles();

    for (int i = 0; i < ITERATIONS; ++i) {
        float out = ws_fast.process(v_in);
        sink = out;
        v_in -= 0.00542f;
        if (v_in < 5.5f) v_in = 12.0f;
    }

    end = dwt::cycles();
    uint32_t total_fast = end - start;
    uint32_t per_sample_fast = total_fast / ITERATIONS;

    uart::puts("--- WaveShaperFast (Forward Euler) ---\n");
    uart::puts("Total cycles:      ");
    uart::print_uint(total_fast);
    uart::puts("\n");
    uart::puts("Cycles per sample: ");
    uart::print_uint(per_sample_fast);
    uart::puts("\n\n");

    float rt_ratio_fast = 3500.0f / static_cast<float>(per_sample_fast);
    uart::puts("RT ratio @168MHz:  ");
    uart::print_float(rt_ratio_fast, 1);
    uart::puts("x\n\n");

    // WaveShaperSchurUltra benchmark (BC diode delayed, exp 2->1)
    tb303::WaveShaperSchurUltra ws_ultra;
    ws_ultra.setSampleRate(SAMPLE_RATE);
    ws_ultra.reset();

    v_in = 12.0f;
    for (int i = 0; i < 100; ++i) {
        float out = ws_ultra.process(v_in);
        sink = out;
        v_in -= 0.01f;
        if (v_in < 5.5f) v_in = 12.0f;
    }

    ws_ultra.reset();
    v_in = 12.0f;

    dwt::reset();
    start = dwt::cycles();

    for (int i = 0; i < ITERATIONS; ++i) {
        float out = ws_ultra.process(v_in);
        sink = out;
        v_in -= 0.00542f;
        if (v_in < 5.5f) v_in = 12.0f;
    }

    end = dwt::cycles();
    uint32_t total_ultra = end - start;
    uint32_t per_sample_ultra = total_ultra / ITERATIONS;

    uart::puts("--- WaveShaperSchurUltra (exp 2->1) ---\n");
    uart::puts("Total cycles:      ");
    uart::print_uint(total_ultra);
    uart::puts("\n");
    uart::puts("Cycles per sample: ");
    uart::print_uint(per_sample_ultra);
    uart::puts("\n\n");

    float rt_ratio_ultra = 3500.0f / static_cast<float>(per_sample_ultra);
    uart::puts("RT ratio @168MHz:  ");
    uart::print_float(rt_ratio_ultra, 1);
    uart::puts("x\n\n");

    // WaveShaperSchurTable benchmark (Meijer table lookup)
    tb303::WaveShaperSchurTable ws_table;
    ws_table.setSampleRate(SAMPLE_RATE);
    ws_table.reset();

    v_in = 12.0f;
    for (int i = 0; i < 100; ++i) {
        float out = ws_table.process(v_in);
        sink = out;
        v_in -= 0.01f;
        if (v_in < 5.5f) v_in = 12.0f;
    }

    ws_table.reset();
    v_in = 12.0f;

    dwt::reset();
    start = dwt::cycles();

    for (int i = 0; i < ITERATIONS; ++i) {
        float out = ws_table.process(v_in);
        sink = out;
        v_in -= 0.00542f;
        if (v_in < 5.5f) v_in = 12.0f;
    }

    end = dwt::cycles();
    uint32_t total_table = end - start;
    uint32_t per_sample_table = total_table / ITERATIONS;

    uart::puts("--- WaveShaperSchurTable (Meijer LUT) ---\n");
    uart::puts("Total cycles:      ");
    uart::print_uint(total_table);
    uart::puts("\n");
    uart::puts("Cycles per sample: ");
    uart::print_uint(per_sample_table);
    uart::puts("\n\n");

    float rt_ratio_table = 3500.0f / static_cast<float>(per_sample_table);
    uart::puts("RT ratio @168MHz:  ");
    uart::print_float(rt_ratio_table, 1);
    uart::puts("x\n\n");

    // WaveShaperWDF benchmark (chowdsp_wdf based)
    wdf_tb303::WaveShaperWDF ws_wdf;
    ws_wdf.setSampleRate(SAMPLE_RATE);
    ws_wdf.reset();

    v_in = 12.0f;
    for (int i = 0; i < 100; ++i) {
        float out = ws_wdf.process(v_in);
        sink = out;
        v_in -= 0.01f;
        if (v_in < 5.5f) v_in = 12.0f;
    }

    ws_wdf.reset();
    v_in = 12.0f;

    dwt::reset();
    start = dwt::cycles();

    for (int i = 0; i < ITERATIONS; ++i) {
        float out = ws_wdf.process(v_in);
        sink = out;
        v_in -= 0.00542f;
        if (v_in < 5.5f) v_in = 12.0f;
    }

    end = dwt::cycles();
    uint32_t total_wdf = end - start;
    uint32_t per_sample_wdf = total_wdf / ITERATIONS;

    uart::puts("--- WaveShaperWDF (chowdsp_wdf) ---\n");
    uart::puts("Total cycles:      ");
    uart::print_uint(total_wdf);
    uart::puts("\n");
    uart::puts("Cycles per sample: ");
    uart::print_uint(per_sample_wdf);
    uart::puts("\n\n");

    float rt_ratio_wdf = 3500.0f / static_cast<float>(per_sample_wdf);
    uart::puts("RT ratio @168MHz:  ");
    uart::print_float(rt_ratio_wdf, 1);
    uart::puts("x\n\n");

    // WaveShaperWDFFull benchmark
    wdf_tb303::WaveShaperWDFFull ws_wdf_full;
    ws_wdf_full.setSampleRate(SAMPLE_RATE);
    ws_wdf_full.reset();

    v_in = 12.0f;
    for (int i = 0; i < 100; ++i) {
        float out = ws_wdf_full.process(v_in);
        sink = out;
        v_in -= 0.01f;
        if (v_in < 5.5f) v_in = 12.0f;
    }

    ws_wdf_full.reset();
    v_in = 12.0f;

    dwt::reset();
    start = dwt::cycles();

    for (int i = 0; i < ITERATIONS; ++i) {
        float out = ws_wdf_full.process(v_in);
        sink = out;
        v_in -= 0.00542f;
        if (v_in < 5.5f) v_in = 12.0f;
    }

    end = dwt::cycles();
    uint32_t total_wdf_full = end - start;
    uint32_t per_sample_wdf_full = total_wdf_full / ITERATIONS;

    uart::puts("--- WaveShaperWDFFull (Lambert W) ---\n");
    uart::puts("Total cycles:      ");
    uart::print_uint(total_wdf_full);
    uart::puts("\n");
    uart::puts("Cycles per sample: ");
    uart::print_uint(per_sample_wdf_full);
    uart::puts("\n\n");

    float rt_ratio_wdf_full = 3500.0f / static_cast<float>(per_sample_wdf_full);
    uart::puts("RT ratio @168MHz:  ");
    uart::print_float(rt_ratio_wdf_full, 1);
    uart::puts("x\n\n");

    // WaveShaperSchurLambertW benchmark (omega_fast2 based)
    tb303::WaveShaperSchurLambertW ws_lambertw;
    ws_lambertw.setSampleRate(SAMPLE_RATE);
    ws_lambertw.reset();

    v_in = 12.0f;
    for (int i = 0; i < 100; ++i) {
        float out = ws_lambertw.process(v_in);
        sink = out;
        v_in -= 0.01f;
        if (v_in < 5.5f) v_in = 12.0f;
    }

    ws_lambertw.reset();
    v_in = 12.0f;

    dwt::reset();
    start = dwt::cycles();

    for (int i = 0; i < ITERATIONS; ++i) {
        float out = ws_lambertw.process(v_in);
        sink = out;
        v_in -= 0.00542f;
        if (v_in < 5.5f) v_in = 12.0f;
    }

    end = dwt::cycles();
    uint32_t total_lambertw = end - start;
    uint32_t per_sample_lambertw = total_lambertw / ITERATIONS;

    uart::puts("--- WaveShaperSchurLambertW (omega_fast2) ---\n");
    uart::puts("Total cycles:      ");
    uart::print_uint(total_lambertw);
    uart::puts("\n");
    uart::puts("Cycles per sample: ");
    uart::print_uint(per_sample_lambertw);
    uart::puts("\n\n");

    float rt_ratio_lambertw = 3500.0f / static_cast<float>(per_sample_lambertw);
    uart::puts("RT ratio @168MHz:  ");
    uart::print_float(rt_ratio_lambertw, 1);
    uart::puts("x\n\n");

    // WaveShaperSchurOmega3 benchmark (omega3 only - no Newton correction)
    tb303::WaveShaperSchurOmega3 ws_omega3;
    ws_omega3.setSampleRate(SAMPLE_RATE);
    ws_omega3.reset();

    v_in = 12.0f;
    for (int i = 0; i < 100; ++i) {
        float out = ws_omega3.process(v_in);
        sink = out;
        v_in -= 0.01f;
        if (v_in < 5.5f) v_in = 12.0f;
    }

    ws_omega3.reset();
    v_in = 12.0f;

    dwt::reset();
    start = dwt::cycles();

    for (int i = 0; i < ITERATIONS; ++i) {
        float out = ws_omega3.process(v_in);
        sink = out;
        v_in -= 0.00542f;
        if (v_in < 5.5f) v_in = 12.0f;
    }

    end = dwt::cycles();
    uint32_t total_omega3 = end - start;
    uint32_t per_sample_omega3 = total_omega3 / ITERATIONS;

    uart::puts("--- WaveShaperSchurOmega3 (omega3 only) ---\n");
    uart::puts("Total cycles:      ");
    uart::print_uint(total_omega3);
    uart::puts("\n");
    uart::puts("Cycles per sample: ");
    uart::print_uint(per_sample_omega3);
    uart::puts("\n\n");

    float rt_ratio_omega3 = 3500.0f / static_cast<float>(per_sample_omega3);
    uart::puts("RT ratio @168MHz:  ");
    uart::print_float(rt_ratio_omega3, 1);
    uart::puts("x\n\n");

    // WaveShaperDecoupled benchmark (matrix-free explicit solver)
    tb303::WaveShaperDecoupled ws_decoupled;
    ws_decoupled.setSampleRate(SAMPLE_RATE);
    ws_decoupled.reset();

    v_in = 12.0f;
    for (int i = 0; i < 100; ++i) {
        float out = ws_decoupled.process(v_in);
        sink = out;
        v_in -= 0.01f;
        if (v_in < 5.5f) v_in = 12.0f;
    }

    ws_decoupled.reset();
    v_in = 12.0f;

    dwt::reset();
    start = dwt::cycles();

    for (int i = 0; i < ITERATIONS; ++i) {
        float out = ws_decoupled.process(v_in);
        sink = out;
        v_in -= 0.00542f;
        if (v_in < 5.5f) v_in = 12.0f;
    }

    end = dwt::cycles();
    uint32_t total_decoupled = end - start;
    uint32_t per_sample_decoupled = total_decoupled / ITERATIONS;

    uart::puts("--- WaveShaperDecoupled (matrix-free) ---\n");
    uart::puts("Total cycles:      ");
    uart::print_uint(total_decoupled);
    uart::puts("\n");
    uart::puts("Cycles per sample: ");
    uart::print_uint(per_sample_decoupled);
    uart::puts("\n\n");

    float rt_ratio_decoupled = 3500.0f / static_cast<float>(per_sample_decoupled);
    uart::puts("RT ratio @168MHz:  ");
    uart::print_float(rt_ratio_decoupled, 1);
    uart::puts("x\n\n");

    // WaveShaperHybridAdaptive benchmark (BJT state-adaptive solver)
    tb303::WaveShaperHybridAdaptive ws_hybrid;
    ws_hybrid.setSampleRate(SAMPLE_RATE);
    ws_hybrid.reset();

    v_in = 12.0f;
    for (int i = 0; i < 100; ++i) {
        float out = ws_hybrid.process(v_in);
        sink = out;
        v_in -= 0.01f;
        if (v_in < 5.5f) v_in = 12.0f;
    }

    ws_hybrid.reset();
    v_in = 12.0f;

    dwt::reset();
    start = dwt::cycles();

    for (int i = 0; i < ITERATIONS; ++i) {
        float out = ws_hybrid.process(v_in);
        sink = out;
        v_in -= 0.00542f;
        if (v_in < 5.5f) v_in = 12.0f;
    }

    end = dwt::cycles();
    uint32_t total_hybrid = end - start;
    uint32_t per_sample_hybrid = total_hybrid / ITERATIONS;

    uart::puts("--- WaveShaperHybridAdaptive (BJT state) ---\n");
    uart::puts("Total cycles:      ");
    uart::print_uint(total_hybrid);
    uart::puts("\n");
    uart::puts("Cycles per sample: ");
    uart::print_uint(per_sample_hybrid);
    uart::puts("\n\n");

    float rt_ratio_hybrid = 3500.0f / static_cast<float>(per_sample_hybrid);
    uart::puts("RT ratio @168MHz:  ");
    uart::print_float(rt_ratio_hybrid, 1);
    uart::puts("x\n\n");

    // ================================================================
    // 新しい高速化バリエーション
    // ================================================================

    // WaveShaperPade benchmark (Padé [2,2] + Schur)
    tb303::WaveShaperPade ws_pade;
    ws_pade.setSampleRate(SAMPLE_RATE);
    ws_pade.reset();

    v_in = 12.0f;
    for (int i = 0; i < 100; ++i) {
        float out = ws_pade.process(v_in);
        sink = out;
        v_in -= 0.01f;
        if (v_in < 5.5f) v_in = 12.0f;
    }

    ws_pade.reset();
    v_in = 12.0f;

    dwt::reset();
    start = dwt::cycles();

    for (int i = 0; i < ITERATIONS; ++i) {
        float out = ws_pade.process(v_in);
        sink = out;
        v_in -= 0.00542f;
        if (v_in < 5.5f) v_in = 12.0f;
    }

    end = dwt::cycles();
    uint32_t total_pade = end - start;
    uint32_t per_sample_pade = total_pade / ITERATIONS;

    uart::puts("--- WaveShaperPade (Padé [2,2]) ---\n");
    uart::puts("Total cycles:      ");
    uart::print_uint(total_pade);
    uart::puts("\n");
    uart::puts("Cycles per sample: ");
    uart::print_uint(per_sample_pade);
    uart::puts("\n\n");

    float rt_ratio_pade = 3500.0f / static_cast<float>(per_sample_pade);
    uart::puts("RT ratio @168MHz:  ");
    uart::print_float(rt_ratio_pade, 1);
    uart::puts("x\n\n");

    // WaveShaperLUT benchmark (LUT + linear interpolation + Schur)
    tb303::WaveShaperLUT ws_lut;
    ws_lut.setSampleRate(SAMPLE_RATE);
    ws_lut.reset();

    v_in = 12.0f;
    for (int i = 0; i < 100; ++i) {
        float out = ws_lut.process(v_in);
        sink = out;
        v_in -= 0.01f;
        if (v_in < 5.5f) v_in = 12.0f;
    }

    ws_lut.reset();
    v_in = 12.0f;

    dwt::reset();
    start = dwt::cycles();

    for (int i = 0; i < ITERATIONS; ++i) {
        float out = ws_lut.process(v_in);
        sink = out;
        v_in -= 0.00542f;
        if (v_in < 5.5f) v_in = 12.0f;
    }

    end = dwt::cycles();
    uint32_t total_lut = end - start;
    uint32_t per_sample_lut = total_lut / ITERATIONS;

    uart::puts("--- WaveShaperLUT (LUT interp) ---\n");
    uart::puts("Total cycles:      ");
    uart::print_uint(total_lut);
    uart::puts("\n");
    uart::puts("Cycles per sample: ");
    uart::print_uint(per_sample_lut);
    uart::puts("\n\n");

    float rt_ratio_lut = 3500.0f / static_cast<float>(per_sample_lut);
    uart::puts("RT ratio @168MHz:  ");
    uart::print_float(rt_ratio_lut, 1);
    uart::puts("x\n\n");

    // WaveShaperSchurFast benchmark (BC junction skip)
    tb303::WaveShaperSchurFast ws_fast2;
    ws_fast2.setSampleRate(SAMPLE_RATE);
    ws_fast2.reset();

    v_in = 12.0f;
    for (int i = 0; i < 100; ++i) {
        float out = ws_fast2.process(v_in);
        sink = out;
        v_in -= 0.01f;
        if (v_in < 5.5f) v_in = 12.0f;
    }

    ws_fast2.reset();
    v_in = 12.0f;

    dwt::reset();
    start = dwt::cycles();

    for (int i = 0; i < ITERATIONS; ++i) {
        float out = ws_fast2.process(v_in);
        sink = out;
        v_in -= 0.00542f;
        if (v_in < 5.5f) v_in = 12.0f;
    }

    end = dwt::cycles();
    uint32_t total_fast2 = end - start;
    uint32_t per_sample_fast2 = total_fast2 / ITERATIONS;

    uart::puts("--- WaveShaperSchurFast (BC skip) ---\n");
    uart::puts("Total cycles:      ");
    uart::print_uint(total_fast2);
    uart::puts("\n");
    uart::puts("Cycles per sample: ");
    uart::print_uint(per_sample_fast2);
    uart::puts("\n\n");

    float rt_ratio_fast2 = 3500.0f / static_cast<float>(per_sample_fast2);
    uart::puts("RT ratio @168MHz:  ");
    uart::print_float(rt_ratio_fast2, 1);
    uart::puts("x\n\n");

    // WaveShaperPredict benchmark (state prediction)
    tb303::WaveShaperPredict ws_predict;
    ws_predict.setSampleRate(SAMPLE_RATE);
    ws_predict.reset();

    v_in = 12.0f;
    for (int i = 0; i < 100; ++i) {
        float out = ws_predict.process(v_in);
        sink = out;
        v_in -= 0.01f;
        if (v_in < 5.5f) v_in = 12.0f;
    }

    ws_predict.reset();
    v_in = 12.0f;

    dwt::reset();
    start = dwt::cycles();

    for (int i = 0; i < ITERATIONS; ++i) {
        float out = ws_predict.process(v_in);
        sink = out;
        v_in -= 0.00542f;
        if (v_in < 5.5f) v_in = 12.0f;
    }

    end = dwt::cycles();
    uint32_t total_predict = end - start;
    uint32_t per_sample_predict = total_predict / ITERATIONS;

    uart::puts("--- WaveShaperPredict (state pred) ---\n");
    uart::puts("Total cycles:      ");
    uart::print_uint(total_predict);
    uart::puts("\n");
    uart::puts("Cycles per sample: ");
    uart::print_uint(per_sample_predict);
    uart::puts("\n\n");

    float rt_ratio_predict = 3500.0f / static_cast<float>(per_sample_predict);
    uart::puts("RT ratio @168MHz:  ");
    uart::print_float(rt_ratio_predict, 1);
    uart::puts("x\n\n");

    // WaveShaperGeometric benchmark (geometric state mapping)
    tb303::WaveShaperGeometric ws_geo;
    ws_geo.setSampleRate(SAMPLE_RATE);
    ws_geo.reset();

    v_in = 12.0f;
    for (int i = 0; i < 100; ++i) {
        float out = ws_geo.process(v_in);
        sink = out;
        v_in -= 0.01f;
        if (v_in < 5.5f) v_in = 12.0f;
    }

    ws_geo.reset();
    v_in = 12.0f;

    dwt::reset();
    start = dwt::cycles();

    for (int i = 0; i < ITERATIONS; ++i) {
        float out = ws_geo.process(v_in);
        sink = out;
        v_in -= 0.00542f;
        if (v_in < 5.5f) v_in = 12.0f;
    }

    end = dwt::cycles();
    uint32_t total_geo = end - start;
    uint32_t per_sample_geo = total_geo / ITERATIONS;

    uart::puts("--- WaveShaperGeometric (hyperbolic) ---\n");
    uart::puts("Total cycles:      ");
    uart::print_uint(total_geo);
    uart::puts("\n");
    uart::puts("Cycles per sample: ");
    uart::print_uint(per_sample_geo);
    uart::puts("\n\n");

    float rt_ratio_geo = 3500.0f / static_cast<float>(per_sample_geo);
    uart::puts("RT ratio @168MHz:  ");
    uart::print_float(rt_ratio_geo, 1);
    uart::puts("x\n\n");

    // WaveShaperOptimized benchmark (pad_exp + analytical 3x3 inverse)
    tb303::WaveShaperOptimized ws_opt;
    ws_opt.setSampleRate(SAMPLE_RATE);
    ws_opt.reset();

    v_in = 12.0f;
    for (int i = 0; i < 100; ++i) {
        float out = ws_opt.process(v_in);
        sink = out;
        v_in -= 0.01f;
        if (v_in < 5.5f) v_in = 12.0f;
    }

    ws_opt.reset();
    v_in = 12.0f;

    dwt::reset();
    start = dwt::cycles();

    for (int i = 0; i < ITERATIONS; ++i) {
        float out = ws_opt.process(v_in);
        sink = out;
        v_in -= 0.00542f;
        if (v_in < 5.5f) v_in = 12.0f;
    }

    end = dwt::cycles();
    uint32_t total_opt = end - start;
    uint32_t per_sample_opt = total_opt / ITERATIONS;

    uart::puts("--- WaveShaperOptimized (pad_exp + 3x3) ---\n");
    uart::puts("Total cycles:      ");
    uart::print_uint(total_opt);
    uart::puts("\n");
    uart::puts("Cycles per sample: ");
    uart::print_uint(per_sample_opt);
    uart::puts("\n\n");

    float rt_ratio_opt = 3500.0f / static_cast<float>(per_sample_opt);
    uart::puts("RT ratio @168MHz:  ");
    uart::print_float(rt_ratio_opt, 1);
    uart::puts("x\n\n");

    // WaveShaperMinimal benchmark (minimal approx)
    tb303::WaveShaperMinimal ws_minimal;
    ws_minimal.setSampleRate(SAMPLE_RATE);
    ws_minimal.reset();

    v_in = 12.0f;
    for (int i = 0; i < 100; ++i) {
        float out = ws_minimal.process(v_in);
        sink = out;
        v_in -= 0.01f;
        if (v_in < 5.5f) v_in = 12.0f;
    }

    ws_minimal.reset();
    v_in = 12.0f;

    dwt::reset();
    start = dwt::cycles();

    for (int i = 0; i < ITERATIONS; ++i) {
        float out = ws_minimal.process(v_in);
        sink = out;
        v_in -= 0.00542f;
        if (v_in < 5.5f) v_in = 12.0f;
    }

    end = dwt::cycles();
    uint32_t total_minimal = end - start;
    uint32_t per_sample_minimal = total_minimal / ITERATIONS;

    uart::puts("--- WaveShaperMinimal (minimal) ---\n");
    uart::puts("Total cycles:      ");
    uart::print_uint(total_minimal);
    uart::puts("\n");
    uart::puts("Cycles per sample: ");
    uart::print_uint(per_sample_minimal);
    uart::puts("\n\n");

    float rt_ratio_minimal = 3500.0f / static_cast<float>(per_sample_minimal);
    uart::puts("RT ratio @168MHz:  ");
    uart::print_float(rt_ratio_minimal, 1);
    uart::puts("x\n\n");

    // WaveShaperSchraudolph benchmark (Schraudolph pure exp)
    tb303::WaveShaperSchraudolph ws_schraud;
    ws_schraud.setSampleRate(SAMPLE_RATE);
    ws_schraud.reset();

    v_in = 12.0f;
    for (int i = 0; i < 100; ++i) {
        float out = ws_schraud.process(v_in);
        sink = out;
        v_in -= 0.01f;
        if (v_in < 5.5f) v_in = 12.0f;
    }

    ws_schraud.reset();
    v_in = 12.0f;

    dwt::reset();
    start = dwt::cycles();

    for (int i = 0; i < ITERATIONS; ++i) {
        float out = ws_schraud.process(v_in);
        sink = out;
        v_in -= 0.00542f;
        if (v_in < 5.5f) v_in = 12.0f;
    }

    end = dwt::cycles();
    uint32_t total_schraud = end - start;
    uint32_t per_sample_schraud = total_schraud / ITERATIONS;

    uart::puts("--- WaveShaperSchraudolph (pure bitwise exp) ---\n");
    uart::puts("Total cycles:      ");
    uart::print_uint(total_schraud);
    uart::puts("\n");
    uart::puts("Cycles per sample: ");
    uart::print_uint(per_sample_schraud);
    uart::puts("\n\n");

    float rt_ratio_schraud = 3500.0f / static_cast<float>(per_sample_schraud);
    uart::puts("RT ratio @168MHz:  ");
    uart::print_float(rt_ratio_schraud, 1);
    uart::puts("x\n\n");

    // WaveShaperPWL benchmark (Piecewise Linear diode)
    tb303::WaveShaperPWL ws_pwl;
    ws_pwl.setSampleRate(SAMPLE_RATE);
    ws_pwl.reset();

    v_in = 12.0f;
    for (int i = 0; i < 100; ++i) {
        float out = ws_pwl.process(v_in);
        sink = out;
        v_in -= 0.01f;
        if (v_in < 5.5f) v_in = 12.0f;
    }

    ws_pwl.reset();
    v_in = 12.0f;

    dwt::reset();
    start = dwt::cycles();

    for (int i = 0; i < ITERATIONS; ++i) {
        float out = ws_pwl.process(v_in);
        sink = out;
        v_in -= 0.00542f;
        if (v_in < 5.5f) v_in = 12.0f;
    }

    end = dwt::cycles();
    uint32_t total_pwl = end - start;
    uint32_t per_sample_pwl = total_pwl / ITERATIONS;

    uart::puts("--- WaveShaperPWL (piecewise linear diode) ---\n");
    uart::puts("Total cycles:      ");
    uart::print_uint(total_pwl);
    uart::puts("\n");
    uart::puts("Cycles per sample: ");
    uart::print_uint(per_sample_pwl);
    uart::puts("\n\n");

    float rt_ratio_pwl = 3500.0f / static_cast<float>(per_sample_pwl);
    uart::puts("RT ratio @168MHz:  ");
    uart::print_float(rt_ratio_pwl, 1);
    uart::puts("x\n\n");

    // WaveShaperTanh benchmark (tanh-based diode model)
    tb303::WaveShaperTanh ws_tanh;
    ws_tanh.setSampleRate(SAMPLE_RATE);
    ws_tanh.reset();

    v_in = 12.0f;
    for (int i = 0; i < 100; ++i) {
        float out = ws_tanh.process(v_in);
        sink = out;
        v_in -= 0.01f;
        if (v_in < 5.5f) v_in = 12.0f;
    }

    ws_tanh.reset();
    v_in = 12.0f;

    dwt::reset();
    start = dwt::cycles();

    for (int i = 0; i < ITERATIONS; ++i) {
        float out = ws_tanh.process(v_in);
        sink = out;
        v_in -= 0.00542f;
        if (v_in < 5.5f) v_in = 12.0f;
    }

    end = dwt::cycles();
    uint32_t total_tanh = end - start;
    uint32_t per_sample_tanh = total_tanh / ITERATIONS;

    uart::puts("--- WaveShaperTanh (tanh-based diode) ---\n");
    uart::puts("Total cycles:      ");
    uart::print_uint(total_tanh);
    uart::puts("\n");
    uart::puts("Cycles per sample: ");
    uart::print_uint(per_sample_tanh);
    uart::puts("\n\n");

    float rt_ratio_tanh = 3500.0f / static_cast<float>(per_sample_tanh);
    uart::puts("RT ratio @168MHz:  ");
    uart::print_float(rt_ratio_tanh, 1);
    uart::puts("x\n\n");

    // WaveShaperVCCS benchmark (linear VCCS model)
    tb303::WaveShaperVCCS ws_vccs;
    ws_vccs.setSampleRate(SAMPLE_RATE);
    ws_vccs.reset();

    v_in = 12.0f;
    for (int i = 0; i < 100; ++i) {
        float out = ws_vccs.process(v_in);
        sink = out;
        v_in -= 0.01f;
        if (v_in < 5.5f) v_in = 12.0f;
    }

    ws_vccs.reset();
    v_in = 12.0f;

    dwt::reset();
    start = dwt::cycles();

    for (int i = 0; i < ITERATIONS; ++i) {
        float out = ws_vccs.process(v_in);
        sink = out;
        v_in -= 0.00542f;
        if (v_in < 5.5f) v_in = 12.0f;
    }

    end = dwt::cycles();
    uint32_t total_vccs = end - start;
    uint32_t per_sample_vccs = total_vccs / ITERATIONS;

    uart::puts("--- WaveShaperVCCS (linear transconductance) ---\n");
    uart::puts("Total cycles:      ");
    uart::print_uint(total_vccs);
    uart::puts("\n");
    uart::puts("Cycles per sample: ");
    uart::print_uint(per_sample_vccs);
    uart::puts("\n\n");

    float rt_ratio_vccs = 3500.0f / static_cast<float>(per_sample_vccs);
    uart::puts("RT ratio @168MHz:  ");
    uart::print_float(rt_ratio_vccs, 1);
    uart::puts("x\n\n");

    // WaveShaperOneIter benchmark (1 Newton iteration)
    tb303::WaveShaperOneIter ws_oneiter;
    ws_oneiter.setSampleRate(SAMPLE_RATE);
    ws_oneiter.reset();

    v_in = 12.0f;
    for (int i = 0; i < 100; ++i) {
        float out = ws_oneiter.process(v_in);
        sink = out;
        v_in -= 0.01f;
        if (v_in < 5.5f) v_in = 12.0f;
    }

    ws_oneiter.reset();
    v_in = 12.0f;

    dwt::reset();
    start = dwt::cycles();

    for (int i = 0; i < ITERATIONS; ++i) {
        float out = ws_oneiter.process(v_in);
        sink = out;
        v_in -= 0.00542f;
        if (v_in < 5.5f) v_in = 12.0f;
    }

    end = dwt::cycles();
    uint32_t total_oneiter = end - start;
    uint32_t per_sample_oneiter = total_oneiter / ITERATIONS;

    uart::puts("--- WaveShaperOneIter (1 Newton + prev gradient) ---\n");
    uart::puts("Total cycles:      ");
    uart::print_uint(total_oneiter);
    uart::puts("\n");
    uart::puts("Cycles per sample: ");
    uart::print_uint(per_sample_oneiter);
    uart::puts("\n\n");

    float rt_ratio_oneiter = 3500.0f / static_cast<float>(per_sample_oneiter);
    uart::puts("RT ratio @168MHz:  ");
    uart::print_float(rt_ratio_oneiter, 1);
    uart::puts("x\n\n");

    // WaveShaperTwoIter benchmark (2 Newton iterations)
    tb303::WaveShaperTwoIter ws_twoiter;
    ws_twoiter.setSampleRate(SAMPLE_RATE);
    ws_twoiter.reset();

    v_in = 12.0f;
    for (int i = 0; i < 100; ++i) {
        float out = ws_twoiter.process(v_in);
        sink = out;
        v_in -= 0.01f;
        if (v_in < 5.5f) v_in = 12.0f;
    }

    ws_twoiter.reset();
    v_in = 12.0f;

    dwt::reset();
    start = dwt::cycles();

    for (int i = 0; i < ITERATIONS; ++i) {
        float out = ws_twoiter.process(v_in);
        sink = out;
        v_in -= 0.00542f;
        if (v_in < 5.5f) v_in = 12.0f;
    }

    end = dwt::cycles();
    uint32_t total_twoiter = end - start;
    uint32_t per_sample_twoiter = total_twoiter / ITERATIONS;

    uart::puts("--- WaveShaperTwoIter (2 Newton iterations) ---\n");
    uart::puts("Total cycles:      ");
    uart::print_uint(total_twoiter);
    uart::puts("\n");
    uart::puts("Cycles per sample: ");
    uart::print_uint(per_sample_twoiter);
    uart::puts("\n\n");

    float rt_ratio_twoiter = 3500.0f / static_cast<float>(per_sample_twoiter);
    uart::puts("RT ratio @168MHz:  ");
    uart::print_float(rt_ratio_twoiter, 1);
    uart::puts("x\n\n");

    // WaveShaper3Var benchmark (3-variable Newton with v_cap elimination)
    tb303::WaveShaper3Var ws_3var;
    ws_3var.setSampleRate(SAMPLE_RATE);
    ws_3var.reset();

    v_in = 12.0f;
    for (int i = 0; i < 100; ++i) {
        float out = ws_3var.process(v_in);
        sink = out;
        v_in -= 0.01f;
        if (v_in < 5.5f) v_in = 12.0f;
    }

    ws_3var.reset();
    v_in = 12.0f;

    dwt::reset();
    start = dwt::cycles();

    for (int i = 0; i < ITERATIONS; ++i) {
        float out = ws_3var.process(v_in);
        sink = out;
        v_in -= 0.00542f;
        if (v_in < 5.5f) v_in = 12.0f;
    }

    end = dwt::cycles();
    uint32_t total_3var = end - start;
    uint32_t per_sample_3var = total_3var / ITERATIONS;

    uart::puts("--- WaveShaper3Var (3x3 Newton, v_cap elim) ---\n");
    uart::puts("Total cycles:      ");
    uart::print_uint(total_3var);
    uart::puts("\n");
    uart::puts("Cycles per sample: ");
    uart::print_uint(per_sample_3var);
    uart::puts("\n\n");

    float rt_ratio_3var = 3500.0f / static_cast<float>(per_sample_3var);
    uart::puts("RT ratio @168MHz:  ");
    uart::print_float(rt_ratio_3var, 1);
    uart::puts("x\n\n");

    // WaveShaper3VarOpt benchmark (exp multiplicative update + Jacobian factorization)
    tb303::WaveShaper3VarOpt ws_3varopt;
    ws_3varopt.setSampleRate(SAMPLE_RATE);
    ws_3varopt.reset();

    v_in = 12.0f;
    for (int i = 0; i < 100; ++i) {
        float out = ws_3varopt.process(v_in);
        sink = out;
        v_in -= 0.01f;
        if (v_in < 5.5f) v_in = 12.0f;
    }

    ws_3varopt.reset();
    v_in = 12.0f;

    dwt::reset();
    start = dwt::cycles();

    for (int i = 0; i < ITERATIONS; ++i) {
        float out = ws_3varopt.process(v_in);
        sink = out;
        v_in -= 0.00542f;
        if (v_in < 5.5f) v_in = 12.0f;
    }

    end = dwt::cycles();
    uint32_t total_3varopt = end - start;
    uint32_t per_sample_3varopt = total_3varopt / ITERATIONS;

    uart::puts("--- WaveShaper3VarOpt (exp mult, 2 iter) ---\n");
    uart::puts("Total cycles:      ");
    uart::print_uint(total_3varopt);
    uart::puts("\n");
    uart::puts("Cycles per sample: ");
    uart::print_uint(per_sample_3varopt);
    uart::puts("\n\n");

    float rt_ratio_3varopt = 3500.0f / static_cast<float>(per_sample_3varopt);
    uart::puts("RT ratio @168MHz:  ");
    uart::print_float(rt_ratio_3varopt, 1);
    uart::puts("x\n\n");

    // WaveShaper3Var2Iter benchmark (2 iterations)
    tb303::WaveShaper3Var2Iter ws_3var2;
    ws_3var2.setSampleRate(SAMPLE_RATE);
    ws_3var2.reset();

    v_in = 12.0f;
    for (int i = 0; i < 100; ++i) {
        float out = ws_3var2.process(v_in);
        sink = out;
        v_in -= 0.01f;
        if (v_in < 5.5f) v_in = 12.0f;
    }

    ws_3var2.reset();
    v_in = 12.0f;

    dwt::reset();
    start = dwt::cycles();

    for (int i = 0; i < ITERATIONS; ++i) {
        float out = ws_3var2.process(v_in);
        sink = out;
        v_in -= 0.00542f;
        if (v_in < 5.5f) v_in = 12.0f;
    }

    end = dwt::cycles();
    uint32_t total_3var2 = end - start;
    uint32_t per_sample_3var2 = total_3var2 / ITERATIONS;

    uart::puts("--- WaveShaper3Var2Iter (2 Newton) ---\n");
    uart::puts("Total cycles:      ");
    uart::print_uint(total_3var2);
    uart::puts("\n");
    uart::puts("Cycles per sample: ");
    uart::print_uint(per_sample_3var2);
    uart::puts("\n\n");

    float rt_ratio_3var2 = 3500.0f / static_cast<float>(per_sample_3var2);
    uart::puts("RT ratio @168MHz:  ");
    uart::print_float(rt_ratio_3var2, 1);
    uart::puts("x\n\n");

    // WaveShaperSchur2Iter benchmark (2 iterations)
    tb303::WaveShaperSchur2Iter ws_schur2;
    ws_schur2.setSampleRate(SAMPLE_RATE);
    ws_schur2.reset();

    v_in = 12.0f;
    for (int i = 0; i < 100; ++i) {
        float out = ws_schur2.process(v_in);
        sink = out;
        v_in -= 0.01f;
        if (v_in < 5.5f) v_in = 12.0f;
    }

    ws_schur2.reset();
    v_in = 12.0f;

    dwt::reset();
    start = dwt::cycles();

    for (int i = 0; i < ITERATIONS; ++i) {
        float out = ws_schur2.process(v_in);
        sink = out;
        v_in -= 0.00542f;
        if (v_in < 5.5f) v_in = 12.0f;
    }

    end = dwt::cycles();
    uint32_t total_schur2 = end - start;
    uint32_t per_sample_schur2 = total_schur2 / ITERATIONS;

    uart::puts("--- WaveShaperSchur2Iter (2 Newton) ---\n");
    uart::puts("Total cycles:      ");
    uart::print_uint(total_schur2);
    uart::puts("\n");
    uart::puts("Cycles per sample: ");
    uart::print_uint(per_sample_schur2);
    uart::puts("\n\n");

    float rt_ratio_schur2 = 3500.0f / static_cast<float>(per_sample_schur2);
    uart::puts("RT ratio @168MHz:  ");
    uart::print_float(rt_ratio_schur2, 1);
    uart::puts("x\n\n");

    // Comparison (Ebers-Moll preserving implementations)
    uart::puts("=== Ebers-Moll Preserving Implementations ===\n");
    uart::puts("WaveShaperSchur:      ");
    uart::print_uint(per_sample);
    uart::puts(" cycles (baseline)\n");
    uart::puts("WaveShaperSchurMo:    ");
    uart::print_uint(per_sample_mo);
    uart::puts(" cycles (mo::pow2)\n");
    uart::puts("WaveShaperSchurUltra: ");
    uart::print_uint(per_sample_ultra);
    uart::puts(" cycles (BC delayed)\n");
    uart::puts("WaveShaperSchurTable: ");
    uart::print_uint(per_sample_table);
    uart::puts(" cycles (Meijer LUT)\n");
    uart::puts("SchurLambertW:        ");
    uart::print_uint(per_sample_lambertw);
    uart::puts(" cycles (omega_fast2)\n");
    uart::puts("SchurOmega3:          ");
    uart::print_uint(per_sample_omega3);
    uart::puts(" cycles (omega3 only)\n");
    uart::puts("Decoupled:            ");
    uart::print_uint(per_sample_decoupled);
    uart::puts(" cycles (matrix-free)\n");
    uart::puts("HybridAdaptive:       ");
    uart::print_uint(per_sample_hybrid);
    uart::puts(" cycles (BJT state)\n");
    uart::puts("Schraudolph:          ");
    uart::print_uint(per_sample_schraud);
    uart::puts(" cycles (bitwise exp)\n");
    uart::puts("TwoIter:              ");
    uart::print_uint(per_sample_twoiter);
    uart::puts(" cycles (2 Newton)\n");
    uart::puts("OneIter:              ");
    uart::print_uint(per_sample_oneiter);
    uart::puts(" cycles (1 Newton)\n");
    uart::puts("3Var:                 ");
    uart::print_uint(per_sample_3var);
    uart::puts(" cycles (3x3 Newton)\n");
    uart::puts("3VarOpt:              ");
    uart::print_uint(per_sample_3varopt);
    uart::puts(" cycles (exp mult)\n\n");

    uart::puts("=== WDF Implementations ===\n");
    uart::puts("WaveShaperWDF:        ");
    uart::print_uint(per_sample_wdf);
    uart::puts(" cycles (DiodePair)\n");
    uart::puts("WaveShaperWDFFull:    ");
    uart::print_uint(per_sample_wdf_full);
    uart::puts(" cycles (Lambert W)\n\n");

    uart::puts("=== Non-Ebers-Moll (Reference) ===\n");
    uart::puts("WaveShaperFast:       ");
    uart::print_uint(per_sample_fast);
    uart::puts(" cycles (Forward Euler)\n");
    uart::puts("SquareShaper:         ");
    uart::print_uint(per_sample_sq);
    uart::puts(" cycles (PNP approx)\n");
    uart::puts("Geometric:            ");
    uart::print_uint(per_sample_geo);
    uart::puts(" cycles (hyperbolic)\n\n");

    uart::puts("=== Speedup vs Baseline ===\n");
    uart::puts("SchurUltra:  ");
    uart::print_float(static_cast<float>(per_sample) / static_cast<float>(per_sample_ultra), 2);
    uart::puts("x\n");
    uart::puts("SchurTable:  ");
    uart::print_float(static_cast<float>(per_sample) / static_cast<float>(per_sample_table), 2);
    uart::puts("x\n");
    uart::puts("WDF:         ");
    uart::print_float(static_cast<float>(per_sample) / static_cast<float>(per_sample_wdf), 2);
    uart::puts("x\n");
    uart::puts("WDFFull:     ");
    uart::print_float(static_cast<float>(per_sample) / static_cast<float>(per_sample_wdf_full), 2);
    uart::puts("x\n");
    uart::puts("LambertW:    ");
    uart::print_float(static_cast<float>(per_sample) / static_cast<float>(per_sample_lambertw), 2);
    uart::puts("x\n");
    uart::puts("Omega3:      ");
    uart::print_float(static_cast<float>(per_sample) / static_cast<float>(per_sample_omega3), 2);
    uart::puts("x\n");
    uart::puts("Decoupled:   ");
    uart::print_float(static_cast<float>(per_sample) / static_cast<float>(per_sample_decoupled), 2);
    uart::puts("x\n");
    uart::puts("Hybrid:      ");
    uart::print_float(static_cast<float>(per_sample) / static_cast<float>(per_sample_hybrid), 2);
    uart::puts("x\n");
    uart::puts("3Var:        ");
    uart::print_float(static_cast<float>(per_sample) / static_cast<float>(per_sample_3var), 2);
    uart::puts("x\n");
    uart::puts("3VarOpt:     ");
    uart::print_float(static_cast<float>(per_sample) / static_cast<float>(per_sample_3varopt), 2);
    uart::puts("x\n");
    uart::puts("Geometric:   ");
    uart::print_float(static_cast<float>(per_sample) / static_cast<float>(per_sample_geo), 2);
    uart::puts("x\n");
    uart::puts("\n");

    // === Micro-benchmark: fast_exp vs omega4 ===
    uart::puts("=== Micro-benchmark: exp approximations ===\n");

    // fast_exp benchmark
    float x_test = 0.5f;
    float sum_exp = 0.0f;
    dwt::reset();
    start = dwt::cycles();
    for (int i = 0; i < ITERATIONS; ++i) {
        sum_exp += tb303::fast_exp(x_test);
        x_test += 0.001f;
        if (x_test > 10.0f) x_test = -10.0f;
    }
    end = dwt::cycles();
    sink = sum_exp;
    uint32_t cycles_fast_exp = (end - start) / ITERATIONS;
    uart::puts("fast_exp:     ");
    uart::print_uint(cycles_fast_exp);
    uart::puts(" cycles/call\n");

    // fast_tanh benchmark
    x_test = 0.5f;
    float sum_tanh = 0.0f;
    dwt::reset();
    start = dwt::cycles();
    for (int i = 0; i < ITERATIONS; ++i) {
        sum_tanh += tb303::fast_tanh(x_test);
        x_test += 0.001f;
        if (x_test > 3.0f) x_test = -3.0f;
    }
    end = dwt::cycles();
    sink = sum_tanh;
    uint32_t cycles_fast_tanh = (end - start) / ITERATIONS;
    uart::puts("fast_tanh:    ");
    uart::print_uint(cycles_fast_tanh);
    uart::puts(" cycles/call\n");

    // omega4 benchmark (Wright Omega)
    x_test = 0.5f;
    float sum_omega = 0.0f;
    dwt::reset();
    start = dwt::cycles();
    for (int i = 0; i < ITERATIONS; ++i) {
        sum_omega += tb303::omega4(x_test);
        x_test += 0.001f;
        if (x_test > 10.0f) x_test = -10.0f;
    }
    end = dwt::cycles();
    sink = sum_omega;
    uint32_t cycles_omega4 = (end - start) / ITERATIONS;
    uart::puts("omega4:       ");
    uart::print_uint(cycles_omega4);
    uart::puts(" cycles/call\n");

    // DiodeLambertW.current benchmark
    tb303::DiodeLambertW diode_lw;
    diode_lw.init(tb303::I_S, tb303::V_T, 1000.0f);  // 1kΩ直列抵抗
    float v_test = 0.3f;
    float sum_diode = 0.0f;
    dwt::reset();
    start = dwt::cycles();
    for (int i = 0; i < ITERATIONS; ++i) {
        sum_diode += diode_lw.current(v_test);
        v_test += 0.0001f;
        if (v_test > 1.0f) v_test = -0.5f;
    }
    end = dwt::cycles();
    sink = sum_diode;
    uint32_t cycles_diode_lw = (end - start) / ITERATIONS;
    uart::puts("DiodeLambertW:");
    uart::print_uint(cycles_diode_lw);
    uart::puts(" cycles/call\n");

    // diode_iv (Newton用) benchmark
    float i_out, g_out;
    v_test = 0.3f;
    float sum_diode_iv = 0.0f;
    dwt::reset();
    start = dwt::cycles();
    for (int i = 0; i < ITERATIONS; ++i) {
        tb303::diode_iv(v_test, i_out, g_out);
        sum_diode_iv += i_out + g_out;
        v_test += 0.0001f;
        if (v_test > 1.0f) v_test = -0.5f;
    }
    end = dwt::cycles();
    sink = sum_diode_iv;
    uint32_t cycles_diode_iv = (end - start) / ITERATIONS;
    uart::puts("diode_iv:     ");
    uart::print_uint(cycles_diode_iv);
    uart::puts(" cycles/call\n");

    // omega_fast benchmark (Fukushima-style, no Newton)
    x_test = 0.5f;
    float sum_omega_fast = 0.0f;
    dwt::reset();
    start = dwt::cycles();
    for (int i = 0; i < ITERATIONS; ++i) {
        sum_omega_fast += tb303::omega_fast(x_test);
        x_test += 0.001f;
        if (x_test > 10.0f) x_test = -10.0f;
    }
    end = dwt::cycles();
    sink = sum_omega_fast;
    uint32_t cycles_omega_fast = (end - start) / ITERATIONS;
    uart::puts("omega_fast:   ");
    uart::print_uint(cycles_omega_fast);
    uart::puts(" cycles/call\n");

    // omega_fast2 benchmark
    x_test = 0.5f;
    float sum_omega_fast2 = 0.0f;
    dwt::reset();
    start = dwt::cycles();
    for (int i = 0; i < ITERATIONS; ++i) {
        sum_omega_fast2 += tb303::omega_fast2(x_test);
        x_test += 0.001f;
        if (x_test > 10.0f) x_test = -10.0f;
    }
    end = dwt::cycles();
    sink = sum_omega_fast2;
    uint32_t cycles_omega_fast2 = (end - start) / ITERATIONS;
    uart::puts("omega_fast2:  ");
    uart::print_uint(cycles_omega_fast2);
    uart::puts(" cycles/call\n");

    // omega3 benchmark (no Newton correction)
    x_test = 0.5f;
    float sum_omega3 = 0.0f;
    dwt::reset();
    start = dwt::cycles();
    for (int i = 0; i < ITERATIONS; ++i) {
        sum_omega3 += tb303::omega3(x_test);
        x_test += 0.001f;
        if (x_test > 10.0f) x_test = -10.0f;
    }
    end = dwt::cycles();
    sink = sum_omega3;
    uint32_t cycles_omega3_micro = (end - start) / ITERATIONS;
    uart::puts("omega3:       ");
    uart::print_uint(cycles_omega3_micro);
    uart::puts(" cycles/call\n\n");

    // =========================================================================
    // 精度測定: WaveShaperSchur（ベースライン）との比較
    // =========================================================================
    uart::puts("=== ACCURACY TEST (vs Schur baseline) ===\n");
    uart::puts("Samples: 1200 (1 cycle @ 40Hz)\n\n");

    {
        constexpr int ACC_SAMPLES = 1200;  // 40Hz @ 48kHz = 1200 samples/cycle

        // 全実装を同時に処理して比較
        tb303::WaveShaperSchur acc_ref;
        tb303::WaveShaperSchurLambertW acc_lw;
        tb303::WaveShaperSchurOmega3 acc_o3;
        tb303::WaveShaperDecoupled acc_dec;
        tb303::WaveShaperHybridAdaptive acc_hybrid;
        tb303::WaveShaperFast acc_fast;
        tb303::WaveShaperSchraudolph acc_schrau;
        tb303::WaveShaperPWL acc_pwl;
        tb303::WaveShaperTanh acc_tanh;
        tb303::WaveShaperVCCS acc_vccs;
        tb303::WaveShaperOneIter acc_one;
        tb303::WaveShaperTwoIter acc_two;
        tb303::WaveShaperPade acc_pade;
        tb303::WaveShaperLUT acc_lut;

        acc_ref.setSampleRate(SAMPLE_RATE); acc_ref.reset();
        acc_lw.setSampleRate(SAMPLE_RATE); acc_lw.reset();
        acc_o3.setSampleRate(SAMPLE_RATE); acc_o3.reset();
        acc_dec.setSampleRate(SAMPLE_RATE); acc_dec.reset();
        acc_hybrid.setSampleRate(SAMPLE_RATE); acc_hybrid.reset();
        acc_fast.setSampleRate(SAMPLE_RATE); acc_fast.reset();
        acc_schrau.setSampleRate(SAMPLE_RATE); acc_schrau.reset();
        acc_pwl.setSampleRate(SAMPLE_RATE); acc_pwl.reset();
        acc_tanh.setSampleRate(SAMPLE_RATE); acc_tanh.reset();
        acc_vccs.setSampleRate(SAMPLE_RATE); acc_vccs.reset();
        acc_one.setSampleRate(SAMPLE_RATE); acc_one.reset();
        acc_two.setSampleRate(SAMPLE_RATE); acc_two.reset();
        acc_pade.setSampleRate(SAMPLE_RATE); acc_pade.reset();
        acc_lut.setSampleRate(SAMPLE_RATE); acc_lut.reset();

        tb303::WaveShaperSchurFast acc_fast2;
        tb303::WaveShaperPredict acc_predict;
        tb303::WaveShaperMinimal acc_minimal;
        tb303::WaveShaper3Var acc_3var;
        tb303::WaveShaper3VarOpt acc_3varopt;
        tb303::WaveShaperGeometric acc_geo;
        tb303::WaveShaperOptimized acc_opt;
        tb303::WaveShaper3Var2Iter acc_3var2;
        tb303::WaveShaperSchur2Iter acc_schur2;
        acc_fast2.setSampleRate(SAMPLE_RATE); acc_fast2.reset();
        acc_predict.setSampleRate(SAMPLE_RATE); acc_predict.reset();
        acc_minimal.setSampleRate(SAMPLE_RATE); acc_minimal.reset();
        acc_3var.setSampleRate(SAMPLE_RATE); acc_3var.reset();
        acc_3varopt.setSampleRate(SAMPLE_RATE); acc_3varopt.reset();
        acc_geo.setSampleRate(SAMPLE_RATE); acc_geo.reset();
        acc_opt.setSampleRate(SAMPLE_RATE); acc_opt.reset();
        acc_3var2.setSampleRate(SAMPLE_RATE); acc_3var2.reset();
        acc_schur2.setSampleRate(SAMPLE_RATE); acc_schur2.reset();

        float sum_sq_lw = 0.0f, max_lw = 0.0f;
        float sum_sq_o3 = 0.0f, max_o3 = 0.0f;
        float sum_sq_dec = 0.0f, max_dec = 0.0f;
        float sum_sq_hybrid = 0.0f, max_hybrid = 0.0f;
        float sum_sq_fast = 0.0f, max_fast = 0.0f;
        float sum_sq_schrau = 0.0f, max_schrau = 0.0f;
        float sum_sq_pwl = 0.0f, max_pwl = 0.0f;
        float sum_sq_tanh = 0.0f, max_tanh = 0.0f;
        float sum_sq_vccs = 0.0f, max_vccs = 0.0f;
        float sum_sq_one = 0.0f, max_one = 0.0f;
        float sum_sq_two = 0.0f, max_two = 0.0f;
        float sum_sq_pade = 0.0f, max_pade = 0.0f;
        float sum_sq_lut = 0.0f, max_lut = 0.0f;
        float sum_sq_fast2 = 0.0f, max_fast2 = 0.0f;
        float sum_sq_predict = 0.0f, max_predict = 0.0f;
        float sum_sq_minimal = 0.0f, max_minimal = 0.0f;
        float sum_sq_3var = 0.0f, max_3var = 0.0f;
        float sum_sq_3varopt = 0.0f, max_3varopt = 0.0f;
        float sum_sq_geo = 0.0f, max_geo = 0.0f;
        float sum_sq_opt = 0.0f, max_opt = 0.0f;
        float sum_sq_3var2 = 0.0f, max_3var2 = 0.0f;
        float sum_sq_schur2 = 0.0f, max_schur2 = 0.0f;

        float acc_v_in = 12.0f;
        for (int i = 0; i < ACC_SAMPLES; ++i) {
            float out_ref = acc_ref.process(acc_v_in);
            float out_lw = acc_lw.process(acc_v_in);
            float out_o3 = acc_o3.process(acc_v_in);
            float out_dec = acc_dec.process(acc_v_in);
            float out_hybrid = acc_hybrid.process(acc_v_in);
            float out_fast = acc_fast.process(acc_v_in);
            float out_schrau = acc_schrau.process(acc_v_in);
            float out_pwl = acc_pwl.process(acc_v_in);
            float out_tanh = acc_tanh.process(acc_v_in);
            float out_vccs = acc_vccs.process(acc_v_in);
            float out_one = acc_one.process(acc_v_in);
            float out_two = acc_two.process(acc_v_in);
            float out_pade = acc_pade.process(acc_v_in);
            float out_lut = acc_lut.process(acc_v_in);
            float out_fast2 = acc_fast2.process(acc_v_in);
            float out_predict = acc_predict.process(acc_v_in);
            float out_minimal = acc_minimal.process(acc_v_in);
            float out_3var = acc_3var.process(acc_v_in);
            float out_3varopt = acc_3varopt.process(acc_v_in);
            float out_geo = acc_geo.process(acc_v_in);
            float out_opt = acc_opt.process(acc_v_in);
            float out_3var2 = acc_3var2.process(acc_v_in);
            float out_schur2 = acc_schur2.process(acc_v_in);

            float err_lw = out_lw - out_ref;
            float err_o3 = out_o3 - out_ref;
            float err_dec = out_dec - out_ref;
            float err_hybrid = out_hybrid - out_ref;
            float err_fast = out_fast - out_ref;
            float err_schrau = out_schrau - out_ref;
            float err_pwl = out_pwl - out_ref;
            float err_tanh = out_tanh - out_ref;
            float err_vccs = out_vccs - out_ref;
            float err_one = out_one - out_ref;
            float err_two = out_two - out_ref;
            float err_pade = out_pade - out_ref;
            float err_lut = out_lut - out_ref;
            float err_fast2 = out_fast2 - out_ref;
            float err_predict = out_predict - out_ref;
            float err_minimal = out_minimal - out_ref;
            float err_3var = out_3var - out_ref;
            float err_3varopt = out_3varopt - out_ref;
            float err_geo = out_geo - out_ref;
            float err_opt = out_opt - out_ref;
            float err_3var2 = out_3var2 - out_ref;
            float err_schur2 = out_schur2 - out_ref;

            sum_sq_lw += err_lw * err_lw;
            sum_sq_o3 += err_o3 * err_o3;
            sum_sq_dec += err_dec * err_dec;
            sum_sq_hybrid += err_hybrid * err_hybrid;
            sum_sq_fast += err_fast * err_fast;
            sum_sq_schrau += err_schrau * err_schrau;
            sum_sq_pwl += err_pwl * err_pwl;
            sum_sq_tanh += err_tanh * err_tanh;
            sum_sq_vccs += err_vccs * err_vccs;
            sum_sq_one += err_one * err_one;
            sum_sq_two += err_two * err_two;
            sum_sq_pade += err_pade * err_pade;
            sum_sq_lut += err_lut * err_lut;
            sum_sq_fast2 += err_fast2 * err_fast2;
            sum_sq_predict += err_predict * err_predict;
            sum_sq_minimal += err_minimal * err_minimal;
            sum_sq_3var += err_3var * err_3var;
            sum_sq_3varopt += err_3varopt * err_3varopt;
            sum_sq_geo += err_geo * err_geo;
            sum_sq_opt += err_opt * err_opt;
            sum_sq_3var2 += err_3var2 * err_3var2;
            sum_sq_schur2 += err_schur2 * err_schur2;

            if (std::abs(err_lw) > max_lw) max_lw = std::abs(err_lw);
            if (std::abs(err_o3) > max_o3) max_o3 = std::abs(err_o3);
            if (std::abs(err_dec) > max_dec) max_dec = std::abs(err_dec);
            if (std::abs(err_hybrid) > max_hybrid) max_hybrid = std::abs(err_hybrid);
            if (std::abs(err_fast) > max_fast) max_fast = std::abs(err_fast);
            if (std::abs(err_schrau) > max_schrau) max_schrau = std::abs(err_schrau);
            if (std::abs(err_pwl) > max_pwl) max_pwl = std::abs(err_pwl);
            if (std::abs(err_tanh) > max_tanh) max_tanh = std::abs(err_tanh);
            if (std::abs(err_vccs) > max_vccs) max_vccs = std::abs(err_vccs);
            if (std::abs(err_one) > max_one) max_one = std::abs(err_one);
            if (std::abs(err_two) > max_two) max_two = std::abs(err_two);
            if (std::abs(err_pade) > max_pade) max_pade = std::abs(err_pade);
            if (std::abs(err_lut) > max_lut) max_lut = std::abs(err_lut);
            if (std::abs(err_fast2) > max_fast2) max_fast2 = std::abs(err_fast2);
            if (std::abs(err_predict) > max_predict) max_predict = std::abs(err_predict);
            if (std::abs(err_minimal) > max_minimal) max_minimal = std::abs(err_minimal);
            if (std::abs(err_3var) > max_3var) max_3var = std::abs(err_3var);
            if (std::abs(err_3varopt) > max_3varopt) max_3varopt = std::abs(err_3varopt);
            if (std::abs(err_geo) > max_geo) max_geo = std::abs(err_geo);
            if (std::abs(err_opt) > max_opt) max_opt = std::abs(err_opt);
            if (std::abs(err_3var2) > max_3var2) max_3var2 = std::abs(err_3var2);
            if (std::abs(err_schur2) > max_schur2) max_schur2 = std::abs(err_schur2);

            acc_v_in -= 0.00542f;
            if (acc_v_in < 5.5f) acc_v_in = 12.0f;
        }

        auto print_result = [](const char* name, float sum_sq, float max_err) {
            constexpr int samples = 1200;
            float rms = std::sqrt(sum_sq / samples) * 1000.0f;
            max_err *= 1000.0f;
            uart::puts(name);
            uart::puts(": RMS=");
            uart::print_float(rms, 1);
            uart::puts("mV, Max=");
            uart::print_float(max_err, 1);
            uart::puts("mV\n");
        };

        uart::puts("Schur (baseline)   : RMS=0.0mV, Max=0.0mV\n");
        print_result("SchurLambertW      ", sum_sq_lw, max_lw);
        print_result("SchurOmega3        ", sum_sq_o3, max_o3);
        print_result("Decoupled          ", sum_sq_dec, max_dec);
        print_result("HybridAdaptive     ", sum_sq_hybrid, max_hybrid);
        print_result("Fast (Euler)       ", sum_sq_fast, max_fast);
        print_result("Schraudolph        ", sum_sq_schrau, max_schrau);
        print_result("PWL                ", sum_sq_pwl, max_pwl);
        print_result("Tanh               ", sum_sq_tanh, max_tanh);
        print_result("VCCS               ", sum_sq_vccs, max_vccs);
        print_result("OneIter            ", sum_sq_one, max_one);
        print_result("TwoIter            ", sum_sq_two, max_two);
        print_result("Pade [2,2]         ", sum_sq_pade, max_pade);
        print_result("LUT interp         ", sum_sq_lut, max_lut);
        print_result("SchurFast (BC skip)", sum_sq_fast2, max_fast2);
        print_result("Predict            ", sum_sq_predict, max_predict);
        print_result("Minimal            ", sum_sq_minimal, max_minimal);
        print_result("3Var (v_cap elim)  ", sum_sq_3var, max_3var);
        print_result("3VarOpt (exp mult) ", sum_sq_3varopt, max_3varopt);
        print_result("Geometric (hyper)  ", sum_sq_geo, max_geo);
        print_result("Optimized (pad_exp)", sum_sq_opt, max_opt);
        print_result("3Var2Iter (2 Newton)", sum_sq_3var2, max_3var2);
        print_result("Schur2Iter (2 Newton)", sum_sq_schur2, max_schur2);
    }

    uart::puts("\n");
    uart::puts("=== BENCHMARK COMPLETE ===\n");

    // Halt
    while (true) {
        asm volatile("wfi");
    }
#endif

    return 0;
}
