// SPDX-License-Identifier: MIT
#pragma once

#include <atomic>
#include <cstdint>
#include <utility>

#include "bench/timer/concept.hh"

namespace umi::bench {

/// Measure a single execution of func, returning elapsed cycles/time
/// Uses acquire/release memory ordering (sufficient for most cases, less overhead than seq_cst)
/// and includes compiler barrier to prevent optimization of measurement boundaries.
template <TimerLike Timer, typename Func>
typename Timer::Counter measure(Func&& func) {
    // Compiler barrier to prevent reordering of timer reads
    std::atomic_signal_fence(std::memory_order_acquire);
    const auto start = Timer::now();
    std::atomic_signal_fence(std::memory_order_release);
    std::forward<Func>(func)();
    std::atomic_signal_fence(std::memory_order_acquire);
    const auto end = Timer::now();
    std::atomic_signal_fence(std::memory_order_release);
    return end - start;
}

/// Measure func executed 'iterations' times
template <TimerLike Timer, typename Func>
typename Timer::Counter measure_n(Func&& func, std::uint32_t iterations) {
    return measure<Timer>([&func, iterations] {
        for (std::uint32_t i = 0; i < iterations; ++i) {
            func();
        }
    });
}

/// Measure with baseline subtraction
template <TimerLike Timer, typename Func>
typename Timer::Counter measure_corrected(Func&& func, typename Timer::Counter baseline) {
    const auto measured = measure<Timer>(std::forward<Func>(func));
    return (measured > baseline) ? (measured - baseline) : 0;
}

/// Measure N iterations with baseline subtraction
template <TimerLike Timer, typename Func>
typename Timer::Counter measure_corrected_n(Func&& func, typename Timer::Counter baseline, std::uint32_t iterations) {
    const auto measured = measure_n<Timer>(std::forward<Func>(func), iterations);
    return (measured > baseline) ? (measured - baseline) : 0;
}

} // namespace umi::bench
