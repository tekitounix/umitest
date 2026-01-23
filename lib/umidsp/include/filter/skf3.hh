#pragma once

#include <cmath>
#include <numbers>
#include <type_traits>

#include "common.hh"

namespace detail {

struct Skf3Coeffs {
    float lpf_a0 = 0.0f;
    float a1 = 0.0f;
    float a2 = 0.0f;
    float a3 = 0.0f;
    float a4 = 0.0f;
    float a5 = 0.0f;
    float k = 0.0f;
};

struct Skf3State {
    float z1 = 0.0f;
    float s1 = 0.0f;
    float s2 = 0.0f;
};

constexpr float calc_lpf_a0(float wc) {
    return (2.92778f - 7.35606f * wc * (0.753747f - wc)) * wc;
}

inline Skf3Coeffs make_skf3_coeffs(float wc_lpf, float wc_skf, float resonance) {
    if (wc_lpf < 1.0e-6f)
        wc_lpf = 1.0e-6f;
    if (wc_lpf > 0.5f)
        wc_lpf = 0.5f;
    if (wc_skf < 1.0e-6f)
        wc_skf = 1.0e-6f;
    if (wc_skf > 0.5f)
        wc_skf = 0.5f;
    if (resonance < 0.0f)
        resonance = 0.0f;
    if (resonance > 1.0f)
        resonance = 1.0f;

    const float g = std::tan(std::numbers::pi_v<float> * wc_skf);
    const float g_plus1 = 1.0f + g;
    const float k = 2.0f * resonance;
    const float a0 = 1.0f / ((g_plus1 * g_plus1) - (g * k));

    Skf3Coeffs c{};
    c.lpf_a0 = calc_lpf_a0(wc_lpf);
    c.k = k;
    c.a1 = k * a0;
    c.a2 = g_plus1 * a0;
    c.a3 = g * c.a2;
    c.a4 = g * a0;
    c.a5 = g * c.a4;
    return c;
}

inline float process_lp(float& z1, float a0, float x) {
    const float v = (x - z1) * a0;
    const float y = v + z1;
    z1 = v + y;
    return y;
}

inline float process_skf(Skf3State& s, const Skf3Coeffs& c, float x) {
    const float v1 = c.a1 * s.s2 + c.a2 * s.s1 + c.a3 * x;
    const float v2 = c.a2 * s.s2 + c.a4 * s.s1 + c.a5 * x;
    s.s1 = 2.0f * (v1 - c.k * v2) - s.s1;
    s.s2 = 2.0f * v2 - s.s2;
    return v2;
}

} // namespace detail

using Skf3InlinePolicy = InlineCoeffs<detail::Skf3Coeffs, detail::make_skf3_coeffs>;
using Skf3OwnPolicy = OwnCoeffs<detail::Skf3Coeffs, detail::make_skf3_coeffs>;
using Skf3SharedPolicy = SharedCoeffs<detail::Skf3Coeffs>;

template <typename CoeffSource>
class Skf3 {
  public:
    Skf3()
        requires(!std::is_same_v<CoeffSource, Skf3SharedPolicy>)
    = default;
    explicit Skf3(const detail::Skf3Coeffs& shared)
        requires(std::is_same_v<CoeffSource, Skf3SharedPolicy>)
        : c(shared) {}

    void reset() { s = {}; }

    void set_params(float wc_lpf, float wc_skf, float resonance)
        requires requires(CoeffSource& c, float w1, float w2, float r) { c.set_params(w1, w2, r); }
    {
        c.set_params(wc_lpf, wc_skf, resonance);
    }

    float process(float x, float wc_lpf, float wc_skf, float resonance) {
        const auto& coeffs_ref = c.coeffs(wc_lpf, wc_skf, resonance);
        const float lp = detail::process_lp(s.z1, coeffs_ref.lpf_a0, x);
        return detail::process_skf(s, coeffs_ref, lp);
    }

    float process(float x)
        requires(!CoeffSource::needs_params)
    {
        const auto& coeffs_ref = c.coeffs();
        const float lp = detail::process_lp(s.z1, coeffs_ref.lpf_a0, x);
        return detail::process_skf(s, coeffs_ref, lp);
    }

  private:
    detail::Skf3State s{};
    CoeffSource c{};
};

using Skf3Inline = Skf3<Skf3InlinePolicy>;
using Skf3Async = Skf3<Skf3OwnPolicy>;
using Skf3Shared = Skf3<Skf3SharedPolicy>;
