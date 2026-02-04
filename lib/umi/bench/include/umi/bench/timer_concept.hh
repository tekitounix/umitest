#pragma once

#include <concepts>
#include <cstdint>

namespace umi::bench {

template<typename T>
concept TimerLike = requires {
    typename T::Counter;
    { T::enable() } -> std::same_as<void>;
    { T::reset() } -> std::same_as<void>;
    { T::now() } -> std::same_as<typename T::Counter>;
};

} // namespace umi::bench
