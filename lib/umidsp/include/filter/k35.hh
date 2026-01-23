#pragma once

#include <algorithm>
#include <cmath>
#include <numbers>
#include <type_traits>

#include "common.hh"
#include "halfband.hh"

namespace detail {

struct K35Coeffs {
    float f = 0.0f;
    float r = 0.0f;
};

struct K35State {
    float yl1 = 0.0f;
    float yl2 = 0.0f;
    HalfbandOwn anti_alias[2];
};

inline float tanh_x_dx_k35(float x) {
    const float a = x * x;
    return ((a + 105.0f) * a + 945.0f) / ((15.0f * a + 420.0f) * a + 945.0f);
}

inline K35Coeffs make_k35_coeffs(float wc, float resonance) {
    wc = std::clamp(wc, 0.002f, 0.5f - 0.04f);
    resonance = std::clamp(resonance, 0.0f, 1.0f);
    const float f = std::tan(std::numbers::pi_v<float> * wc);
    const float r = 3.0f * resonance;
    return {f, r};
}

inline float process_k35_impl(K35State& s, const K35Coeffs& c, float input) {
    input *= 0.5f;

    const float t = tanh_x_dx_k35(c.r * s.yl2);
    const float f_plus_1 = c.f + 1.0f;
    const float ff = c.f * c.f;
    const float denom = ff * c.r * t + f_plus_1 * f_plus_1;
    const float y2 = (ff * input + c.f * s.yl1 + f_plus_1 * s.yl2) / denom;
    const float y1 = (f_plus_1 * y2 - s.yl2) / c.f;

    s.yl1 += 2.0f * c.f * (input - y1 - c.r * t * y2);
    s.yl2 += 2.0f * c.f * (y1 + c.r * t * y2 - y2);

    return y2;
}

inline float process_k35(K35State& s, const K35Coeffs& c, float x) {
    s.anti_alias[1].process(process_k35_impl(s, c, s.anti_alias[0].process(x)));
    return s.anti_alias[1].process(process_k35_impl(s, c, s.anti_alias[0].process(0.0f)));
}

} // namespace detail

using K35InlinePolicy = InlineCoeffs<detail::K35Coeffs, detail::make_k35_coeffs>;
using K35OwnPolicy = OwnCoeffs<detail::K35Coeffs, detail::make_k35_coeffs>;
using K35SharedPolicy = SharedCoeffs<detail::K35Coeffs>;

template <typename CoeffSource>
class K35 {
  public:
    K35()
    requires(!std::is_same_v<CoeffSource, K35SharedPolicy>) = default;
    explicit K35(const detail::K35Coeffs& shared)
        requires(std::is_same_v<CoeffSource, K35SharedPolicy>)
        : c(shared) {}

    void reset() {
        s.yl1 = 0.0f;
        s.yl2 = 0.0f;
        s.anti_alias[0].reset();
        s.anti_alias[1].reset();
    }

    void set_params(float wc, float resonance) {
        wc_ = wc;
        resonance_ = resonance;
        if constexpr (requires(CoeffSource& c, float w, float r) { c.set_params(w, r); }) {
            c.set_params(wc, resonance);
        }
    }

    float process(float x, float wc, float resonance) {
        const auto& coeffs_ref = c.coeffs(wc, resonance);
        return detail::process_k35(s, coeffs_ref, x);
    }

    float process(float x) {
        if constexpr (CoeffSource::needs_params) {
            return process(x, wc_, resonance_);
        } else {
            const auto& coeffs_ref = c.coeffs();
            return detail::process_k35(s, coeffs_ref, x);
        }
    }

  private:
    detail::K35State s{};
    CoeffSource c{};
    float wc_ = 0.0f;
    float resonance_ = 0.0f;
};

using K35Inline = K35<K35InlinePolicy>;
using K35Async = K35<K35OwnPolicy>;
using K35Shared = K35<K35SharedPolicy>;
