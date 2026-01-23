#pragma once

#include <array>
#include <cstdint>
#include <type_traits>

#include "common.hh"

namespace detail {

template <size_t Stages>
struct BiquadCoeffs {
    std::array<float, Stages> b0{};
    std::array<float, Stages> b1{};
    std::array<float, Stages> b2{};
    std::array<float, Stages> a1{};
    std::array<float, Stages> a2{};
};

template <size_t Stages>
struct BiquadState {
    std::array<float, Stages> z1{};
    std::array<float, Stages> z2{};
};

template <size_t Stages>
constexpr BiquadCoeffs<Stages> make_biquad_coeffs_q29(const std::array<int32_t, 5 * Stages>& coeffs) {
    BiquadCoeffs<Stages> c{};
    for (size_t stage = 0; stage < Stages; ++stage) {
        c.b0[stage] = coeffs[stage * 5 + 0] / float(1 << 29);
        c.b1[stage] = coeffs[stage * 5 + 1] / float(1 << 30);
        c.b2[stage] = coeffs[stage * 5 + 2] / float(1 << 30);
        c.a1[stage] = coeffs[stage * 5 + 3] / float(1 << 30);
        c.a2[stage] = coeffs[stage * 5 + 4] / float(1 << 30);
    }
    return c;
}

template <size_t Stages>
inline float process_biquad(BiquadState<Stages>& s, const BiquadCoeffs<Stages>& c, float input) {
    float output = input;
    for (size_t stage = 0; stage < Stages; ++stage) {
        const float v = output * c.b0[stage] + s.z1[stage];
        output = v + s.z2[stage];
        s.z2[stage] = c.b2[stage] * v - c.a2[stage] * output;
        s.z1[stage] = c.b1[stage] * v - c.a1[stage] * output;
    }
    return output;
}

} // namespace detail

template <size_t Stages>
using BiquadInline = InlineCoeffs<detail::BiquadCoeffs<Stages>, detail::make_biquad_coeffs_q29<Stages>>;

template <size_t Stages>
using BiquadOwn = OwnCoeffs<detail::BiquadCoeffs<Stages>, detail::make_biquad_coeffs_q29<Stages>>;

template <size_t Stages>
using BiquadShared = SharedCoeffs<detail::BiquadCoeffs<Stages>>;

template <size_t Stages, typename CoeffSource>
class Biquad {
  public:
    Biquad()
        requires(!std::is_same_v<CoeffSource, BiquadShared<Stages>>)
    = default;
    explicit Biquad(const detail::BiquadCoeffs<Stages>& shared)
        requires(std::is_same_v<CoeffSource, BiquadShared<Stages>>)
        : c(shared) {}

    void reset() {
        s.z1.fill(0.0f);
        s.z2.fill(0.0f);
    }

    void set_params(const std::array<int32_t, 5 * Stages>& coeffs)
        requires requires(CoeffSource& c, const std::array<int32_t, 5 * Stages>& v) { c.set_params(v); }
    {
        c.set_params(coeffs);
    }

    void set_coeffs(const std::array<int32_t, 5 * Stages>& coeffs)
        requires requires(CoeffSource& c, const std::array<int32_t, 5 * Stages>& v) { c.set_params(v); }
    {
        set_params(coeffs);
    }

    float process(float input, const std::array<int32_t, 5 * Stages>& coeffs) {
        const auto& coeffs_ref = c.coeffs(coeffs);
        return detail::process_biquad(s, coeffs_ref, input);
    }

    float process(float input)
        requires(!CoeffSource::needs_params)
    {
        const auto& coeffs_ref = c.coeffs();
        return detail::process_biquad(s, coeffs_ref, input);
    }

  private:
    detail::BiquadState<Stages> s{};
    CoeffSource c{};
};

using Biquad4 = Biquad<4, BiquadOwn<4>>;
