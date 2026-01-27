#pragma once

#include <cstdint>

namespace mo {

static inline std::uint32_t swap(std::uint32_t x) {
  return (x >> 16) | (x << 16);
}

template <std::floating_point F>
[[nodiscard]] [[gnu::always_inline]]
constexpr F pot_impedance(F x, F r) noexcept {
  return -x * (x - F(1)) * r;
}

// Fast approximation of sine between 0 and pi/2
template <std::floating_point F>
[[nodiscard]] [[gnu::always_inline]] constexpr F fast_sine_quarter(F x) noexcept {
  constexpr F a = F(0.405284735) * F(4) / pi<F>;
  constexpr F b = F(0.878469146);
  constexpr F c = F(0.225);
  F x2 = x * x;
  return (a * x - a * x * x2) / (b - c * x2 + x2 * x2) + x;
}

// 1. Linear crossfade
template <std::floating_point F>
[[nodiscard]] [[gnu::always_inline]]
constexpr F linear_crossfade(F x1, F x2, F t) noexcept {
  return x1 * (F(1) - t) + x2 * t;
}

// 2. Constant power crossfade
template <std::floating_point F>
[[nodiscard]] [[gnu::always_inline]]
constexpr F constant_power_crossfade(F x1, F x2, F t) noexcept {
  // F gain1 = std::cos(t * std::numbers::pi_v<F> / F(2));
  // F gain2 = std::sin(t * std::numbers::pi_v<F> / F(2));
  // return x1 * gain1 + x2 * gain2;
  F sine = fast_sine_quarter(t * half_pi<F>);  // pi/2
  F cosine = fast_sine_quarter(half_pi<F> - t * half_pi<F>);
  return x1 * cosine + x2 * sine;
}

// 3. Smooth step function
template <std::floating_point F>
[[nodiscard]] [[gnu::always_inline]]
constexpr F smooth_step(F x) noexcept {
  return x * x * (F(3) - F(2) * x);
}

// 4. Smoothed constant power crossfade
template <std::floating_point F>
[[nodiscard]] [[gnu::always_inline]]
constexpr F smooth_constant_power_crossfade(F x1, F x2, F t) noexcept {
  F smoothT = smooth_step(t);
  // F gain1 = std::cos(smoothT * std::numbers::pi_v<F> / F(2));
  // F gain2 = std::sin(smoothT * std::numbers::pi_v<F> / F(2));
  // return x1 * gain1 + x2 * gain2;
  return constant_power_crossfade(x1, x2, smoothT);
}

// 5. Equal power crossfade
template <std::floating_point F>
[[nodiscard]] [[gnu::always_inline]]
constexpr F equal_power_crossfade(F x1, F x2, F t) noexcept {
  F gain1 = std::sqrt(F(1) - t);
  F gain2 = std::sqrt(t);
  // F gain1 = F(1) - t * (F(0.5) + F(0.125) * t);
  // F gain2 = t * (F(1) - F(0.25) * t);
  return x1 * gain1 + x2 * gain2;
}

}  // namespace mo
