#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <string_view>
#include <utility>

#include "stats.hh"
#include "timer_concept.hh"

namespace umi::bench {

template<TimerLike Timer>
class Runner {
public:
    using Counter = typename Timer::Counter;

    explicit Runner(std::string_view name = {}) : name_(name) {}

    template<std::size_t N, typename Func>
    Stats benchmark(Func&& func) const {
        std::array<Counter, N> samples{};
        for (std::size_t i = 0; i < N; i++) {
            samples[i] = measure(std::forward<Func>(func));
        }
        return compute_stats(samples);
    }

private:
    template<typename Func>
    static Counter measure(Func&& func) {
        Timer::reset();
        std::atomic_signal_fence(std::memory_order_seq_cst);
        const Counter start = Timer::now();
        std::atomic_signal_fence(std::memory_order_seq_cst);
        std::forward<Func>(func)();
        std::atomic_signal_fence(std::memory_order_seq_cst);
        const Counter end = Timer::now();
        return end - start;
    }

    std::string_view name_;
};

} // namespace umi::bench
