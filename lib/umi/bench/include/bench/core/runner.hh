#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

#include "bench/core/measure.hh"
#include "bench/core/stats.hh"
#include "bench/timer/concept.hh"

namespace umi::bench {

/// Benchmark runner with automatic baseline calibration
template <TimerLike Timer, std::size_t DefaultSamples = 64>
class Runner {
  public:
    using Counter = typename Timer::Counter;

    Runner() = default;

    /// Calibrate baseline overhead with improved statistical reliability
    /// Uses warmup iterations, median instead of min, and volatile workaround
    template <std::size_t N = DefaultSamples, std::size_t Warmup = 10>
    Runner& calibrate() {
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
        std::sort(sorted.begin(), sorted.end());

        // Use median instead of min for better outlier resistance
        if constexpr (N % 2 == 0) {
            baseline = (sorted[N / 2 - 1] + sorted[N / 2]) / 2;
        } else {
            baseline = sorted[N / 2];
        }

        return *this;
    }

    /// Get current baseline value
    Counter get_baseline() const { return baseline; }

    /// Run benchmark: N samples, single iteration per sample
    template <std::size_t N = DefaultSamples, typename Func>
    Stats run(Func&& func) const {
        std::array<Counter, N> samples{};
        for (std::size_t i = 0; i < N; ++i) {
            samples[i] = measure_corrected<Timer>(std::forward<Func>(func), baseline);
        }
        return compute_stats(samples);
    }

    /// Run benchmark: N samples, multiple iterations per sample
    template <std::size_t N = DefaultSamples, typename Func>
    Stats run(std::uint32_t iterations, Func&& func) const {
        std::array<Counter, N> samples{};
        for (std::size_t i = 0; i < N; ++i) {
            samples[i] = measure_corrected_n<Timer>(std::forward<Func>(func), baseline, iterations);
        }
        return compute_stats(samples, iterations);
    }

  private:
    Counter baseline = 0;
};

} // namespace umi::bench
