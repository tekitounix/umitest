// SPDX-License-Identifier: MIT
// UMI-OS - DSP Constants
//
// Shared mathematical constants for DSP modules.
// This file has no dependencies and can be included anywhere.

#pragma once

#if __has_include(<numbers>) && __cplusplus >= 202002L
#include <numbers>
#endif

namespace umi::dsp {

// ============================================================================
// Mathematical Constants
// ============================================================================

#if defined(__cpp_lib_math_constants)
inline constexpr float kPi = std::numbers::pi_v<float>;
#else
inline constexpr float kPi = 3.14159265358979323846f;
#endif
inline constexpr float k2Pi = kPi * 2.0f;

} // namespace umi::dsp
