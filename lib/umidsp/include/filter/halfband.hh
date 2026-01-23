#pragma once

#include <array>
#include <type_traits>

#include "common.hh"

namespace detail {

struct HalfbandCoeffs {
    std::array<float, 3> g{{-0.114779080944552331f, 0.344337242833656965f, 0.540883676221790677f}};
};

struct HalfbandState {
    std::array<float, 7> z{};
};

constexpr HalfbandCoeffs make_halfband_coeffs() {
    return {};
}

inline float process_halfband(HalfbandState& s, const HalfbandCoeffs& c, float x) {
    s.z[6] = s.z[5];
    s.z[5] = s.z[4];
    s.z[4] = s.z[3];
    s.z[3] = s.z[2];
    s.z[2] = s.z[1];
    s.z[1] = s.z[0];
    s.z[0] = x;

    float y = (s.z[0] + s.z[6]) * c.g[0];
    y += (s.z[2] + s.z[4]) * c.g[1];
    y += s.z[3] * c.g[2];
    return y;
}

} // namespace detail

using HalfbandOwnPolicy = OwnCoeffs<detail::HalfbandCoeffs, detail::make_halfband_coeffs>;
using HalfbandSharedPolicy = SharedCoeffs<detail::HalfbandCoeffs>;

template <typename CoeffSource>
class Halfband {
  public:
    Halfband()
        requires(!std::is_same_v<CoeffSource, HalfbandSharedPolicy>)
    = default;
    explicit Halfband(const detail::HalfbandCoeffs& shared)
        requires(std::is_same_v<CoeffSource, HalfbandSharedPolicy>)
        : c(shared) {}

    void reset() { s.z.fill(0.0f); }

    void set_params()
        requires requires(CoeffSource& c) { c.set_params(); }
    {
        c.set_params();
    }

    float process(float x) { return detail::process_halfband(s, c.coeffs(), x); }

  private:
    detail::HalfbandState s{};
    CoeffSource c{};
};

using HalfbandOwn = Halfband<HalfbandOwnPolicy>;
using HalfbandShared = Halfband<HalfbandSharedPolicy>;
