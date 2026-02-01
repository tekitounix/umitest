// SPDX-License-Identifier: MIT
// UMI-OS Parameter Mapping
// Maps MIDI CC and hardware inputs to denormalized parameter values

#pragma once

#include "shared_state.hh"
#include <cstdint>

namespace umi {

// ============================================================================
// Parameter Map Entry
// ============================================================================

/// Maps a MIDI CC or hardware input to a parameter slot with range
struct ParamMapEntry {
    uint8_t param_id;       ///< Target parameter index (0-31), 0xFF = unmapped
    uint8_t _pad[3]{};
    float range_low = 0.0f; ///< Output range minimum
    float range_high = 1.0f; ///< Output range maximum

    /// Check if this entry is mapped
    [[nodiscard]] constexpr bool is_mapped() const noexcept { return param_id != 0xFF; }

    /// Denormalize a 0.0-1.0 normalized value to the configured range
    [[nodiscard]] constexpr float denormalize(float normalized) const noexcept {
        return range_low + normalized * (range_high - range_low);
    }
};

static_assert(sizeof(ParamMapEntry) == 12);

// ============================================================================
// CC → Parameter Mapping (128 entries)
// ============================================================================

/// Maps MIDI CC numbers (0-127) to parameter slots
struct ParamMapping {
    ParamMapEntry entries[128]; ///< CC# → param mapping

    /// Look up mapping for a CC number
    [[nodiscard]] const ParamMapEntry& operator[](uint8_t cc_num) const noexcept {
        return entries[cc_num];
    }

    /// Create an empty mapping (all unmapped)
    static constexpr ParamMapping make_empty() noexcept {
        ParamMapping m{};
        for (auto& e : m.entries) {
            e.param_id = 0xFF;
        }
        return m;
    }
};

static_assert(sizeof(ParamMapping) == 1536);

// ============================================================================
// Hardware Input → Parameter Mapping (16 entries)
// ============================================================================

/// Maps hardware inputs (0-15) to parameter slots
struct InputParamMapping {
    ParamMapEntry entries[16]; ///< Input# → param mapping

    [[nodiscard]] const ParamMapEntry& operator[](uint8_t input_id) const noexcept {
        return entries[input_id & 0x0F];
    }

    static constexpr InputParamMapping make_empty() noexcept {
        InputParamMapping m{};
        for (auto& e : m.entries) {
            e.param_id = 0xFF;
        }
        return m;
    }
};

static_assert(sizeof(InputParamMapping) == 192);

// ============================================================================
// Input Configuration
// ============================================================================

/// Input routing mode
enum class InputMode : uint8_t {
    DISABLED = 0,       ///< Input ignored
    PARAM_ONLY = 1,     ///< Route to parameter only
    EVENT_ONLY = 2,     ///< Route to event only
    PARAM_AND_EVENT = 3, ///< Route to both
};

/// Per-input configuration
struct InputConfig {
    uint8_t input_id = 0;
    InputMode mode = InputMode::DISABLED;
    uint16_t deadzone = 0;    ///< Dead zone threshold (0-65535)
    uint16_t smoothing = 0;   ///< Smoothing factor (0 = none)
    uint16_t threshold = 0;   ///< Change threshold for event generation
};

static_assert(sizeof(InputConfig) == 8);

// ============================================================================
// Application Configuration Bundle
// ============================================================================

// Forward declaration
struct RouteTable;

/// Complete application configuration
/// Set atomically via syscall, double-buffered in kernel
struct AppConfig {
    ParamMapping param_mapping;
    InputParamMapping input_mapping;
    InputConfig inputs[16];

    static constexpr AppConfig make_default() noexcept {
        AppConfig cfg{};
        cfg.param_mapping = ParamMapping::make_empty();
        cfg.input_mapping = InputParamMapping::make_empty();
        return cfg;
    }
};

// ============================================================================
// Denormalization Pipeline
// ============================================================================

/// Apply CC value through parameter mapping to SharedParamState
/// @param cc_num MIDI CC number (0-127)
/// @param cc_value Raw 7-bit CC value (0-127)
/// @param mapping Parameter mapping table
/// @param state Output shared parameter state
inline void apply_cc_to_param(uint8_t cc_num, uint8_t cc_value,
                              const ParamMapping& mapping,
                              SharedParamState& state) noexcept {
    const auto& entry = mapping[cc_num];
    if (!entry.is_mapped()) {
        return;
    }
    if (entry.param_id >= SharedParamState::MAX_PARAMS) {
        return;
    }

    // Normalize 7-bit CC to 0.0-1.0
    float normalized = static_cast<float>(cc_value) / 127.0f;

    // Denormalize to parameter range
    float value = entry.denormalize(normalized);

    // Write to shared state
    state.values[entry.param_id] = value;
    state.changed_flags |= (1u << entry.param_id);
    ++state.version;
}

/// Apply hardware input value through parameter mapping to SharedParamState
/// @param input_id Hardware input index (0-15)
/// @param raw_value Raw 16-bit input value (0-65535)
/// @param mapping Input parameter mapping table
/// @param state Output shared parameter state
inline void apply_input_to_param(uint8_t input_id, uint16_t raw_value,
                                 const InputParamMapping& mapping,
                                 SharedParamState& state) noexcept {
    const auto& entry = mapping[input_id & 0x0F];
    if (!entry.is_mapped()) {
        return;
    }
    if (entry.param_id >= SharedParamState::MAX_PARAMS) {
        return;
    }

    // Normalize 16-bit input to 0.0-1.0
    float normalized = static_cast<float>(raw_value) / 65535.0f;

    // Denormalize to parameter range
    float value = entry.denormalize(normalized);

    // Write to shared state
    state.values[entry.param_id] = value;
    state.changed_flags |= (1u << entry.param_id);
    ++state.version;
}

} // namespace umi
