// SPDX-License-Identifier: MIT
// UMI-OS - Universal Musical Instruments Operating System
// Processor concepts and type-erased wrapper

#pragma once

#include "types.hh"
#include "audio_context.hh"
#include <span>
#include <string_view>
#include <cstdint>
#include <memory>
#include <utility>

namespace umi {

// ============================================================================
// Port Descriptor
// ============================================================================

/// Port direction
enum class PortDirection : uint8_t {
    IN,
    OUT,
};

/// Port kind
enum class PortKind : uint8_t {
    CONTINUOUS,  ///< Audio, CV - fixed sample rate
    EVENT,       ///< MIDI, OSC, Parameters - variable timing
};

/// Event type hint for event ports
enum class TypeHint : uint16_t {
    UNKNOWN      = 0x0000,

    // MIDI
    MIDI_BYTES   = 0x0100,
    MIDI_UMP     = 0x0101,
    MIDI_SYSEX   = 0x0102,

    // Parameters
    PARAM_CHANGE  = 0x0200,
    PARAM_GESTURE = 0x0201,

    // Network/Serial
    OSC          = 0x0300,
    SERIAL       = 0x0301,

    // System
    CLOCK        = 0x0400,
    TRANSPORT    = 0x0401,

    // User defined (0x8000+)
    USER_DEFINED  = 0x8000,
};

/// Port descriptor
struct PortDescriptor {
    port_id_t id = 0;
    std::string_view name;
    PortKind kind = PortKind::CONTINUOUS;
    PortDirection dir = PortDirection::IN;
    
    // For Continuous ports
    uint32_t channels = 1;
    
    // For Event ports
    TypeHint type_hint = TypeHint::UNKNOWN;
};

// ============================================================================
// Parameter Descriptor
// ============================================================================

/// Parameter curve type for UI scaling
enum class ParamCurve : uint8_t {
    LINEAR = 0,  // Linear mapping (default)
    LOG,         // Logarithmic (good for frequency, gain, time)
    EXP,         // Exponential (inverse of log)
    AUTO = 255,  // Auto-detect based on name and range
};

/// Infer optimal curve from parameter characteristics
/// Rules:
///   - Frequency params (Hz range, "freq/cutoff/pitch"): Log
///   - Time params (ms range, "attack/decay/release/time"): Log
///   - Gain/volume with wide range (>10x): Log
///   - 0-1 normalized params: Linear
///   - Everything else: Linear
[[nodiscard]] constexpr ParamCurve infer_param_curve(
    std::string_view name, float min_val, float max_val) noexcept {

    // Helper: case-insensitive contains
    auto contains = [](std::string_view haystack, std::string_view needle) {
        if (needle.size() > haystack.size()) return false;
        for (size_t i = 0; i <= haystack.size() - needle.size(); ++i) {
            bool match = true;
            for (size_t j = 0; j < needle.size() && match; ++j) {
                char h = haystack[i + j];
                char n = needle[j];
                // Simple lowercase
                if (h >= 'A' && h <= 'Z') h += 32;
                if (n >= 'A' && n <= 'Z') n += 32;
                if (h != n) match = false;
            }
            if (match) return true;
        }
        return false;
    };

    // Frequency parameters (typically 20-20000 Hz range)
    if (contains(name, "freq") || contains(name, "cutoff") ||
        contains(name, "pitch") || contains(name, "hz")) {
        return ParamCurve::LOG;
    }

    // Time parameters (typically ms range)
    if (contains(name, "attack") || contains(name, "decay") ||
        contains(name, "release") || contains(name, "time") ||
        contains(name, "delay") || contains(name, "rate")) {
        // Only use log for time ranges > 10x
        if (max_val > min_val * 10.0f && min_val > 0) {
            return ParamCurve::LOG;
        }
    }

    // Gain/volume with large range
    if (contains(name, "gain") || contains(name, "level")) {
        if (max_val > min_val * 10.0f && min_val > 0) {
            return ParamCurve::LOG;
        }
    }

    // Default: linear for normalized 0-1 params, percentages, etc.
    return ParamCurve::LINEAR;
}

/// Parameter descriptor
struct ParamDescriptor {
    param_id_t id = 0;
    std::string_view name;
    float default_value = 0.0f;
    float min_value = 0.0f;
    float max_value = 1.0f;
    ParamCurve curve = ParamCurve::AUTO;  // Auto-detect by default

