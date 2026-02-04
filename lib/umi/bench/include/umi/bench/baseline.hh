#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <utility>

#include "stats.hh"
#include "timer_concept.hh"

namespace umi::bench {

template<TimerLike Timer, typename Func>
typename Timer::Counter measure_inline(Func&& func) {
    Timer::reset();
    std::atomic_signal_fence(std::memory_order_seq_cst);
    const auto start = Timer::now();
    std::atomic_signal_fence(std::memory_order_seq_cst);
    std::forward<Func>(func)();
    std::atomic_signal_fence(std::memory_order_seq_cst);
    const auto end = Timer::now();
    return end - start;
}

template<TimerLike Timer>
struct Baseline {
    using Counter = typename Timer::Counter;

    template<std::size_t N>
    static Counter measure_min() {
        std::array<Counter, N> samples{};
        for (std::size_t i = 0; i < N; i++) {
            samples[i] = measure_inline<Timer>([] {});
        }
        return *std::min_element(samples.begin(), samples.end());
    }
};

template<TimerLike Timer, typename Func>
typename Timer::Counter measure_corrected(Func&& func, typename Timer::Counter baseline) {
    const auto measured = measure_inline<Timer>(std::forward<Func>(func));
    return (measured > baseline) ? (measured - baseline) : 0;
}

template<TimerLike Timer>
class InlineRunner {
public:
    using Counter = typename Timer::Counter;

    template<std::size_t N>
    void calibrate() {
        baseline_ = Baseline<Timer>::template measure_min<N>();
    }

    template<std::size_t N, typename Func>
    std::array<Counter, N> run_n_corrected(Func&& func) const {
        std::array<Counter, N> samples{};
        for (std::size_t i = 0; i < N; i++) {
            samples[i] = measure_corrected<Timer>(std::forward<Func>(func), baseline_);
        }
        return samples;
    }

    template<std::size_t N, typename Func>
    Stats benchmark_corrected(Func&& func) const {
        const auto samples = run_n_corrected<N>(std::forward<Func>(func));
        return compute_stats(samples);
    }

    Counter baseline() const { return baseline_; }

private:
    Counter baseline_ = 0;
};

} // namespace umi::bench
