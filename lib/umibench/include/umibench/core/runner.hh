// SPDX-License-Identifier: MIT
#pragma once

/// @file
/// @brief Benchmark runner with baseline calibration and repeated sampling.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

#include "umibench/core/measure.hh"
#include "umibench/core/stats.hh"
#include "umibench/timer/concept.hh"

namespace umi::bench {

/// @brief Benchmark runner with baseline overhead calibration.
/// @tparam Timer Timer implementation that satisfies TimerLike.
/// @tparam DefaultSamples Default sample count for run/calibrate overloads.
template <TimerLike Timer, std::size_t DefaultSamples = 64>
class Runner {
  public:
    /// @brief Counter type returned by the timer.
    using Counter = typename Timer::Counter;

    /// @brief Construct runner with zero baseline.
    Runner() = default;

    /// @brief Calibrate baseline overhead using repeated empty measurements.
    /// @tparam N Number of calibration samples.
    /// @tparam Warmup Warmup iterations before sampling.
    /// @return Reference to this runner.
    /// @note Baseline is median-based for better outlier resistance.
    template <std::size_t N = DefaultSamples, std::size_t Warmup = 10>
    Runner& calibrate() {
        static_assert(N > 0, "Need at least one calibration sample");

        // Warmup iterations to stabilize cache and branch predictor
        for (std::size_t i = 0; i < Warmup; ++i) {
            volatile int dummy = 0;
            (void)measure<Timer>([&dummy] { dummy = 1; });
            (void)dummy;
        }

        std::array<Counter, N> samples{};
        for (std::size_t i = 0; i < N; ++i) {
            // Use volatile workaround to prevent optimization of empty lambda
            volatile int dummy = 0;
            samples[i] = measure<Timer>([&dummy] { dummy = 1; });
            (void)dummy;
        }

        // Sort to compute median (more robust than min for outlier resistance)
        std::array<Counter, N> sorted = samples;

        // Simple Insertion Sort to avoid std::sort dependency in bare-metal
        for (std::size_t i = 1; i < N; ++i) {
            const Counter key = sorted[i];
            std::size_t j = i;
            while (j > 0 && sorted[j - 1] > key) {
                sorted[j] = sorted[j - 1];
                --j;
            }
            sorted[j] = key;
        }

        // Use median instead of min for better outlier resistance
        if constexpr (N % 2 == 0) {
            const Counter lower = sorted[(N / 2) - 1];
            const Counter upper = sorted[N / 2];
            baseline = lower + ((upper - lower) / static_cast<Counter>(2));
        } else {
            baseline = sorted[N / 2];
        }

        return *this;
    }

    /// @brief Get current calibrated baseline.
    /// @return Current baseline overhead.
    [[nodiscard]] Counter get_baseline() const { return baseline; }

    /// @brief Run benchmark with one callable invocation per sample.
    /// @tparam N Number of samples.
    /// @tparam Func Callable type.
    /// @param func Callable to benchmark.
    /// @return Aggregated statistics for measured samples.
    template <std::size_t N = DefaultSamples, typename Func>
    Stats run(Func&& func) const {
        auto fn = std::forward<Func>(func);
        std::array<Counter, N> samples{};
        for (std::size_t i = 0; i < N; ++i) {
            samples[i] = measure_corrected<Timer>(fn, baseline);
        }
        return compute_stats(samples);
    }

    /// @brief Run benchmark with multiple callable invocations per sample.
    /// @tparam N Number of samples.
    /// @tparam Func Callable type.
    /// @param iterations Callable invocations per sample.
    /// @param func Callable to benchmark.
    /// @return Aggregated statistics for measured samples.
    template <std::size_t N = DefaultSamples, typename Func>
    Stats run(std::uint32_t iterations, Func&& func) const {
        auto fn = std::forward<Func>(func);
        std::array<Counter, N> samples{};
        for (std::size_t i = 0; i < N; ++i) {
            samples[i] = measure_corrected_n<Timer>(fn, baseline, iterations);
        }
        return compute_stats(samples, iterations);
    }

  private:
    Counter baseline = 0;
};

} // namespace umi::bench
