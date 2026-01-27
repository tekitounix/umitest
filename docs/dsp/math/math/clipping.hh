#pragma once

#include <concepts>
#include <cmath>
#include "basic_math.hh"
#include "algorithm.hh"

namespace mo {
inline namespace cliping {
template <std::floating_point F>
[[nodiscard]] [[gnu::always_inline]]
constexpr F sigmoid(F x, F shape = 0) noexcept {
  return x / (shape + std::abs(x));
}

template <std::floating_point F>
[[nodiscard]] [[gnu::always_inline]]
F diode(F x, F vf = 0, F curve = 100) {
  // return 1 / (1 + exp(-curve * (x - vf)));
  return (tanh((curve * (x - vf)) * F(0.5)) + F(1)) * F(0.5);
}

// depth = 1でLinear to C Taper
template <std::floating_point F>
[[nodiscard]] [[gnu::always_inline]]
constexpr F saturate(F x, F depth) noexcept  // depth: 0~1, folder
{
  return x * (F(1) - depth + depth * (F(2) - std::abs(x)));
}

template <std::floating_point F>
[[nodiscard]] [[gnu::always_inline]]
constexpr F saturate_sig(F x, F depth) noexcept  // depth: 0~
{
  return x / (depth + std::abs(x)) * (F(1) + depth);
}

template <std::floating_point F>
[[nodiscard]] [[gnu::always_inline]]
constexpr F gclip(F x, F s) noexcept {  // s:0~1, 0で直線, 104 cycles
  // https://www.kvraudio.com/forum/viewtopic.php?t=483920&start=45
  // https://www.desmos.com/calculator/n9fmtv0dvy
  const auto abs_x = std::abs(x);
  if (abs_x <= (F(1) - s)) { return x; }
  const auto tmp = F(1) - min((abs_x - F(1) + s) / s, F(2)) * F(0.5);
  return sgn(x) * (s * (F(1) - tmp * tmp) + F(1) - s);  // 93 cycles
}

template <std::floating_point F>
[[nodiscard]] [[gnu::always_inline]]
constexpr F sqrt_clip(F x) noexcept {  // 81 cycles
  return x * inv_sqrt(F(1) + x * x);
}

template <std::floating_point F>
[[nodiscard]] [[gnu::always_inline]]
constexpr F saturate(F x) noexcept {
  return (F(2) * x * (F(1) - std::abs(x)));
}

// template <std::floating_point F>
// constexpr F clip(const F x) noexcept {
//   const auto abs_x = std::abs(x);
//   const auto two_x = x * x;
//   const auto numer = x
//                      * (1
//                         + abs_x
//                         + (static_cast<F>(1.05622909486427)
//                            + static_cast<F>(0.215166815390934)
//                                * two_x * abs_x)
//                             * two_x);
//   const auto denom = static_cast<F>(1.02718982441289) + std::abs(numer);
//   return numer / denom;
// }

template <std::floating_point F>
[[nodiscard]] [[gnu::always_inline]]
constexpr F clip(F x) noexcept {
  return clamp(x, F(-1.0), F(1.0));
}

template <std::floating_point F>
[[nodiscard]] [[gnu::always_inline]]
constexpr F fast_clip(F x) noexcept {
  return F(0.5) * x * (F(3) - x * x);
}

template <std::floating_point F>
[[nodiscard]] [[gnu::always_inline]]
constexpr F better_clip(F x) noexcept {  // 26 cycles
  if (1 < x) return fast_clip(F(1));
  if (x < -1) return fast_clip(F(-1));
  return fast_clip(x);
}

template <std::floating_point F>
[[nodiscard]] [[gnu::always_inline]]
constexpr F fastest_clip(F x) noexcept {
  return -x * (std::abs(x) - F(2));
}

template <std::floating_point F>
[[nodiscard]] [[gnu::always_inline]]
constexpr F better_clip1(F x) noexcept {  // 26 cycles
  if (1 < x) return fastest_clip(F(1));
  if (x < -1) return fastest_clip(F(-1));
  return fastest_clip(x);
}

template <std::floating_point F>
[[nodiscard]] [[gnu::always_inline]]
constexpr F better_clip2(F x) noexcept {  // 78 cycles
  x *= div(x);
  if (1 < x) return fastest_clip(F(1));
  if (x < -1) return fastest_clip(F(-1));
  return fastest_clip(fastest_clip(x));
}

}  // namespace cliping
}  // namespace mo