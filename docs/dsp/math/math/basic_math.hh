#pragma once

#include <bit>
#include <cmath>  // https://github.com/gcc-mirror/gcc/blob/master/libstdc%2B%2B-v3/include/c_global/cmath
#include <concepts>
#include "mo.hh"
#include "algorithm.hh"
#include "numbers.hh"

namespace mo {
inline namespace math {
/*
[Cortex-M4]
+: VADD.F32 (1)
-: VSUB.F32 (1)
×: VMUL.F32 (1)
÷: DIV.F32 (14)
|x|: VABS.F32 (1)
√x: VSQRT.F32 (14)
std::copysign: __builtin_copysign
 */

template <typename T>
[[nodiscard]] [[gnu::always_inline]]
constexpr T sgn(T x) {  // 24 cycles
  if (T(0) < x) {
    return T(1);
  } else if (x < T(0)) {
    return T(-1);
  } else {
    return T(0);
  }
}

template <std::floating_point F>
[[nodiscard]] [[gnu::always_inline]]
constexpr F div(F x) noexcept {
  return F(1) / x;
}

template <std::floating_point F>
[[nodiscard]] [[gnu::always_inline]]
constexpr F inv_sqrt(F x) noexcept {
  return F(1) / std::sqrt(x);
}

// sin(x) { -π < x < π }
template <std::floating_point F>
[[nodiscard]] [[gnu::always_inline]]
constexpr F fast_sin(F x) noexcept {
  const auto abx = std::abs(x);
  const auto num = F(16) * x * (mo::pi<F> - abx);
  const auto den = F(5) * mo::pi<F> * mo::pi<F> - F(4) * abx * (mo::pi<F> - abx);
  return num / den;
}

// sin(x) { -π < x < π }
template <std::floating_point F>
[[nodiscard]] [[gnu::always_inline]]
constexpr F ultra_fast_sin(F x) noexcept {
  constexpr F constant = F(4) / (mo::pi<F> * mo::pi<F>);
  return x * constant * (mo::pi<F> - std::abs(x));
}

template <std::floating_point F>
[[nodiscard]] [[gnu::always_inline]]
constexpr F sin(F x) noexcept {
  const auto x2 = x * x;
  const auto num = -x * (F(-11511339840) + x2 * (F(1640635920) + x2 * (F(-52785432) + x2 * F(479249))));
  const auto den = F(11511339840) + x2 * (F(277920720) + x2 * (F(3177720) + x2 * F(18361)));
  return num / den;
}

template <std::floating_point F>
[[nodiscard]] [[gnu::always_inline]]
constexpr F cos(F x) noexcept {
  const auto x2 = x * x;
  const auto num = -(F(-39251520) + x2 * (F(18471600) + x2 * (F(-1075032) + F(14615) * x2)));
  const auto den = F(39251520) + x2 * (F(1154160) + x2 * (F(16632) + x2 * F(127)));
  return num / den;
}

template <std::floating_point F>
[[nodiscard]] [[gnu::always_inline]]
constexpr F tan(F x) noexcept {
  const auto x2 = x * x;
  const auto num = x * (F(-135135) + x2 * (F(17325) + x2 * (F(-378) + x2)));
  const auto den = F(-135135) + x2 * (F(62370) + x2 * (F(-3150) + F(28) * x2));
  return num / den;
}

template <std::floating_point F>
[[nodiscard]] [[gnu::always_inline]]
constexpr F exp(F x) noexcept {
  const auto num = F(1680) + x * (F(840) + x * (F(180) + x * (F(20) + x)));
  const auto den = F(1680) + x * (F(-840) + x * (F(180) + x * (F(-20) + x)));
  return num / den;
}

template <std::floating_point F>
[[nodiscard]] [[gnu::always_inline]]
constexpr F sinh(F x) noexcept {
  const auto x2 = x * x;
  const auto num = -x * (F(11511339840) + x2 * (F(1640635920) + x2 * (F(52785432) + x2 * F(479249))));
  const auto den = F(-11511339840) + x2 * (F(277920720) + x2 * (F(-3177720) + x2 * F(18361)));
  return num / den;
}

template <std::floating_point F>
[[nodiscard]] [[gnu::always_inline]]
constexpr F cosh(F x) noexcept {
  const auto x2 = x * x;
  const auto num = -(F(39251520) + x2 * (F(18471600) + x2 * (F(1075032) + F(14615) * x2)));
  const auto den = F(-39251520) + x2 * (F(1154160) + x2 * (F(-16632) + F(127) * x2));
  return num / den;
}

// 0~1あたりの範囲ではかなり正確だが大きい値で1を超えるので注意、clamp併用するといいかも
// template <std::floating_point F>
// [[nodiscard]] [[gnu::always_inline]]
// constexpr F juce_tanh(F x) noexcept {
//   auto x2 = x * x;
//   auto num = x * (F(135135) + x2 * (F(17325) + x2 * (F(378) + x2)));
//   auto den = F(135135) + x2 * (F(62370) + x2 * (F(3150) + F(28) * x2));
//   return num / den;
// }

// 0~1あたりの範囲ではいくらか誤差があるがx=3あたりから1になり、ほとんど1を超えない。
template <std::floating_point F>
[[nodiscard]] [[gnu::always_inline]]
constexpr F tanh(F x) noexcept {  // 39cycles
  constexpr F div = F(1) / F(6.8);
  x = x * div;
  x = std::abs(x + F(0.5)) - std::abs(x - F(0.5));
  x = (std::abs(x) - F(2)) * x;
  return (std::abs(x) - F(2)) * x;
}

template <std::floating_point F>
[[nodiscard]] [[gnu::always_inline]]
constexpr F qtanh(F x) noexcept {  // 88cycles
  x = x * F(0.25);
  const auto a = std::abs(x);
  const auto x2 = x * x;
  const auto y = F(1) - F(1) / (F(1) + a + x2 + F(0.66422417311781) * x2 * a + F(0.36483285408241) * x2 * x2);
  return (0 <= x) ? y : -y;
}

template <std::floating_point F>
[[nodiscard]] [[gnu::always_inline]]
constexpr F pow(F x, F y) noexcept {  // 未検証
  if constexpr (std::is_same_v<F, float>) {
    int i = std::bit_cast<int>(x);
    // i = static_cast<int>(y * (i - 1065307417) + 1065307417);
    // i = static_cast<int>(y * (i - 1072632447) + 1072632447);
    i = static_cast<int>(y * (i - 1064866808) + 1064866808);
    return std::bit_cast<float>(i);
  } else {
    int64_t i = std::bit_cast<int64_t>(x);
    int high = static_cast<int>((i >> 32) & 0xFFFFFFFF);
    high = static_cast<int>(y * (high - 1072632447) + 1072632447);
    i = static_cast<int64_t>(high) << 32;
    return std::bit_cast<double>(i);
  }
}

template <std::floating_point F>
[[nodiscard]] [[gnu::always_inline]]
constexpr F pow2(F x) noexcept {
  if constexpr (std::is_same_v<F, float>) {
    float f = static_cast<float>((x - 0.5f) + (3 << 22));
    int32_t i = std::bit_cast<int32_t>(f);
    const int32_t ix = i - 0x4b400000;
    const float dx = static_cast<float>(x - static_cast<float>(ix));
    x = 1.0f + dx * (0.6960656421638072f + dx * (0.224494337302845f + dx * 0.07944023841053369f));
    i = std::bit_cast<int32_t>(static_cast<float>(x));
    i += (ix << 23);
    return std::bit_cast<float>(i);
  } else {
    double d = x + 1023.0;
    int64_t i = std::bit_cast<int64_t>(d);
    int64_t ix = i & 0x7FF0000000000000LL;
    i = (i & 0x000FFFFFFFFFFFFFLL) | 0x3FF0000000000000LL;
    x = std::bit_cast<double>(i) - 1.0;
    double y = 1.0 + x * (0.693147180559945309417232121458176568 + x * (0.240226506959100712333551263163332722 + x * 0.0555041086648215799531422637686218426));
    i = ix + (static_cast<int64_t>(y * 1024.0) << 52);
    return std::bit_cast<double>(i);
  }
}

template <std::floating_point F>
[[nodiscard]] [[gnu::always_inline]]
constexpr F pow10(F x) noexcept {
  return mo::exp<F>(F(2.302585092994046) * x);
}

// From https://gist.github.com/LingDong-/7e4c4cae5cbbc44400a05fba65f06f23
template <std::floating_point F>
[[nodiscard]] [[gnu::always_inline]]
constexpr F ln(F x) noexcept {  // 63 cycles
  if constexpr (std::is_same_v<F, float>) {
    auto i = std::bit_cast<uint32_t>(x);
    auto t = static_cast<int32_t>(i >> 23) - 127;
    i = 0x3f800000 | (i & 0x007fffff);
    x = std::bit_cast<float>(i);
    return F(-1.49278) + (F(2.11263) + (F(-0.729104) + F(0.10969) * x) * x) * x + F(0.6931471806) * t;
  } else {
    return std::log(x);
  }
}

template <std::floating_point F>
[[nodiscard]] [[gnu::always_inline]]
constexpr F log2(F x) noexcept {  //! double用の実装は未確認
  const auto i = std::bit_cast<uint32_t>(x);
  auto y = static_cast<F>(i);
  // y *= F(1) / (1 << 23);
  y *= F(1.1920928955078125e-7);
  return y - F(126.94269504);
}

template <std::floating_point F>
[[nodiscard]] [[gnu::always_inline]]
constexpr F log10(F x) noexcept {
  return mo::log2(x) * (std::numbers::log10e_v<F> / std::numbers::log2e_v<F>);
}

template <std::floating_point F>
[[nodiscard]] [[gnu::always_inline]]
constexpr F log(F x) noexcept {  // 20 cycles
  const auto i = std::bit_cast<uint32_t>(x);
  auto y = static_cast<F>(i);
  y *= F(8.2629582881927490e-8);
  return y - F(87.989971088);
}

// ln(x+1) { -0.8 < x < 5 }
template <std::floating_point F>
[[nodiscard]] [[gnu::always_inline]]
constexpr F log1p(F x) noexcept {
  const auto num = x * (F(7560) + x * (F(15120) + x * (F(9870) + x * (F(2310) + x * F(137)))));
  const auto den = F(7560) + x * (F(18900) + x * (F(16800) + x * (F(6300) + x * (F(900) + F(30) * x))));
  return num / den;
}

// From http://www.machinedlearnings.com/2011/06/fast-approximate-logarithm-exponential.html
template <std::floating_point F>
[[nodiscard]] [[gnu::always_inline]]
constexpr F asinh(F x) noexcept {
  return mo::log(x + std::sqrt(x * x + F(1)));
}

template <std::floating_point F>
[[nodiscard]] [[gnu::always_inline]]
constexpr F acosh(F x) noexcept {
  return mo::log(x + std::sqrt(x * x - F(1)));
}

template <std::floating_point F>
[[nodiscard]] [[gnu::always_inline]]
constexpr F atanh(F x) noexcept {
  return mo::log((F(1) + x) / (F(1) - x)) / F(2);
}

template <std::floating_point F>
[[nodiscard]] [[gnu::always_inline]]
constexpr F acoth(F x) noexcept {
  return mo::log((F(1) - x) / (F(1) + x)) / F(2);
}

template <std::floating_point F>
[[nodiscard]] [[gnu::always_inline]]
constexpr F asech(F x) noexcept {
  return mo::acosh(F(1) / x);
}

template <std::floating_point F>
[[nodiscard]] [[gnu::always_inline]]
constexpr F acosech(F x) noexcept {
  return mo::asinh(F(1) / x);
}

template <std::floating_point F>
[[nodiscard]] [[gnu::always_inline]]
constexpr F lerp(F a, F b, F t) noexcept {
  return a + t * (b - a);  // 22sycles
}
}  // namespace math
}  // namespace mo
