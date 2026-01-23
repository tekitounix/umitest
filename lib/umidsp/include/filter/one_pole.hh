#pragma once

#include <cmath>
#include <numbers>
#include <type_traits>

#include "common.hh"

namespace detail {

struct OnePoleCoeffs {
    float a0 = 0.0f;
};

struct OnePoleState {
    float z1 = 0.0f;
};

constexpr float calc_a0(float wc) {
    // g = tan(pi * wc);
    // a0 = g / (1.0f + g);
    // ↓Approximation↓
    // a0 = ((15.952062f * wc - 11.969296f) * wc + 5.9948827f) * wc * 0.5f;
    return (2.92778f - 7.35606f * wc * (0.753747f - wc)) * wc;
}

constexpr OnePoleCoeffs make_onepole_coeffs_tpt(float wc) {
    return {calc_a0(wc)};
}

inline float process_lp(OnePoleState& s, float a0, float x) {
    const auto v = (x - s.z1) * a0;
    const auto y = v + s.z1;
    s.z1 = v + y;
    return y;
}

} // namespace detail

enum class OnePoleKind { Lp, Hp };

using OnePoleInline = InlineCoeffs<detail::OnePoleCoeffs, detail::make_onepole_coeffs_tpt>;
using OnePoleOwn = OwnCoeffs<detail::OnePoleCoeffs, detail::make_onepole_coeffs_tpt>;
using OnePoleShared = SharedCoeffs<detail::OnePoleCoeffs>;

template <OnePoleKind Kind, typename CoeffSource>
class OnePole {
  public:
    OnePole()
        requires(!std::is_same_v<CoeffSource, OnePoleShared>)
    = default;
    explicit OnePole(const detail::OnePoleCoeffs& shared)
        requires(std::is_same_v<CoeffSource, OnePoleShared>)
        : c(shared) {}

    void reset() { s.z1 = 0.0f; }

    void set_params(float wc)
        requires requires(CoeffSource& c, float v) { c.set_params(v); }
    {
        c.set_params(wc);
    }

    void set_wc(float wc)
        requires requires(CoeffSource& c, float v) { c.set_params(v); }
    {
        set_params(wc);
    }

    float process(float x, float wc) {
        const auto& coeffs_ref = c.coeffs(wc);
        const auto y_lp = detail::process_lp(s, coeffs_ref.a0, x);
        if constexpr (Kind == OnePoleKind::Lp) {
            return y_lp;
        } else {
            return x - y_lp;
        }
    }

    float process(float x)
        requires(!CoeffSource::needs_params)
    {
        return process(x, 0.0f);
    }

  private:
    detail::OnePoleState s{};
    CoeffSource c{};
};

using Lpf = OnePole<OnePoleKind::Lp, OnePoleInline>;
using LpfAsync = OnePole<OnePoleKind::Lp, OnePoleOwn>;
using LpfShared = OnePole<OnePoleKind::Lp, OnePoleShared>;

using Hpf = OnePole<OnePoleKind::Hp, OnePoleInline>;
using HpfAsync = OnePole<OnePoleKind::Hp, OnePoleOwn>;
using HpfShared = OnePole<OnePoleKind::Hp, OnePoleShared>;
