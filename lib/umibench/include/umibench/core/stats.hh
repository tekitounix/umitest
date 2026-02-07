// SPDX-License-Identifier: MIT
#pragma once

/// @file
/// @brief Statistics primitives for benchmark sample aggregation.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace umi::bench {

/// @brief Aggregated benchmark statistics.
struct Stats {
    std::uint64_t min = 0;        ///< Minimum measured sample.
    std::uint64_t max = 0;        ///< Maximum measured sample.
    std::uint64_t median = 0;     ///< Median sample (average of middle two for even sample counts).
    std::uint32_t samples = 0;    ///< Number of collected samples.
    std::uint32_t iterations = 1; ///< Callable invocations per sample.

    double mean = 0.0;   ///< Arithmetic mean over all samples.
    double stddev = 0.0; ///< Standard deviation (population).
    double sum = 0.0;    ///< Sum of all samples.

    /// @brief Calculate coefficient of variation.
    /// @return Relative standard deviation in percent, or `0` when mean is `0`.
    [[nodiscard]] double cv() const {
        if (mean > 0.0) {
            return (stddev / mean) * 100.0;
        }
        return 0.0;
    }
};

namespace detail {

/// @brief In-place insertion sort for fixed-size arrays.
/// @tparam T Element type.
/// @tparam N Array size.
/// @param arr Array to sort.
template <typename T, std::size_t N>
void insertion_sort(std::array<T, N>& arr) {
    for (std::size_t i = 1; i < N; ++i) {
        const T key = arr[i];
        std::size_t j = i;
        while (j > 0 && arr[j - 1] > key) {
            arr[j] = arr[j - 1];
            --j;
        }
        arr[j] = key;
    }
}

/// @brief Compute overflow-safe midpoint of two values.
/// @tparam T Arithmetic type.
/// @param lower Lower value.
/// @param upper Upper value.
/// @return Midpoint between lower and upper.
template <typename T>
constexpr T median_of_two(T lower, T upper) {
    return lower + ((upper - lower) / static_cast<T>(2));
}

} // namespace detail

/// @brief Compute aggregate statistics from raw samples.
/// @tparam Counter Sample counter type.
/// @tparam N Number of samples in the input array.
/// @param samples Input samples.
/// @param iterations Callable invocations per sample.
/// @return Aggregated statistics.
template <typename Counter, std::size_t N>
Stats compute_stats(const std::array<Counter, N>& samples, std::uint32_t iterations = 1) {
    static_assert(N > 0, "Need at least one sample");

    Stats stats;
    stats.samples = static_cast<std::uint32_t>(N);
    stats.iterations = iterations;
    stats.min = samples[0];
    stats.max = samples[0];

    // Calculate sum and min/max in one pass
    double sum = 0.0;
    for (const auto value : samples) {
        if (value < stats.min) {
            stats.min = value;
        }
        if (value > stats.max) {
            stats.max = value;
        }
        sum += static_cast<double>(value);
    }
    stats.sum = sum;
    stats.mean = sum / static_cast<double>(N);

    // Sort for median
    std::array<Counter, N> sorted = samples;
    detail::insertion_sort(sorted);

    // Proper median: average of middle two for even N
    if constexpr (N % 2 == 0) {
        const auto lower = sorted[(N / 2) - 1];
        const auto upper = sorted[N / 2];
        stats.median = detail::median_of_two(lower, upper);
    } else {
        stats.median = sorted[N / 2];
    }

    // Calculate standard deviation
    double variance_sum = 0.0;
    for (const auto value : samples) {
        const double diff = static_cast<double>(value) - stats.mean;
        variance_sum += diff * diff;
    }
    stats.stddev = std::sqrt(variance_sum / static_cast<double>(N));

    return stats;
}

} // namespace umi::bench
