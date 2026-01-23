#pragma once

#include <cmath>
#include <type_traits>

#include "common.hh"

namespace detail {

struct SlewLimiterCoeffs {
    float step = 0.0f;
};

inline SlewLimiterCoeffs make_slew_limiter_coeffs(float sample_rate, float slew_rate, float voltage_swing) {
    if (sample_rate <= 0.0f || voltage_swing <= 0.0f) {
        return {};
    }
    return {slew_rate / (sample_rate * voltage_swing)};
}

inline float process_slew(float& s, float step, float x) {
    const float dx = x - s;
    if (step < std::abs(dx)) {
        s += (0.0f < dx ? step : -step);
    } else {
        s = x;
    }
    return s;
}

} // namespace detail

using SlewLimiterInlinePolicy = InlineCoeffs<detail::SlewLimiterCoeffs, detail::make_slew_limiter_coeffs>;
using SlewLimiterOwnPolicy = OwnCoeffs<detail::SlewLimiterCoeffs, detail::make_slew_limiter_coeffs>;
using SlewLimiterSharedPolicy = SharedCoeffs<detail::SlewLimiterCoeffs>;

template <typename CoeffSource>
class SlewLimiter {
  public:
    SlewLimiter()
        requires(!std::is_same_v<CoeffSource, SlewLimiterSharedPolicy>)
    = default;
    explicit SlewLimiter(const detail::SlewLimiterCoeffs& shared)
        requires(std::is_same_v<CoeffSource, SlewLimiterSharedPolicy>)
        : c(shared) {}

    void reset() { s = 0.0f; }

    void set_params(float sample_rate, float slew_rate, float voltage_swing)
        requires requires(CoeffSource& c, float fs, float sr, float vs) { c.set_params(fs, sr, vs); }
    {
        c.set_params(sample_rate, slew_rate, voltage_swing);
    }

    float process(float x, float sample_rate, float slew_rate, float voltage_swing) {
        const auto& coeffs_ref = c.coeffs(sample_rate, slew_rate, voltage_swing);
        return detail::process_slew(s, coeffs_ref.step, x);
    }

    float process(float x)
        requires(!CoeffSource::needs_params)
    {
        const auto& coeffs_ref = c.coeffs();
        return detail::process_slew(s, coeffs_ref.step, x);
    }

  private:
    float s = 0.0f;
    CoeffSource c{};
};

using SlewLimiterInline = SlewLimiter<SlewLimiterInlinePolicy>;
using SlewLimiterAsync = SlewLimiter<SlewLimiterOwnPolicy>;
using SlewLimiterShared = SlewLimiter<SlewLimiterSharedPolicy>;
