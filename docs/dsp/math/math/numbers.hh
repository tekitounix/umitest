#pragma once

#include <concepts>
#include <numbers>

namespace mo {
inline namespace numbers {
template <std::floating_point F>
inline constexpr F e = std::numbers::e_v<F>;
template <std::floating_point F>
inline constexpr F log2e = std::numbers::log2e_v<F>;
template <std::floating_point F>
inline constexpr F log10e = std::numbers::log10e_v<F>;
template <std::floating_point F>
inline constexpr F pi = std::numbers::pi_v<F>;
template <std::floating_point F>
inline constexpr F tau = std::numbers::pi_v<F> * F(2);
template <std::floating_point F>
inline constexpr F half_pi = std::numbers::pi_v<F> / F(2);
template <std::floating_point F>
inline constexpr F inv_pi = std::numbers::inv_pi_v<F>;
template <std::floating_point F>
inline constexpr F inv_sqrtpi = std::numbers::inv_sqrtpi_v<F>;
template <std::floating_point F>
inline constexpr F ln2 = std::numbers::ln2_v<F>;
template <std::floating_point F>
inline constexpr F ln10 = std::numbers::ln10_v<F>;
template <std::floating_point F>
inline constexpr F sqrt2 = std::numbers::sqrt2_v<F>;
template <std::floating_point F>
inline constexpr F sqrt3 = std::numbers::sqrt3_v<F>;
template <std::floating_point F>
inline constexpr F inv_sqrt2 = F(1) / std::numbers::sqrt2_v<F>;
template <std::floating_point F>
inline constexpr F inv_sqrt3 = std::numbers::inv_sqrt3_v<F>;
template <std::floating_point F>
inline constexpr F egamma = std::numbers::egamma_v<F>;
template <std::floating_point F>
inline constexpr F phi = std::numbers::phi_v<F>;
}  // namespace numbers
}  // namespace mo