    /// Get effective curve (resolves Auto)
    [[nodiscard]] constexpr ParamCurve effective_curve() const noexcept {
        if (curve == ParamCurve::AUTO) {
            return infer_param_curve(name, min_value, max_value);
        }
        return curve;
    }

    /// Normalize value to 0-1 range (applies curve)
    [[nodiscard]] constexpr float normalize(float value) const noexcept {
        if (max_value == min_value) return 0.0f;

        const auto c = effective_curve();
        switch (c) {
        case ParamCurve::LOG: {
            // Log scale: map [min, max] to [0, 1] logarithmically
            // Avoid log(0) by using small offset
            const float min_log = (min_value > 0) ? min_value : 1.0f;
            const float log_min = log_approx(min_log);
            const float log_max = log_approx(max_value);
            const float log_val = log_approx(value > min_log ? value : min_log);
            return (log_val - log_min) / (log_max - log_min);
        }
        case ParamCurve::EXP: {
            // Exponential: square the linear value
            float linear = (value - min_value) / (max_value - min_value);
            return linear * linear;
        }
        default: // Linear (and Auto fallback)
            return (value - min_value) / (max_value - min_value);
        }
    }

    /// Denormalize from 0-1 range (applies curve)
    [[nodiscard]] constexpr float denormalize(float normalized) const noexcept {
        const auto c = effective_curve();
        switch (c) {
        case ParamCurve::LOG: {
            // Log scale: exponential mapping
            const float min_log = (min_value > 0) ? min_value : 1.0f;
            const float log_min = log_approx(min_log);
            const float log_max = log_approx(max_value);
            const float log_val = log_min + normalized * (log_max - log_min);
            return exp_approx(log_val);
        }
        case ParamCurve::EXP: {
            // Exponential: sqrt of normalized value
            float linear = (normalized > 0) ? sqrt_approx(normalized) : 0.0f;
            return min_value + linear * (max_value - min_value);
        }
        default: // Linear
            return min_value + normalized * (max_value - min_value);
        }
    }

    /// Clamp value to valid range
    [[nodiscard]] constexpr float clamp(float value) const noexcept {
        if (value < min_value) return min_value;
        if (value > max_value) return max_value;
        return value;
    }

private:
    // Simple approximations for constexpr context
    [[nodiscard]] static constexpr float log_approx(float x) noexcept {
        // Natural log approximation using series expansion
        // For the range we care about (20-20000), this is sufficient
        if (x <= 0) return -10.0f;

        // Normalize to [1, 2) range and count powers of 2
        float result = 0.0f;
        while (x >= 2.0f) { x *= 0.5f; result += 0.693147f; }  // ln(2)
        while (x < 1.0f)  { x *= 2.0f; result -= 0.693147f; }

        // Taylor series for ln(1+y) where y = x-1, |y| < 1
        float y = x - 1.0f;
        float term = y;
        result += term;
        term *= -y; result += term * 0.5f;
        term *= -y; result += term * 0.333333f;
        term *= -y; result += term * 0.25f;
        return result;
    }

    [[nodiscard]] static constexpr float exp_approx(float x) noexcept {
        // Exponential approximation
        // Reduce to e^(n*ln2 + r) = 2^n * e^r where |r| < ln(2)/2
        if (x > 10.0f) return 22026.0f;  // Cap at e^10
        if (x < -10.0f) return 0.0f;

        // Count integer part
        float result = 1.0f;
        while (x >= 0.693147f) { x -= 0.693147f; result *= 2.0f; }
        while (x < 0.0f)       { x += 0.693147f; result *= 0.5f; }

        // Taylor series for e^x where |x| < ln(2)
        float term = 1.0f;
        float sum = 1.0f;
        term *= x; sum += term;              // x
        term *= x * 0.5f; sum += term;       // x^2/2!
        term *= x * 0.333333f; sum += term;  // x^3/3!
        term *= x * 0.25f; sum += term;      // x^4/4!
        return result * sum;
    }

