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
    In,
    Out,
};

/// Port kind
enum class PortKind : uint8_t {
    Continuous,  ///< Audio, CV - fixed sample rate
    Event,       ///< MIDI, OSC, Parameters - variable timing
};

/// Event type hint for event ports
enum class TypeHint : uint16_t {
    Unknown      = 0x0000,
    
    // MIDI
    MidiBytes    = 0x0100,
    MidiUmp      = 0x0101,
    MidiSysex    = 0x0102,
    
    // Parameters
    ParamChange  = 0x0200,
    ParamGesture = 0x0201,
    
    // Network/Serial
    Osc          = 0x0300,
    Serial       = 0x0301,
    
    // System
    Clock        = 0x0400,
    Transport    = 0x0401,
    
    // User defined (0x8000+)
    UserDefined  = 0x8000,
};

/// Port descriptor
struct PortDescriptor {
    port_id_t id = 0;
    std::string_view name;
    PortKind kind = PortKind::Continuous;
    PortDirection dir = PortDirection::In;
    
    // For Continuous ports
    uint32_t channels = 1;
    
    // For Event ports
    TypeHint type_hint = TypeHint::Unknown;
};

// ============================================================================
// Parameter Descriptor
// ============================================================================

/// Parameter descriptor
struct ParamDescriptor {
    param_id_t id = 0;
    std::string_view name;
    float default_value = 0.0f;
    float min_value = 0.0f;
    float max_value = 1.0f;
    
    /// Normalize value to 0-1 range
    [[nodiscard]] constexpr float normalize(float value) const noexcept {
        if (max_value == min_value) return 0.0f;
        return (value - min_value) / (max_value - min_value);
    }
    
    /// Denormalize from 0-1 range
    [[nodiscard]] constexpr float denormalize(float normalized) const noexcept {
        return min_value + normalized * (max_value - min_value);
    }
    
    /// Clamp value to valid range
    [[nodiscard]] constexpr float clamp(float value) const noexcept {
        if (value < min_value) return min_value;
        if (value > max_value) return max_value;
        return value;
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
