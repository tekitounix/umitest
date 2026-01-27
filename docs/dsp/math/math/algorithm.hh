#pragma once

#include <concepts>

namespace mo {
inline namespace algorithm {

template <typename F>
  requires std::totally_ordered<F>
[[nodiscard]] [[gnu::always_inline]]
constexpr F min(F a, F b) noexcept(noexcept(b < a)) {
  return b < a ? b : a;
}

template <typename F>
  requires std::totally_ordered<F>
[[nodiscard]] [[gnu::always_inline]]
constexpr F max(F a, F b) noexcept(noexcept(a < b)) {
  return a < b ? b : a;
}

template <typename F>
  requires std::totally_ordered<F>
[[nodiscard]] [[gnu::always_inline]]
constexpr F clamp(F x, F lo, F hi) noexcept(noexcept(min(max(x, lo), hi))) {
  return min(max(x, lo), hi);
}

}  // namespace algorithm
}  // namespace mo