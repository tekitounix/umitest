#pragma once

// Diode Ladder Filter with D-Factorization Optimization
// Based on Karrikuh (Dominique Wurtz) implementation
// D-Factor optimization reduces s0 calculation from 19 to 18 instructions on Cortex-M4

#include <numbers>

namespace detail {

// tan(pi * wc) approximation for wc in [0, 0.5)
// Based on rational approximation, accurate for audio cutoff range
constexpr float tan_approx(float x) {
    // tan(x) ≈ x * (1 + x²/3 + 2x⁴/15) for small x
    // For pi*wc range, use optimized polynomial
    const float x2 = x * x;
    return x * (1.0f + x2 * (0.333333f + x2 * 0.1333f));
}

// tanh approximation using rational function
// tanh(x) ≈ x * (27 + x²) / (27 + 9x²) for |x| < 3
// Fast and smooth, good for saturation
constexpr float tanh_approx(float x) {
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

struct DLadderCoeffs {
    float a = 0.0f;
    float a2 = 0.0f;
    float b = 0.0f;
    float d = 0.0f;
    float c = 0.0f;
    float g0 = 0.0f;
    float ainv = 0.0f;
};

constexpr DLadderCoeffs make_dladder_coeffs(float wc) {
    const float a = tan_approx(std::numbers::pi_v<float> * wc) * std::numbers::inv_sqrt2_v<float>;
    const float a2 = a * a;
    const float b = (2.0f * a) + 1.0f;
    const float b2 = b * b;

    // D-Factorization: D = b² - 2a²
    const float d = b2 - (2.0f * a2);

    // c = 1/(D² - 2a⁴) = 1/(2a⁴ - 4a²b² + b⁴)
    const float a4 = a2 * a2;
    const float c = 1.0f / ((d * d) - (2.0f * a4));

    // g0 = 2a⁴·c (for feedforward gain)
    const float g0 = (2.0f * a4) * c;

    const float ainv = 1.0f / a;

    return {a, a2, b, d, c, g0, ainv};
}

}  // namespace detail

// Coefficient policy: Inline (compute every sample)
struct DLadderInline {
    static constexpr bool needs_params = true;

    constexpr const detail::DLadderCoeffs& coeffs(float wc) {
        temp = detail::make_dladder_coeffs(wc);
        return temp;
    }

  private:
    detail::DLadderCoeffs temp{};
};

// Coefficient policy: Own (precomputed, set via set_params)
struct DLadderOwn {
    static constexpr bool needs_params = false;

    constexpr void set_params(float wc) {
        co = detail::make_dladder_coeffs(wc);
    }

    [[nodiscard]] constexpr const detail::DLadderCoeffs& coeffs(float /*wc*/) const {
        return co;
    }

  private:
    detail::DLadderCoeffs co{};
};

// Coefficient policy: Shared (external coefficients)
struct DLadderShared {
    static constexpr bool needs_params = false;

    explicit constexpr DLadderShared(const detail::DLadderCoeffs& coeffs) : ptr(&coeffs) {}

    [[nodiscard]] constexpr const detail::DLadderCoeffs& coeffs(float /*wc*/) const {
        return *ptr;
    }

  private:
    const detail::DLadderCoeffs* ptr = nullptr;
};

template <typename CoeffSource>
class DLadder {
  public:
    constexpr DLadder() = default;

    explicit constexpr DLadder(const detail::DLadderCoeffs& shared)
        : coeff_src(shared) {}

    constexpr void reset() {
        z[0] = z[1] = z[2] = z[3] = 0.0f;
    }

    constexpr void set_params(float wc)
        requires (!CoeffSource::needs_params)
    {
        coeff_src.set_params(wc);
    }

    constexpr void set_wc(float wc)
        requires (!CoeffSource::needs_params)
    {
        set_params(wc);
    }

    constexpr void set_resonance(float q) {
        k = q * 16.0f;
        comp = q * 2.2f;
    }

    constexpr float process(float x, float wc) {
        const auto& co = coeff_src.coeffs(wc);

        // D-Factor optimized s0 calculation
        const float term1 = (co.a * z[0]) + (co.b * (z[1] - z[3]));
        const float term2 = (co.a * z[2]) + (co.b * z[3]);
        const float s0 = co.c * ((co.a2 * term1) + (co.d * term2));

        // Feedback and nonlinearity
        const float y0_raw = x - ((k * (co.g0 * x + s0)) / (1.0f + (k * co.g0)));
        const float y0 = detail::tanh_approx(y0_raw);

        // Filter cascade (backward Euler)
        const float y4 = (co.g0 * y0) + s0;
        const float y3 = ((co.b * y4) - z[3]) * co.ainv;
        const float y2 = ((co.b * y3) - (co.a * y4) - z[2]) * co.ainv;
        const float y1 = ((co.b * y2) - (co.a * y3) - z[1]) * co.ainv;

        // State update
        const float a2x = 2.0f * co.a;
        z[0] += (2.0f * a2x) * ((y0 - y1) + y2);
        z[1] += a2x * ((y1 - (2.0f * y2)) + y3);
        z[2] += a2x * ((y2 - (2.0f * y3)) + y4);
        z[3] += a2x * (y3 - (2.0f * y4));

        // Output with resonance compensation
        return y4 + (y4 * comp);
    }

    constexpr float process(float x)
        requires (!CoeffSource::needs_params)
    {
        return process(x, 0.0f);
    }

  private:
    float z[4] = {};
    float k = 0.0f;
    float comp = 0.0f;
    CoeffSource coeff_src{};
};

using DiodeLadder = DLadder<DLadderInline>;
using DiodeLadderAsync = DLadder<DLadderOwn>;
using DiodeLadderSharedCoeffs = DLadder<DLadderShared>;
