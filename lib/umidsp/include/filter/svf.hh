#pragma once

#include <cmath>
#include <numbers>
#include <type_traits>

#include "common.hh"

namespace detail {

struct SvfCoeffs {
    float g = 0.0f;
    float k = 0.0f; // 1/Q
    float h = 1.0f;
};

struct SvfState {
    float ic1eq = 0.0f;
    float ic2eq = 0.0f;
};

inline SvfCoeffs calc_svf_from_q(float wc, float q) {
    if (wc < 1.0e-6f)
        wc = 1.0e-6f;
    if (wc > 0.5f)
        wc = 0.5f;
    if (q < 0.1f)
        q = 0.1f;

    const float g = std::tan(std::numbers::pi_v<float> * wc);
    const float k = 1.0f / q;
    const float h = 1.0f / (1.0f + g * (g + k));
    return {g, k, h};
}

inline SvfCoeffs calc_svf_from_resonance(float wc, float resonance) {
    if (resonance < 0.0f)
        resonance = 0.0f;
    if (resonance > 1.0f)
        resonance = 1.0f;

    // k = 2 - 2 * resonance  (0..1 knob)
    float k = 2.0f - 2.0f * resonance;
    if (k < 0.05f)
        k = 0.05f;
    const float q = 1.0f / k;
    return calc_svf_from_q(wc, q);
}

inline void
process_svf(SvfState& s, const SvfCoeffs& c, float x, float& lp, float& bp, float& hp, float& notch, float& ap) {
    const float v3 = x - s.ic2eq;
    const float v1 = (c.g * v3 + s.ic1eq) * c.h;
    const float v2 = s.ic2eq + c.g * v1;

    s.ic1eq = 2.0f * v1 - s.ic1eq;
    s.ic2eq = 2.0f * v2 - s.ic2eq;

    bp = v1;
    lp = v2;
    hp = x - c.k * bp - lp;
    notch = hp + lp;
    ap = notch - c.k * bp;
}

} // namespace detail

enum class SvfOut : uint8_t {
    Lp = 1u << 0,
    Bp = 1u << 1,
    Hp = 1u << 2,
    Notch = 1u << 3,
    Ap = 1u << 4,
    All = Lp | Bp | Hp | Notch | Ap,
};

constexpr SvfOut operator|(SvfOut a, SvfOut b) {
    return static_cast<SvfOut>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

constexpr bool has_output(SvfOut mask, SvfOut flag) {
    return (static_cast<uint8_t>(mask) & static_cast<uint8_t>(flag)) != 0u;
}

using SvfInlinePolicy = InlineCoeffs<detail::SvfCoeffs, detail::calc_svf_from_resonance>;
using SvfOwnPolicy = OwnCoeffs<detail::SvfCoeffs, detail::calc_svf_from_resonance>;
using SvfSharedPolicy = SharedCoeffs<detail::SvfCoeffs>;

template <typename CoeffSource, SvfOut Outputs = SvfOut::All>
class Svf {
  public:
    Svf()
        requires(!std::is_same_v<CoeffSource, SvfSharedPolicy>)
    = default;
    explicit Svf(const detail::SvfCoeffs& shared)
        requires(std::is_same_v<CoeffSource, SvfSharedPolicy>)
        : c(shared) {}

    void reset() { s = {}; }

    // 正規化周波数で指定
    void set_params(float wc, float resonance) {
        wc_ = wc;
        resonance_ = resonance;
        if constexpr (requires(CoeffSource& c, float w, float r) { c.set_params(w, r); }) {
            c.set_params(wc, resonance);
        }
    }

    void set_q(float wc, float q) {
        wc_ = wc;
        float resonance = 1.0f - 0.5f * (1.0f / q);
        if (resonance < 0.0f)
            resonance = 0.0f;
        if (resonance > 1.0f)
            resonance = 1.0f;
        resonance_ = resonance;
    }

    void set_params(float cutoff_hz, float resonance, float dt) { set_params(calc_wc(cutoff_hz, dt), resonance); }

    void process(float x, float wc, float resonance) {
        const auto& coeffs_ref = c.coeffs(wc, resonance);
        float lp = 0.0f;
        float bp = 0.0f;
        float hp = 0.0f;
        float notch = 0.0f;
        float ap = 0.0f;
        detail::process_svf(s, coeffs_ref, x, lp, bp, hp, notch, ap);

        if constexpr (has_output(Outputs, SvfOut::Lp)) {
            lp_ = lp;
        }
        if constexpr (has_output(Outputs, SvfOut::Bp)) {
            bp_ = bp;
        }
        if constexpr (has_output(Outputs, SvfOut::Hp)) {
            hp_ = hp;
        }
        if constexpr (has_output(Outputs, SvfOut::Notch)) {
            notch_ = notch;
        }
        if constexpr (has_output(Outputs, SvfOut::Ap)) {
            ap_ = ap;
        }
    }

    void process(float x) {
        if constexpr (CoeffSource::needs_params) {
            process(x, wc_, resonance_);
        } else {
            const auto& coeffs_ref = c.coeffs();
            float lp = 0.0f;
            float bp = 0.0f;
            float hp = 0.0f;
            float notch = 0.0f;
            float ap = 0.0f;
            detail::process_svf(s, coeffs_ref, x, lp, bp, hp, notch, ap);

            if constexpr (has_output(Outputs, SvfOut::Lp)) {
                lp_ = lp;
            }
            if constexpr (has_output(Outputs, SvfOut::Bp)) {
                bp_ = bp;
            }
            if constexpr (has_output(Outputs, SvfOut::Hp)) {
                hp_ = hp;
            }
            if constexpr (has_output(Outputs, SvfOut::Notch)) {
                notch_ = notch;
            }
            if constexpr (has_output(Outputs, SvfOut::Ap)) {
                ap_ = ap;
            }
        }
    }

    [[nodiscard]] float operator()(float x) {
        process(x);
        if constexpr (has_output(Outputs, SvfOut::Lp)) {
            return lp_;
        } else if constexpr (has_output(Outputs, SvfOut::Bp)) {
            return bp_;
        } else if constexpr (has_output(Outputs, SvfOut::Hp)) {
            return hp_;
        } else if constexpr (has_output(Outputs, SvfOut::Notch)) {
            return notch_;
        } else {
            return ap_;
        }
    }

    [[nodiscard]] float lp() const { return lp_; }
    [[nodiscard]] float bp() const { return bp_; }
    [[nodiscard]] float hp() const { return hp_; }
    [[nodiscard]] float notch() const { return notch_; }
    [[nodiscard]] float ap() const { return ap_; }

  private:
    detail::SvfState s{};
    CoeffSource c{};
    float lp_ = 0.0f;
    float bp_ = 0.0f;
    float hp_ = 0.0f;
    float notch_ = 0.0f;
    float ap_ = 0.0f;
    float wc_ = 0.0f;
    float resonance_ = 0.0f;
};

template <typename CoeffSource, SvfOut Outputs = SvfOut::All>
using SvfTpt = Svf<CoeffSource, Outputs>;

using SvfInline = Svf<SvfInlinePolicy>;
using SvfAsync = Svf<SvfOwnPolicy>;
using SvfShared = Svf<SvfSharedPolicy>;
