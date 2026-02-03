// SPDX-License-Identifier: MIT
// UMI-OS Route Table
// Defines MIDI routing policy — separated to avoid circular dependencies

#pragma once

#include <cstdint>

namespace umi {

// ============================================================================
// Route Flags — Per-message routing decisions
// ============================================================================

/// Bitmask flags controlling where a message is routed
enum RouteFlags : uint8_t {
    ROUTE_NONE        = 0,      ///< Message dropped
    ROUTE_AUDIO       = 1,      ///< → AudioEventQueue (process() input_events)
    ROUTE_CONTROL     = 2,      ///< → ControlEventQueue (CC converted to INPUT_CHANGE)
    ROUTE_STREAM      = 4,      ///< → Stream recording (future)
    ROUTE_PARAM       = 8,      ///< → ParamMapping → SharedParamState
    ROUTE_CONTROL_RAW = 16,     ///< → ControlEventQueue (UMP32 unchanged)
};

/// Bitwise OR operator for RouteFlags
constexpr RouteFlags operator|(RouteFlags a, RouteFlags b) noexcept {
    return static_cast<RouteFlags>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

// ============================================================================
// MIDI Command Index
// ============================================================================

/// MIDI command index for channel_voice table
/// Maps (status >> 4) - 8 to index 0-7
enum MidiCommandIndex : uint8_t {
    NOTE_OFF_IDX = 0,
    NOTE_ON_IDX = 1,
    POLY_AT_IDX = 2,
    CC_IDX = 3,
    PGM_CHANGE_IDX = 4,
    CHAN_AT_IDX = 5,
    PITCH_BEND_IDX = 6,
};

// ============================================================================
// Route Table — Message routing policy
// ============================================================================

/// Routing table: determines where each MIDI message type goes
/// Layout: 272 bytes total
struct RouteTable {
    /// Channel voice messages: [command_index][channel] → flags
    RouteFlags channel_voice[8][16];    ///< 128B

    /// Control Change overrides: [CC#] → flags
    /// When non-zero, overrides channel_voice[CC_IDX][ch] for this CC number
    RouteFlags control_change[128];     ///< 128B

    /// System messages: [status & 0x0F] → flags
    RouteFlags system[16];              ///< 16B

    /// Look up route flags for a MIDI status byte
    [[nodiscard]] RouteFlags lookup(uint8_t status, uint8_t data1 = 0) const noexcept {
        if (status >= 0xF0) {
            return system[status & 0x0F];
        }
        uint8_t cmd_idx = (status >> 4) - 8;
        uint8_t ch = status & 0x0F;
        RouteFlags flags = channel_voice[cmd_idx][ch];

        // CC override: if control_change entry is set, use it instead
        if (cmd_idx == CC_IDX && control_change[data1] != ROUTE_NONE) {
            flags = control_change[data1];
        }
        return flags;
    }

    /// Create a default route table (all notes → audio, all CC → control)
    static constexpr RouteTable make_default() noexcept {
        RouteTable rt{};
        for (uint8_t ch = 0; ch < 16; ++ch) {
            rt.channel_voice[NOTE_OFF_IDX][ch] = ROUTE_AUDIO;
            rt.channel_voice[NOTE_ON_IDX][ch] = ROUTE_AUDIO;
            rt.channel_voice[POLY_AT_IDX][ch] = ROUTE_AUDIO;
            rt.channel_voice[PITCH_BEND_IDX][ch] = ROUTE_AUDIO;
            rt.channel_voice[CHAN_AT_IDX][ch] = ROUTE_AUDIO;
            rt.channel_voice[CC_IDX][ch] = ROUTE_CONTROL;
            rt.channel_voice[PGM_CHANGE_IDX][ch] = ROUTE_CONTROL;
        }
        return rt;
    }
};

static_assert(sizeof(RouteTable) == 272);

} // namespace umi
