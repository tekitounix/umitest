#pragma once

#include <algorithm>
#include <cmath>

#include "common.hh"

namespace detail {

struct SmootherCoeffs {
    float g0 = 0.0f;
    float sense = 0.0f;
};

struct SmootherState {
    float s1 = 0.0f;
    float s2 = 0.0f;
};

inline float calc_smoother_g0(float wc) {
    // Third-order polynomial approximation of:
    // gc = tan(pi * wc)
    // g0 = 2 * gc / (1 + gc)
    return ((15.952062f * wc - 11.969296f) * wc + 5.9948827f) * wc;
}

inline SmootherCoeffs make_smoother_coeffs(float wc, float sense) {
    if (wc < 1.0e-6f)
        wc = 1.0e-6f;
    if (wc > 0.5f)
        wc = 0.5f;
    return {calc_smoother_g0(wc), sense};
}

inline float process_smoother(SmootherState& s, const SmootherCoeffs& c, float x) {
    const float y1 = s.s1;
    const float y2 = s.s2;
    const float bandz = y1 - y2;
    const float g = std::min(c.g0 + c.sense * std::abs(bandz), 1.0f);
    s.s1 = y1 + g * (x - y1);
    s.s2 = y2 + g * (s.s1 - y2);
    return s.s2;
}

} // namespace detail

using SmootherInlinePolicy = InlineCoeffs<detail::SmootherCoeffs, detail::make_smoother_coeffs>;
using SmootherOwnPolicy = OwnCoeffs<detail::SmootherCoeffs, detail::make_smoother_coeffs>;
using SmootherSharedPolicy = SharedCoeffs<detail::SmootherCoeffs>;

template <typename CoeffSource>
class Smoother {
  public:
    Smoother()
        requires(!std::is_same_v<CoeffSource, SmootherSharedPolicy>)
    = default;
    explicit Smoother(const detail::SmootherCoeffs& shared)
        requires(std::is_same_v<CoeffSource, SmootherSharedPolicy>)
        : c(shared) {}

    void reset() { s = {}; }

    // 正規化周波数で指定
    void set_params(float wc, float sense)
        requires requires(CoeffSource& c, float w, float s) { c.set_params(w, s); }
    {
        c.set_params(wc, sense);
    }

    float process(float x, float wc, float sense) {
        const auto& coeffs_ref = c.coeffs(wc, sense);
        return detail::process_smoother(s, coeffs_ref, x);
    }

    float process(float x)
        requires(!CoeffSource::needs_params)
    {
        return process(x, 0.0f, 0.0f);
    }

  private:
    detail::SmootherState s{};
    CoeffSource c{};
};

template <typename CoeffSource>
using SmootherT = Smoother<CoeffSource>;

using SmootherInline = Smoother<SmootherInlinePolicy>;
using SmootherAsync = Smoother<SmootherOwnPolicy>;
using SmootherShared = Smoother<SmootherSharedPolicy>;
