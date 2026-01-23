#pragma once

#include <cmath>
#include <numbers>
#include <type_traits>

#include "common.hh"

namespace detail {

struct SkfCoeffs {
    float a1 = 0.0f;
    float a2 = 0.0f;
    float a3 = 0.0f;
    float a4 = 0.0f;
    float a5 = 0.0f;
    float k = 0.0f;
};

struct SkfState {
    float s1 = 0.0f;
    float s2 = 0.0f;
};

inline SkfCoeffs make_skf_coeffs(float wc, float resonance) {
    if (wc < 1.0e-6f)
        wc = 1.0e-6f;
    if (wc > 0.5f)
        wc = 0.5f;
    if (resonance < 0.0f)
        resonance = 0.0f;
    if (resonance > 1.0f)
        resonance = 1.0f;

    const float g = std::tan(std::numbers::pi_v<float> * wc);
    const float g_plus1 = 1.0f + g;
    const float k = 2.0f * resonance;
    const float a0 = 1.0f / ((g_plus1 * g_plus1) - (g * k));

    SkfCoeffs c{};
    c.k = k;
    c.a1 = k * a0;
    c.a2 = g_plus1 * a0;
    c.a3 = g * c.a2;
    c.a4 = g * a0;
    c.a5 = g * c.a4;
    return c;
}

inline float process_skf(SkfState& s, const SkfCoeffs& c, float x) {
    const float v1 = c.a1 * s.s2 + c.a2 * s.s1 + c.a3 * x;
    const float v2 = c.a2 * s.s2 + c.a4 * s.s1 + c.a5 * x;
    s.s1 = 2.0f * (v1 - c.k * v2) - s.s1;
    s.s2 = 2.0f * v2 - s.s2;
    return v2;
}

} // namespace detail

using SkfInlinePolicy = InlineCoeffs<detail::SkfCoeffs, detail::make_skf_coeffs>;
using SkfOwnPolicy = OwnCoeffs<detail::SkfCoeffs, detail::make_skf_coeffs>;
using SkfSharedPolicy = SharedCoeffs<detail::SkfCoeffs>;

template <typename CoeffSource>
class Skf {
  public:
    Skf()
        requires(!std::is_same_v<CoeffSource, SkfSharedPolicy>)
    = default;
    explicit Skf(const detail::SkfCoeffs& shared)
        requires(std::is_same_v<CoeffSource, SkfSharedPolicy>)
        : c(shared) {}

    void reset() { s = {}; }

    void set_params(float wc, float resonance)
        requires requires(CoeffSource& c, float w, float r) { c.set_params(w, r); }
    {
        c.set_params(wc, resonance);
    }

    float process(float x, float wc, float resonance) {
        const auto& coeffs_ref = c.coeffs(wc, resonance);
        return detail::process_skf(s, coeffs_ref, x);
    }

    float process(float x)
        requires(!CoeffSource::needs_params)
    {
        const auto& coeffs_ref = c.coeffs();
        return detail::process_skf(s, coeffs_ref, x);
    }

  private:
    detail::SkfState s{};
    CoeffSource c{};
};

using SkfInline = Skf<SkfInlinePolicy>;
using SkfAsync = Skf<SkfOwnPolicy>;
using SkfShared = Skf<SkfSharedPolicy>;
