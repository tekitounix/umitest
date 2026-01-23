#pragma once

#include <algorithm>
#include <cmath>
#include <numbers>
#include <type_traits>

#include "common.hh"

namespace detail {

struct Ssm2040Coeffs {
    float f = 0.0f;
    float r = 0.0f;
};

struct Ssm2040State {
    float s1 = 0.0f;
    float s2 = 0.0f;
    float s3 = 0.0f;
    float s4 = 0.0f;
    float prev_in = 0.0f;
    float prev_out = 0.0f;
};

inline float tanh_x_dx_ssm2040(float x) {
    if (std::abs(x) < 1.0e-6f) {
        return 1.0f;
    }
    if (std::abs(x) > 4.0f) {
        return 1.0f / std::abs(x);
    }

    const float x2 = x * x;
    return ((x2 + 100.0f) * x2 + 900.0f) / ((18.0f * x2 + 400.0f) * x2 + 900.0f);
}

inline Ssm2040Coeffs make_ssm2040_coeffs(float wc, float resonance) {
    wc = std::clamp(wc, 0.002f, 0.5f - 0.04f);
    resonance = std::clamp(resonance, 0.0f, 1.0f);
    const float f = std::tan(std::numbers::pi_v<float> * wc);
    const float r = 3.9f * resonance;
    return {f, r};
}

inline float process_ssm2040(Ssm2040State& s, const Ssm2040Coeffs& c, float input) {
    input *= 0.5f;

    const float ih = 0.5f * (input + s.prev_in);
    s.prev_in = input;

    const float t = tanh_x_dx_ssm2040(c.r * s.prev_out);

    const float f2 = c.f * c.f;
    const float f3 = f2 * c.f;
    const float f4 = f2 * f2;
    const float denom = f4 + 4.0f * f3 + 6.0f * f2 + 4.0f * c.f + 1.0f + c.r * t * f4;

    const float y1 = (s.s1 + c.f * ih - c.r * t * f4 * s.prev_out) / denom;
    const float y2 = (s.s2 + f2 * ih + c.f * s.s1) / denom;
    const float y3 = (s.s3 + f3 * ih + f2 * s.s1 + c.f * s.s2) / denom;
    const float y4 = (s.s4 + f4 * ih + f3 * s.s1 + f2 * s.s2 + c.f * s.s3) / denom;

    s.s1 += 2.0f * c.f * (ih - y1);
    s.s2 += 2.0f * c.f * (c.f * ih + s.s1 - y2);
    s.s3 += 2.0f * c.f * (f2 * ih + c.f * s.s1 + s.s2 - y3);
    s.s4 += 2.0f * c.f * (f3 * ih + f2 * s.s1 + c.f * s.s2 + s.s3 - y4);

    s.prev_out = y4;
    return y4;
}

} // namespace detail

using Ssm2040InlinePolicy = InlineCoeffs<detail::Ssm2040Coeffs, detail::make_ssm2040_coeffs>;
using Ssm2040OwnPolicy = OwnCoeffs<detail::Ssm2040Coeffs, detail::make_ssm2040_coeffs>;
using Ssm2040SharedPolicy = SharedCoeffs<detail::Ssm2040Coeffs>;

template <typename CoeffSource>
class Ssm2040 {
  public:
    Ssm2040()
        requires(!std::is_same_v<CoeffSource, Ssm2040SharedPolicy>)
    = default;
    explicit Ssm2040(const detail::Ssm2040Coeffs& shared)
        requires(std::is_same_v<CoeffSource, Ssm2040SharedPolicy>)
        : c(shared) {}

    void reset() { s = {}; }

    void set_params(float wc, float resonance)
        requires requires(CoeffSource& c, float w, float r) { c.set_params(w, r); }
    {
        c.set_params(wc, resonance);
    }

    float process(float x, float wc, float resonance) {
        const auto& coeffs_ref = c.coeffs(wc, resonance);
        return detail::process_ssm2040(s, coeffs_ref, x);
    }

    float process(float x)
        requires(!CoeffSource::needs_params)
    {
        const auto& coeffs_ref = c.coeffs();
        return detail::process_ssm2040(s, coeffs_ref, x);
    }

  private:
    detail::Ssm2040State s{};
    CoeffSource c{};
};

using Ssm2040Inline = Ssm2040<Ssm2040InlinePolicy>;
using Ssm2040Async = Ssm2040<Ssm2040OwnPolicy>;
using Ssm2040Shared = Ssm2040<Ssm2040SharedPolicy>;

using Prophet5Filter = Ssm2040<Ssm2040OwnPolicy>;
