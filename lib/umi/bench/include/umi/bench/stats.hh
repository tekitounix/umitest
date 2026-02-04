#pragma once

#include <array>
#include <cstdint>

namespace umi::bench {

struct Stats {
    std::uint64_t min = 0;
    std::uint64_t max = 0;
    std::uint64_t mean = 0;
    std::uint64_t median = 0;
};

template<typename Counter, std::size_t N>
Stats compute_stats(const std::array<Counter, N>& samples) {
    Stats stats;
    stats.min = samples[0];
    stats.max = samples[0];

    std::uint64_t sum = 0;
    for (auto value : samples) {
        if (value < stats.min) {
            stats.min = value;
        }
        if (value > stats.max) {
            stats.max = value;
        }
        sum += value;
    }

    stats.mean = sum / N;

    std::array<Counter, N> sorted = samples;
    for (std::size_t i = 1; i < N; i++) {
        Counter key = sorted[i];
        std::size_t j = i;
        while (j > 0 && sorted[j - 1] > key) {
            sorted[j] = sorted[j - 1];
            j--;
        }
        sorted[j] = key;
    }
    stats.median = sorted[N / 2];

    return stats;
}

} // namespace umi::bench