    [[nodiscard]] static constexpr float sqrt_approx(float x) noexcept {
        if (x <= 0) return 0.0f;
        // Newton-Raphson
        float guess = x * 0.5f;
        for (int i = 0; i < 5; ++i) {
            guess = 0.5f * (guess + x / guess);
        }
        return guess;
    }
};

// ============================================================================
// Processor Concepts
// ============================================================================

/// Core concept: anything with process(AudioContext&)
template<typename T>
concept ProcessorLike = requires(T& p, AudioContext& ctx) {
    { p.process(ctx) } -> std::same_as<void>;
};

/// Optional: has control(ControlContext&)
template<typename T>
concept Controllable = ProcessorLike<T> && requires(T& p, ControlContext& ctx) {
    { p.control(ctx) } -> std::same_as<void>;
};

/// Optional: has params()
template<typename T>
concept HasParams = requires(const T& p) {
    { p.params() } -> std::convertible_to<std::span<const ParamDescriptor>>;
};

/// Optional: has ports()
template<typename T>
concept HasPorts = requires(const T& p) {
    { p.ports() } -> std::convertible_to<std::span<const PortDescriptor>>;
};

/// Optional: has state save/load
template<typename T>
concept Stateful = requires(T& p, std::span<uint8_t> buf, std::span<const uint8_t> data) {
    { p.save_state(buf) } -> std::convertible_to<size_t>;
    { p.load_state(data) } -> std::convertible_to<bool>;
};

// ============================================================================
// Type-Erased Processor (for dynamic dispatch when needed)
// ============================================================================

/// Type-erased processor wrapper
/// Use this when you need runtime polymorphism (plugins, testing, etc.)
/// For embedded with single processor, use concepts directly for zero overhead.
class AnyProcessor {
public:
    /// Construct from any ProcessorLike type
    template<ProcessorLike P>
    explicit AnyProcessor(P& proc)
        : impl_(&proc)
        , process_fn_([](void* p, AudioContext& ctx) {
            static_cast<P*>(p)->process(ctx);
        })
        , control_fn_(make_control_fn<P>())
    {}
    
    /// Construct from unique_ptr (takes ownership)
    template<ProcessorLike P>
    explicit AnyProcessor(std::unique_ptr<P> proc)
        : owned_(proc.release(), [](void* p) { delete static_cast<P*>(p); })
        , impl_(owned_.get())
        , process_fn_([](void* p, AudioContext& ctx) {
            static_cast<P*>(p)->process(ctx);
        })
        , control_fn_(make_control_fn<P>())
    {}
    
    void process(AudioContext& ctx) {
        process_fn_(impl_, ctx);
    }
    
    void control(ControlContext& ctx) {
        if (control_fn_) {
            control_fn_(impl_, ctx);
        }
    }
    
    [[nodiscard]] bool has_control() const noexcept {
        return control_fn_ != nullptr;
    }
    
private:
    template<typename P>
    static auto make_control_fn() -> void(*)(void*, ControlContext&) {
        if constexpr (Controllable<P>) {
            return [](void* p, ControlContext& ctx) {
                static_cast<P*>(p)->control(ctx);
            };
        } else {
            return nullptr;
        }
    }
    
    std::unique_ptr<void, void(*)(void*)> owned_{nullptr, [](void*){}};
    void* impl_ = nullptr;
    void (*process_fn_)(void*, AudioContext&) = nullptr;
    void (*control_fn_)(void*, ControlContext&) = nullptr;
};

// ============================================================================
// Helper Functions
// ============================================================================

/// Process a single buffer (inlined for embedded)
template<ProcessorLike P>
inline void process_once(P& proc, AudioContext& ctx) {
    proc.process(ctx);
}

/// Process with optional control
template<ProcessorLike P>
inline void process_with_control(P& proc, AudioContext& audio_ctx, ControlContext& ctrl_ctx) {
    proc.process(audio_ctx);
    if constexpr (Controllable<P>) {
        proc.control(ctrl_ctx);
    }
}

} // namespace umi
