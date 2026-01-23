#pragma once

#include <array>
#include <cmath>
#include <numbers>
#include <type_traits>

#include "common.hh"

namespace detail {

struct MoogCoeffs {
    float g = 0.0f;
    float k = 0.0f;
    float half_dt = 0.0f;
    float div2vt = 0.0f;
};

struct MoogState {
    std::array<float, 4> v{};
    std::array<float, 4> dv{};
    std::array<float, 4> tv{};
};

inline MoogCoeffs make_moog_coeffs(float wc, float resonance, float dt) {
    if (wc < 0.002f)
        wc = 0.002f;
    if (wc > 0.5f - 0.04f)
        wc = 0.5f - 0.04f;
    resonance = std::clamp(resonance, 0.0f, 1.0f);
    if (dt <= 0.0f)
        dt = 1.0f;

    constexpr float vt = 0.312f;
    const float x = std::numbers::pi_v<float> * wc;
    const float g = 4.0f * x * vt * (1.0f - x) / (dt + x * dt);
    const float k = 4.0f * resonance;

    MoogCoeffs c{};
    c.g = g;
    c.k = k;
    c.half_dt = 0.5f * dt;
    c.div2vt = 1.0f / (2.0f * vt);
    return c;
}

inline float process_moog(MoogState& s, const MoogCoeffs& c, float x) {
    const float dV0 = -c.g * (std::tanh((x + c.k * s.v[3]) * c.div2vt) + s.tv[0]);
    s.v[0] += (dV0 + s.dv[0]) * c.half_dt;
    s.dv[0] = dV0;
    s.tv[0] = std::tanh(s.v[0] * c.div2vt);

    const float dV1 = c.g * (s.tv[0] - s.tv[1]);
    s.v[1] += (dV1 + s.dv[1]) * c.half_dt;
    s.dv[1] = dV1;
    s.tv[1] = std::tanh(s.v[1] * c.div2vt);

    const float dV2 = c.g * (s.tv[1] - s.tv[2]);
    s.v[2] += (dV2 + s.dv[2]) * c.half_dt;
    s.dv[2] = dV2;
    s.tv[2] = std::tanh(s.v[2] * c.div2vt);

    const float dV3 = c.g * (s.tv[2] - s.tv[3]);
    s.v[3] += (dV3 + s.dv[3]) * c.half_dt;
    s.dv[3] = dV3;
    s.tv[3] = std::tanh(s.v[3] * c.div2vt);

    return s.v[3];
}

} // namespace detail

using MoogInlinePolicy = InlineCoeffs<detail::MoogCoeffs, detail::make_moog_coeffs>;
using MoogOwnPolicy = OwnCoeffs<detail::MoogCoeffs, detail::make_moog_coeffs>;
using MoogSharedPolicy = SharedCoeffs<detail::MoogCoeffs>;

template <typename CoeffSource>
class MoogLadder {
  public:
    MoogLadder()
        requires(!std::is_same_v<CoeffSource, MoogSharedPolicy>)
    = default;
    explicit MoogLadder(const detail::MoogCoeffs& shared)
        requires(std::is_same_v<CoeffSource, MoogSharedPolicy>)
        : c(shared) {}

    void reset() { s = {}; }

    void set_params(float wc, float resonance, float dt)
        requires requires(CoeffSource& c, float w, float r, float d) { c.set_params(w, r, d); }
    {
        c.set_params(wc, resonance, dt);
    }

    float process(float x, float wc, float resonance, float dt) {
        const auto& coeffs_ref = c.coeffs(wc, resonance, dt);
        return detail::process_moog(s, coeffs_ref, x);
    }

    float process(float x)
        requires(!CoeffSource::needs_params)
    {
        const auto& coeffs_ref = c.coeffs();
        return detail::process_moog(s, coeffs_ref, x);
    }

  private:
    detail::MoogState s{};
    CoeffSource c{};
};

using MoogLadderInline = MoogLadder<MoogInlinePolicy>;
using MoogLadderAsync = MoogLadder<MoogOwnPolicy>;
using MoogLadderShared = MoogLadder<MoogSharedPolicy>;
