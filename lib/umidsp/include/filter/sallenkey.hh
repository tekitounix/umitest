#pragma once

#include <cmath>
#include <numbers>

#include "common.hh"

namespace detail {

struct SallenKeyCoeffs {
    float k = 0.0f;
    float a1 = 0.0f;
    float a2 = 0.0f;
    float a3 = 0.0f;
    float a4 = 0.0f;
    float a5 = 0.0f;
};

struct SallenKeyState {
    float ic1eq = 0.0f;
    float ic2eq = 0.0f;
};

inline SallenKeyCoeffs make_sallenkey_coeffs(float wc, float resonance) {
    if (wc < 1.0e-6f)
        wc = 1.0e-6f;
    if (wc > 0.5f)
        wc = 0.5f;
    if (resonance < 0.0f)
        resonance = 0.0f;
    if (resonance > 1.0f)
        resonance = 1.0f;

    SallenKeyCoeffs c{};
    c.k = 1.9f * resonance; // k = 0..2 (approx)

    const float g = std::tan(std::numbers::pi_v<float> * wc);
    const float g_plus1 = 1.0f + g;
    const float a0 = 1.0f / ((g_plus1 * g_plus1) - (g * c.k));

    c.a1 = c.k * a0;
    c.a2 = g_plus1 * a0;
    c.a3 = g * c.a2;
    c.a4 = g * a0;
    c.a5 = g * c.a4;
    return c;
}

inline float process_sallenkey(SallenKeyState& s, const SallenKeyCoeffs& c, float x) {
    const float v1 = c.a1 * s.ic2eq + c.a2 * s.ic1eq + c.a3 * x;
    const float v2 = c.a2 * s.ic2eq + c.a4 * s.ic1eq + c.a5 * x;
    s.ic1eq = 2.0f * (v1 - c.k * v2) - s.ic1eq;
    s.ic2eq = 2.0f * v2 - s.ic2eq;
    return v2;
}

} // namespace detail

using SallenKeyInlinePolicy = InlineCoeffs<detail::SallenKeyCoeffs, detail::make_sallenkey_coeffs>;
using SallenKeyOwnPolicy = OwnCoeffs<detail::SallenKeyCoeffs, detail::make_sallenkey_coeffs>;
using SallenKeySharedPolicy = SharedCoeffs<detail::SallenKeyCoeffs>;

template <typename CoeffSource>
class SallenKey {
  public:
    SallenKey()
        requires(!std::is_same_v<CoeffSource, SallenKeySharedPolicy>)
    = default;
    explicit SallenKey(const detail::SallenKeyCoeffs& shared)
        requires(std::is_same_v<CoeffSource, SallenKeySharedPolicy>)
        : c(shared) {}

    void reset() { s = {}; }

    // 正規化周波数で指定
    void set_params(float wc, float resonance)
        requires requires(CoeffSource& c, float w, float r) { c.set_params(w, r); }
    {
        c.set_params(wc, resonance);
    }

    float process(float x, float wc, float resonance) {
        const auto& coeffs_ref = c.coeffs(wc, resonance);
        return detail::process_sallenkey(s, coeffs_ref, x);
    }

    float process(float x)
        requires(!CoeffSource::needs_params)
    {
        return process(x, 0.0f, 0.0f);
    }

  private:
    detail::SallenKeyState s{};
    CoeffSource c{};
};

template <typename CoeffSource>
using SallenKeyLpfT = SallenKey<CoeffSource>;

using SallenKeyInline = SallenKey<SallenKeyInlinePolicy>;
using SallenKeyAsync = SallenKey<SallenKeyOwnPolicy>;
using SallenKeyShared = SallenKey<SallenKeySharedPolicy>;
