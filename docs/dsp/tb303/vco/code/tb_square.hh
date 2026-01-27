#pragma once

#include <cmath>
#include <algorithm>
#include <cstdint>

namespace dsp {
namespace oscillator {

/**
 * TB-303 Square Wave Oscillator
 * Models the 2SA733 PNP transistor-based square wave shaping circuit
 */
class SquareShaper {
 public:
  void reset() { Ve = 0.0f; }

  struct Param {
    float dt = 1.0f / 48'000.0f;
    float shape = 0.5f;
  };

  void prepare(Param&& p) {
    // shape parameter controls Ce from 0.1µF to 1.0µF
    const float Ce = 0.1e-6f + (p.shape * 0.9e-6f);  // 0.1µF + shape * 0.9µF
    g = p.dt / Ce;
  }

  struct IO {
    float in = 0;
    float out = 0;
  };

  IO process(IO&& io) {
    float Veb = Ve - io.in;
    float Ib = std::min(Is * (fast_exp(Veb * divVt) - 1.0f), Ib_max);
    float Ic_ideal = beta * Ib;
    float Ic_lin = Ve * divRc;
    float Ic_sat = Ic_lin * fast_tanh(Ve * divVce_sat);
    // float Ic = Ic_sat * fast_tanh(Ic_ideal / std::max(Ic_sat, 1e-6f));
    const float den_part = 0.1f * std::max(Ic_sat, 1e-6f);
    const float Ic = (Ic_sat * Ic_ideal) / (std::abs(Ic_ideal) + den_part);
    float Ie = Ib + Ic;
    float Icharge = (Vcc - Ve) * divRe;
    Ve += g * (Icharge - Ie);
    io.out = Rc * Ic;
    return io;
  }

 private:
  // Fast exponential approximation
  static constexpr float fast_exp(float x) {
    float y = x * 1.442695041f;
    int32_t i = static_cast<int32_t>(y + (y < 0.0f ? -0.5f : 0.5f));
    float f = y - static_cast<float>(i);
    if (f < 0.0f) {
      f += 1.0f;
      i -= 1;
    }
    union {
      float f;
      int32_t i;
    } conv;
    i += 127;
    i = std::clamp(i, 0, 254);
    conv.i = i << 23;
    float p = 1.0f + f * (0.6931471806f + f * (0.2402265069f + f * (0.0551041086f + f * 0.0095158916f)));
    return conv.f * p;
  }

  // Fast tanh approximation
  static constexpr float fast_tanh(float x) noexcept {
    return x / (std::abs(x) + 0.1f);
  }

  // 2SA733 PNP SPICE model parameters
  static constexpr float Re = 22e3f;    // External emitter resistance (�)
  // static constexpr float Ce = 0.5e-6f;  // External emitter capacitor (F)
  // static constexpr float divCe = 1.0f / Ce;
  static constexpr float Rc = 10e3f;  // External collector resistance (�)
  static constexpr float divRc = 1.0f / Rc;
  static constexpr float divRe = 1.0f / Re;
  static constexpr float Vcc = 6.67f;         // Supply voltage (V)
  static constexpr float Is = 55.9e-15f;      // Reverse saturation current (A)
  static constexpr float NF = 1.01201f;       // Emission coefficient
  static constexpr float Vt = 0.02585f * NF;  // Effective thermal voltage (V)
  static constexpr float divVt = 1.0f / Vt;
  static constexpr float beta = 205.0f;       // hFE
  static constexpr float Ib_max = 66.7e-06f;  // Maximum base current (A)
  static constexpr float Vce_sat = 0.1f;      // VCE saturation voltage (V)
  static constexpr float divVce_sat = 1.0f / Vce_sat;

  // State variables
  float Ve = 0.0f;             // Emitter voltage (V)
  float g = 0.0f;              // Time constant coefficient
};

}  // namespace oscillator
}  // namespace dsp
