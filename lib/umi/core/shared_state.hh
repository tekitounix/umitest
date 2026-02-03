// SPDX-License-Identifier: MIT
// UMI-OS Shared State Structures
// Data structures for kernel ↔ app communication via shared memory

#pragma once

#include <cstdint>

namespace umi {

// ============================================================================
// SharedParamState — Parameter values shared between kernel and app
// ============================================================================

/// Parameter state written by EventRouter, read by Processor (via AudioContext)
/// Layout: 164 bytes (32 floats + flags + version + padding)
struct SharedParamState {
    static constexpr uint32_t MAX_PARAMS = 32;

    float values[MAX_PARAMS]{};     ///< Denormalized parameter values
    uint32_t changed_flags = 0;     ///< Bitmask: bit i set when values[i] changed
    uint32_t version = 0;           ///< Monotonic counter, incremented per audio block
};

static_assert(sizeof(SharedParamState) == 136);

// ============================================================================
// SharedChannelState — MIDI channel state
// ============================================================================

/// Per-channel MIDI state maintained by EventRouter
/// Layout: 64 bytes (16 channels × 4 bytes)
struct SharedChannelState {
    struct Channel {
        uint8_t program = 0;        ///< Current program number
        uint8_t pressure = 0;       ///< Channel pressure (aftertouch)
        int16_t pitch_bend = 0;     ///< Pitch bend value (-8192 ~ 8191)
    };

    Channel channels[16]{};
};

static_assert(sizeof(SharedChannelState) == 64);

// ============================================================================
// SharedInputState — Hardware input state
// ============================================================================

/// Raw hardware input values (ADC, GPIO, etc.)
/// Layout: 32 bytes (16 × uint16_t)
struct SharedInputState {
    static constexpr uint32_t MAX_INPUTS = 16;

    uint16_t raw[MAX_INPUTS]{};     ///< Normalized values 0x0000–0xFFFF
};

static_assert(sizeof(SharedInputState) == 32);

} // namespace umi
