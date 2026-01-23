#pragma once

#include <algorithm>
#include <cstddef>
#include <type_traits>
#include <vector>

#include "common.hh"

namespace detail {

struct MovingAverageCoeffs {
    size_t window_size = 1;
};

inline MovingAverageCoeffs make_moving_average_coeffs(size_t window_size) {
    if (window_size == 0) {
        window_size = 1;
    }
    return {window_size};
}

struct MovingAverageState {
    std::vector<float> buffer;
    size_t index = 0;
    size_t filled = 0;
    float sum = 0.0f;
};

inline void reset_moving_average(MovingAverageState& s, size_t window_size) {
    if (window_size == 0) {
        window_size = 1;
    }
    s.buffer.assign(window_size, 0.0f);
    s.index = 0;
    s.filled = 0;
    s.sum = 0.0f;
}

inline float process_moving_average(MovingAverageState& s, size_t window_size, float x) {
    if (window_size == 0) {
        return x;
    }
    if (s.buffer.size() != window_size) {
        reset_moving_average(s, window_size);
    }

    if (s.filled < window_size) {
        s.sum += x;
        s.buffer[s.index] = x;
        ++s.filled;
    } else {
        s.sum -= s.buffer[s.index];
        s.buffer[s.index] = x;
        s.sum += x;
    }

    s.index = (s.index + 1) % window_size;
    return s.sum / static_cast<float>(s.filled);
}

} // namespace detail

using MovingAverageOwnPolicy = OwnCoeffs<detail::MovingAverageCoeffs, detail::make_moving_average_coeffs>;
using MovingAverageSharedPolicy = SharedCoeffs<detail::MovingAverageCoeffs>;

template <typename CoeffSource>
class MovingAverage {
  public:
    MovingAverage()
        requires(!std::is_same_v<CoeffSource, MovingAverageSharedPolicy>)
    = default;
    explicit MovingAverage(const detail::MovingAverageCoeffs& shared)
        requires(std::is_same_v<CoeffSource, MovingAverageSharedPolicy>)
        : c(shared) {
        detail::reset_moving_average(s, shared.window_size);
    }

    void reset() {
        const auto& coeffs_ref = c.coeffs();
        detail::reset_moving_average(s, coeffs_ref.window_size);
    }

    void set_params(size_t window_size)
        requires requires(CoeffSource& c, size_t v) { c.set_params(v); }
    {
        c.set_params(window_size);
        const auto& coeffs_ref = c.coeffs();
        detail::reset_moving_average(s, coeffs_ref.window_size);
    }

    float process(float x)
        requires(!CoeffSource::needs_params)
    {
        const auto& coeffs_ref = c.coeffs();
        return detail::process_moving_average(s, coeffs_ref.window_size, x);
    }

  private:
    detail::MovingAverageState s{};
    CoeffSource c{};
};

using MovingAverageAsync = MovingAverage<MovingAverageOwnPolicy>;
using MovingAverageShared = MovingAverage<MovingAverageSharedPolicy>;
