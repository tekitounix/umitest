// SPDX-License-Identifier: MIT
// UMI-OS - Universal Musical Instruments Operating System
// Processor base class and descriptors

#pragma once

#include "types.hpp"
#include "audio_context.hpp"
#include <span>
#include <string_view>
#include <cstdint>

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
// Processor Requirements
// ============================================================================

/// Resource requirements for a processor
struct Requirements {
    uint32_t stack_size = 1024;     ///< Stack size in bytes
    uint32_t heap_size = 0;         ///< Heap requirement in bytes
    bool needs_control_thread = false;
};

// ============================================================================
// Processor Base Class
// ============================================================================

/// Base class for audio processors (RAII design)
/// 
/// Lifecycle:
/// 1. Construct with StreamConfig - allocate all resources
/// 2. process() - called for each audio buffer (real-time safe!)
/// 3. control() - called from control thread (optional)
/// 4. Destructor - release all resources
///
/// For sample rate changes: construct new instance, swap, destroy old.
/// Use reconfigure() only if in-place reconfiguration is truly needed.
class Processor {
public:
    /// Construct processor with stream configuration
    /// All resource allocation should happen here.
    /// For error handling without exceptions, use factory pattern in derived class:
    ///   static auto create(config) -> expected<unique_ptr<Derived>, Error>
    explicit Processor(const StreamConfig& config) 
        : config_(config) {}
    
    virtual ~Processor() = default;
    
    // Non-copyable, movable
    Processor(const Processor&) = delete;
    Processor& operator=(const Processor&) = delete;
    Processor(Processor&&) = default;
    Processor& operator=(Processor&&) = default;
    
    // === Processing ===
    
    /// Audio processing callback (REQUIRED)
    /// Called from audio thread at buffer rate
    /// NO heap allocation, NO blocking, NO system calls!
    virtual void process(AudioContext& ctx) = 0;
    
    /// Control processing callback (optional)
    /// Called from control thread, lower priority
    virtual void control(ControlContext& ctx) { (void)ctx; }
    
    // === Configuration ===
    
    /// Current stream configuration
    [[nodiscard]] const StreamConfig& config() const noexcept { return config_; }
    
    /// Reconfigure processor in-place (optional)
    /// Default implementation does nothing - override if in-place reconfigure is needed.
    /// For most cases, prefer constructing a new instance instead.
    virtual void reconfigure(const StreamConfig& new_config) { 
        config_ = new_config; 
    }
    
    // === Descriptors ===
    
    /// Return parameter descriptors
    virtual std::span<const ParamDescriptor> params() const { return {}; }
    
    /// Return port descriptors
    virtual std::span<const PortDescriptor> ports() const { return {}; }
    
    // === State management ===
    
    /// Save processor state to binary blob
    virtual size_t save_state(std::span<uint8_t> buffer) const { 
        (void)buffer; 
        return 0; 
    }
    
    /// Load processor state from binary blob
    virtual bool load_state(std::span<const uint8_t> data) { 
        (void)data; 
        return true; 
    }
    
    // === Requirements ===
    
    /// Return resource requirements (can be static)
    static Requirements requirements() { return {}; }
    
protected:
    StreamConfig config_;
};

} // namespace umi
