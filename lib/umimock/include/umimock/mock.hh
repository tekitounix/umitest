// SPDX-License-Identifier: MIT
// umimock - Mock signal generator for testing

#pragma once

#include <concepts>
#include <umimock/detail/impl.hh>

namespace umi::mock {

constexpr float default_value = 0.0f;
constexpr int max_ramp_steps = 100;

/// Shape of mock signal output.
enum class Shape {
    CONSTANT,
    RAMP,
};

/// Mock signal generator for testing.
class MockSignal {
  public:
    constexpr MockSignal() = default;

    constexpr MockSignal(Shape shape, float value) : shape(shape), value(value) {}

    /// Generate next sample value.
    constexpr float generate() {
        switch (shape) {
        case Shape::CONSTANT:
            return value;
        case Shape::RAMP:
            ++count;
            return detail::clamp01(static_cast<float>(count) / static_cast<float>(max_ramp_steps) * value);
        }
        return 0.0f;
    }

    void set_value(float value) { this->value = value; }

    void set_shape(Shape shape) { this->shape = shape; }

    float get_value() const { return value; }

    Shape get_shape() const { return shape; }

    void reset() {
        value = default_value;
        count = 0;
    }

  private:
    Shape shape = Shape::CONSTANT;
    float value = default_value;
    int count = 0;
};

/// Check if T has a generate() method returning float.
template <typename T>
concept Generatable = requires(T& t) {
    { t.generate() } -> std::same_as<float>;
};

/// Process N samples from a Generatable source into a buffer.
template <Generatable G>
void fill_buffer(G& gen, float* buf, int size) {
    for (int i = 0; i < size; ++i) {
        buf[i] = gen.generate();
    }
}

} // namespace umi::mock
