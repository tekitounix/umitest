#pragma once

#include <array>
#include <cmath>
#include <numbers>
#include <type_traits>

#include "common.hh"

namespace detail {

struct ObMoogCoeffs {
    float g = 0.0f;
    float g2 = 0.0f;
    float g4 = 0.0f;
    float k = 0.0f;
    float one_minus_g = 0.0f;
};

struct ObMoogState {
    std::array<float, 4> s{};
};

inline float hard_clip(float x, float limit = 1.0f) {
    if (x > limit)
        return limit;
    if (x < -limit)
        return -limit;
    return x;
}

inline ObMoogCoeffs make_obmoog_coeffs(float wc, float resonance) {
    if (wc < 0.002f)
        wc = 0.002f;
    if (wc > 0.5f - 0.04f)
        wc = 0.5f - 0.04f;
    resonance = std::clamp(resonance, 0.0f, 1.0f);

    const float g = std::tan(std::numbers::pi_v<float> * wc);
    const float G = g / (1.0f + g);
    const float g2 = G * G;
    const float g4 = g2 * g2;

    ObMoogCoeffs c{};
    c.g = G;
    c.g2 = g2;
    c.g4 = g4;
    c.k = 3.2f * resonance;
    c.one_minus_g = 1.0f - G;
    return c;
}

inline float process_obmoog(ObMoogState& s, const ObMoogCoeffs& c, float x) {
    float sum = c.g2 * c.g * (s.s[0] * c.one_minus_g) + c.g2 * (s.s[1] * c.one_minus_g) +
                c.g * (s.s[2] * c.one_minus_g) + (s.s[3] * c.one_minus_g);

    sum = hard_clip(sum * 0.5f) * 2.0f;

    const float y = (x - c.k * sum) / (1.0f + c.k * c.g4);

    const float v1 = (y - s.s[0]) * c.g;
    const float v2 = (s.s[0] - s.s[1]) * c.g;
    const float v3 = (s.s[1] - s.s[2]) * c.g;
    const float v4 = (s.s[2] - s.s[3]) * c.g;

    const float lp1 = v1 + s.s[0];
    const float lp2 = v2 + s.s[1];
    const float lp3 = v3 + s.s[2];
    const float lp4 = v4 + s.s[3];

    s.s[0] = v1 + lp1;
    s.s[1] = v2 + lp2;
    s.s[2] = v3 + lp3;
    s.s[3] = v4 + lp4;

    return lp4;
}

} // namespace detail

using ObMoogInlinePolicy = InlineCoeffs<detail::ObMoogCoeffs, detail::make_obmoog_coeffs>;
using ObMoogOwnPolicy = OwnCoeffs<detail::ObMoogCoeffs, detail::make_obmoog_coeffs>;
using ObMoogSharedPolicy = SharedCoeffs<detail::ObMoogCoeffs>;

template <typename CoeffSource>
class ObMoogLadder {
  public:
    ObMoogLadder()
        requires(!std::is_same_v<CoeffSource, ObMoogSharedPolicy>)
    = default;
    explicit ObMoogLadder(const detail::ObMoogCoeffs& shared)
        requires(std::is_same_v<CoeffSource, ObMoogSharedPolicy>)
        : c(shared) {}

    void reset() { s = {}; }

    void set_params(float wc, float resonance)
        requires requires(CoeffSource& c, float w, float r) { c.set_params(w, r); }
    {
        c.set_params(wc, resonance);
    }

    float process(float x, float wc, float resonance) {
        const auto& coeffs_ref = c.coeffs(wc, resonance);
        return detail::process_obmoog(s, coeffs_ref, x);
    }

    float process(float x)
        requires(!CoeffSource::needs_params)
    {
        const auto& coeffs_ref = c.coeffs();
        return detail::process_obmoog(s, coeffs_ref, x);
    }

  private:
    detail::ObMoogState s{};
    CoeffSource c{};
};

using ObMoogLadderInline = ObMoogLadder<ObMoogInlinePolicy>;
using ObMoogLadderAsync = ObMoogLadder<ObMoogOwnPolicy>;
using ObMoogLadderShared = ObMoogLadder<ObMoogSharedPolicy>;
