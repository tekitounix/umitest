// SPDX-License-Identifier: MIT
// UMI-DSP: Core Constants
#pragma once

#include <cmath>

namespace umidsp {

inline constexpr float Pi = 3.14159265358979323846f;
inline constexpr float TwoPi = 2.0f * Pi;
inline constexpr float HalfPi = Pi / 2.0f;

}  // namespace umidsp

// Legacy namespace alias
namespace umi::dsp {
using namespace umidsp;
}
