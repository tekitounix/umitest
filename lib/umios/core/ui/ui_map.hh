// SPDX-License-Identifier: MIT
// UMI-OS - UI Map (Control-Parameter Binding)
//
// UIMap defines the mapping between UI controls and processor parameters.
// This layer enables:
// - Swappable skins (same controller, different views)
// - Hardware/Software unification (MIDI CC = UI knob)
// - MIDI Learn functionality
// - Value curve transformations

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <cmath>
#include <optional>
#include <span>
#include <vector>
#include <array>
#include <algorithm>

namespace umi {

// ============================================================================
// Value Curves - Transform between normalized [0,1] and display values
// ============================================================================

enum class Curve : uint8_t {
    LINEAR,      // Direct mapping
    LOG,         // Logarithmic (good for frequency, gain)
    EXP,         // Exponential
    SQUARE,      // Quadratic
    SQUARE_ROOT, // Square root
};

/// Apply curve transform: normalized [0,1] -> normalized [0,1]
constexpr float apply_curve(float normalized, Curve curve) noexcept {
    switch (curve) {
        case Curve::LINEAR:
            return normalized;
        case Curve::LOG:
            // Log curve: more resolution at low end
            return std::log10(1.0f + normalized * 9.0f) / std::log10(10.0f);
        case Curve::EXP:
            // Exp curve: more resolution at high end
            return (std::pow(10.0f, normalized) - 1.0f) / 9.0f;
        case Curve::SQUARE:
            return normalized * normalized;
        case Curve::SQUARE_ROOT:
            return std::sqrt(normalized);
    }
    return normalized;
}

/// Inverse curve transform: normalized [0,1] -> normalized [0,1]
constexpr float apply_curve_inverse(float normalized, Curve curve) noexcept {
    switch (curve) {
        case Curve::LINEAR:
            return normalized;
        case Curve::LOG:
            return (std::pow(10.0f, normalized) - 1.0f) / 9.0f;
        case Curve::EXP:
            return std::log10(1.0f + normalized * 9.0f) / std::log10(10.0f);
        case Curve::SQUARE:
            return std::sqrt(normalized);
        case Curve::SQUARE_ROOT:
            return normalized * normalized;
    }
    return normalized;
}

// ============================================================================
// Control Mapping - Bind UI control to processor parameter
// ============================================================================

struct ControlMapping {
    std::string_view control_id;  // UI control identifier (e.g., "knob_filter")
    uint32_t param_id;            // Processor parameter ID
    
    // Value transformation
    Curve curve = Curve::LINEAR;
    float display_min = 0.0f;
    float display_max = 1.0f;
    
    // Display formatting
    std::string_view unit;        // "Hz", "dB", "%", "ms"
    uint8_t decimal_places = 1;
    
    /// Convert normalized [0,1] to display value
    constexpr float to_display(float normalized) const noexcept {
        float curved = apply_curve(normalized, curve);
        return display_min + curved * (display_max - display_min);
    }
    
    /// Convert display value to normalized [0,1]
    constexpr float from_display(float display) const noexcept {
        float normalized = (display - display_min) / (display_max - display_min);
        normalized = std::clamp(normalized, 0.0f, 1.0f);
        return apply_curve_inverse(normalized, curve);
    }
    
    /// Format value for display (returns temporary, copy if needed)
    std::string format(float display_value) const;
};

// ============================================================================
// MIDI Mapping - Bind MIDI CC to processor parameter
// ============================================================================

struct MidiMapping {
    uint8_t channel = 0;          // MIDI channel (0 = omni)
    uint8_t cc_number;            // CC number (0-127)
    uint32_t param_id;            // Processor parameter ID
    
    bool is_learned = false;      // Set via MIDI Learn
    bool is_14bit = false;        // 14-bit CC (cc_number + cc_number+32)
    
    // Range limiting
    float min_value = 0.0f;       // Normalized min
    float max_value = 1.0f;       // Normalized max
    
    /// Convert CC value to normalized parameter value
    constexpr float cc_to_normalized(uint8_t cc_value) const noexcept {
        float normalized = cc_value / 127.0f;
        return min_value + normalized * (max_value - min_value);
    }
    
    /// Convert normalized value to CC value
    constexpr uint8_t normalized_to_cc(float normalized) const noexcept {
        float ranged = (normalized - min_value) / (max_value - min_value);
        return static_cast<uint8_t>(std::clamp(ranged * 127.0f, 0.0f, 127.0f));
    }
};

// ============================================================================
// UIMap - Control to Parameter mapping (UI concerns only)
// ============================================================================

/// Static UI Map for compile-time defined control mappings
template<size_t NumControls>
struct UIMap {
    std::array<ControlMapping, NumControls> controls;
    
    /// Find control mapping by control ID
    constexpr const ControlMapping* find_control(std::string_view id) const noexcept {
        for (const auto& c : controls) {
            if (c.control_id == id) return &c;
        }
        return nullptr;
    }
    
    /// Find control mapping by param ID
    constexpr const ControlMapping* find_by_param(uint32_t param_id) const noexcept {
        for (const auto& c : controls) {
            if (c.param_id == param_id) return &c;
        }
        return nullptr;
    }
};

// ============================================================================
// MidiMap - CC to Parameter mapping (MIDI concerns only, independent of UI)
// ============================================================================

/// Static MIDI Map for compile-time defined CC mappings
template<size_t NumMappings>
struct MidiMap {
    std::array<MidiMapping, NumMappings> mappings;
    
    /// Find MIDI mapping by CC
    constexpr const MidiMapping* find(uint8_t channel, uint8_t cc) const noexcept {
        for (const auto& m : mappings) {
            if ((m.channel == 0 || m.channel == channel) && m.cc_number == cc) {
                return &m;
            }
        }
        return nullptr;
    }
    
    /// Find MIDI mapping by param ID
    constexpr const MidiMapping* find_by_param(uint32_t param_id) const noexcept {
        for (const auto& m : mappings) {
            if (m.param_id == param_id) return &m;
        }
        return nullptr;
    }
};

/// Dynamic MIDI Map for runtime-configurable mappings (MIDI Learn, etc.)
class MidiMapDynamic {
public:
    void add(MidiMapping mapping) {
        mappings_.push_back(std::move(mapping));
    }
    
    /// MIDI Learn: bind next CC to parameter
    void start_learn(uint32_t param_id) {
        learning_param_ = param_id;
    }
    
    /// Called when CC received during learn mode
    bool learn(uint8_t channel, uint8_t cc) {
        if (!learning_param_) return false;
        
        // Remove existing binding for this CC
        std::erase_if(mappings_, [cc](const MidiMapping& m) {
            return m.cc_number == cc;
        });
        
        // Add new learned binding
        mappings_.push_back({
            .channel = channel,
            .cc_number = cc,
            .param_id = *learning_param_,
            .is_learned = true,
        });
        
        learning_param_.reset();
        return true;
    }
    
    void cancel_learn() {
        learning_param_.reset();
    }
    
    bool is_learning() const { return learning_param_.has_value(); }
    std::optional<uint32_t> learning_param() const { return learning_param_; }
    
    const MidiMapping* find(uint8_t channel, uint8_t cc) const {
        for (const auto& m : mappings_) {
            if ((m.channel == 0 || m.channel == channel) && m.cc_number == cc) {
                return &m;
            }
        }
        return nullptr;
    }
    
    std::span<const MidiMapping> mappings() const { return mappings_; }
    
private:
    std::vector<MidiMapping> mappings_;
    std::optional<uint32_t> learning_param_;
};

} // namespace umi
