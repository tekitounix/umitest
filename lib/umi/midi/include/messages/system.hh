// SPDX-License-Identifier: MIT
// UMI-OS MIDI Library - System Messages
// System Common and System Real-Time messages (MT=1)
#pragma once

#include "../core/ump.hh"
#include <cstdint>

namespace umidi::message {

// =============================================================================
// System Real-Time Messages (0xF8-0xFF)
// =============================================================================
// These are single-byte messages that can occur anywhere in the stream

struct TimingClock {
    UMP32 ump;

    static constexpr uint8_t STATUS = 0xF8;

    [[nodiscard]] constexpr uint8_t group() const noexcept { return ump.group(); }
    [[nodiscard]] constexpr bool is_valid() const noexcept {
        return ump.is_system() && ump.status() == STATUS;
    }

    [[nodiscard]] static constexpr TimingClock create(uint8_t group = 0) noexcept {
        return {UMP32::timing_clock(group)};
    }

    [[nodiscard]] static constexpr TimingClock from_ump(UMP32 u) noexcept { return {u}; }
};

struct Start {
    UMP32 ump;

    static constexpr uint8_t STATUS = 0xFA;

    [[nodiscard]] constexpr uint8_t group() const noexcept { return ump.group(); }
    [[nodiscard]] constexpr bool is_valid() const noexcept {
        return ump.is_system() && ump.status() == STATUS;
    }

    [[nodiscard]] static constexpr Start create(uint8_t group = 0) noexcept {
        return {UMP32::start(group)};
    }

    [[nodiscard]] static constexpr Start from_ump(UMP32 u) noexcept { return {u}; }
};

struct Continue {
    UMP32 ump;

    static constexpr uint8_t STATUS = 0xFB;

    [[nodiscard]] constexpr uint8_t group() const noexcept { return ump.group(); }
    [[nodiscard]] constexpr bool is_valid() const noexcept {
        return ump.is_system() && ump.status() == STATUS;
    }

    [[nodiscard]] static constexpr Continue create(uint8_t group = 0) noexcept {
        return {UMP32::continue_msg(group)};
    }

    [[nodiscard]] static constexpr Continue from_ump(UMP32 u) noexcept { return {u}; }
};

struct Stop {
    UMP32 ump;

    static constexpr uint8_t STATUS = 0xFC;

    [[nodiscard]] constexpr uint8_t group() const noexcept { return ump.group(); }
    [[nodiscard]] constexpr bool is_valid() const noexcept {
        return ump.is_system() && ump.status() == STATUS;
    }

    [[nodiscard]] static constexpr Stop create(uint8_t group = 0) noexcept {
        return {UMP32::stop(group)};
    }

    [[nodiscard]] static constexpr Stop from_ump(UMP32 u) noexcept { return {u}; }
};

struct ActiveSensing {
    UMP32 ump;

    static constexpr uint8_t STATUS = 0xFE;

    [[nodiscard]] constexpr uint8_t group() const noexcept { return ump.group(); }
    [[nodiscard]] constexpr bool is_valid() const noexcept {
        return ump.is_system() && ump.status() == STATUS;
    }

    [[nodiscard]] static constexpr ActiveSensing create(uint8_t group = 0) noexcept {
        return {UMP32(1, group, STATUS, 0, 0)};
    }

    [[nodiscard]] static constexpr ActiveSensing from_ump(UMP32 u) noexcept { return {u}; }
};

struct SystemReset {
    UMP32 ump;

    static constexpr uint8_t STATUS = 0xFF;

    [[nodiscard]] constexpr uint8_t group() const noexcept { return ump.group(); }
    [[nodiscard]] constexpr bool is_valid() const noexcept {
        return ump.is_system() && ump.status() == STATUS;
    }

    [[nodiscard]] static constexpr SystemReset create(uint8_t group = 0) noexcept {
        return {UMP32(1, group, STATUS, 0, 0)};
    }

    [[nodiscard]] static constexpr SystemReset from_ump(UMP32 u) noexcept { return {u}; }
};

// =============================================================================
// System Common Messages (0xF1-0xF7)
// =============================================================================

struct MidiTimeCode {
    UMP32 ump;

    static constexpr uint8_t STATUS = 0xF1;

    [[nodiscard]] constexpr uint8_t group() const noexcept { return ump.group(); }
    [[nodiscard]] constexpr uint8_t type_and_value() const noexcept { return ump.data1(); }
    [[nodiscard]] constexpr uint8_t mtc_type() const noexcept { return (ump.data1() >> 4) & 0x07; }
    [[nodiscard]] constexpr uint8_t mtc_value() const noexcept { return ump.data1() & 0x0F; }

    [[nodiscard]] constexpr bool is_valid() const noexcept {
        return ump.is_system() && ump.status() == STATUS;
    }

    [[nodiscard]] static constexpr MidiTimeCode
    create(uint8_t type_value, uint8_t group = 0) noexcept {
        return {UMP32(1, group, STATUS, type_value & 0x7F, 0)};
    }

    [[nodiscard]] static constexpr MidiTimeCode from_ump(UMP32 u) noexcept { return {u}; }
};

struct SongPosition {
    UMP32 ump;

    static constexpr uint8_t STATUS = 0xF2;

    [[nodiscard]] constexpr uint8_t group() const noexcept { return ump.group(); }
    [[nodiscard]] constexpr uint16_t position() const noexcept {
        return uint16_t(ump.data1()) | (uint16_t(ump.data2()) << 7);
    }

    [[nodiscard]] constexpr bool is_valid() const noexcept {
        return ump.is_system() && ump.status() == STATUS;
    }

    [[nodiscard]] static constexpr SongPosition
    create(uint16_t position, uint8_t group = 0) noexcept {
        return {UMP32(1, group, STATUS, position & 0x7F, (position >> 7) & 0x7F)};
    }

    [[nodiscard]] static constexpr SongPosition from_ump(UMP32 u) noexcept { return {u}; }
};

struct SongSelect {
    UMP32 ump;

    static constexpr uint8_t STATUS = 0xF3;

    [[nodiscard]] constexpr uint8_t group() const noexcept { return ump.group(); }
    [[nodiscard]] constexpr uint8_t song() const noexcept { return ump.data1(); }

    [[nodiscard]] constexpr bool is_valid() const noexcept {
        return ump.is_system() && ump.status() == STATUS;
    }

    [[nodiscard]] static constexpr SongSelect
    create(uint8_t song, uint8_t group = 0) noexcept {
        return {UMP32(1, group, STATUS, song & 0x7F, 0)};
    }

    [[nodiscard]] static constexpr SongSelect from_ump(UMP32 u) noexcept { return {u}; }
};

struct TuneRequest {
    UMP32 ump;

    static constexpr uint8_t STATUS = 0xF6;

    [[nodiscard]] constexpr uint8_t group() const noexcept { return ump.group(); }
    [[nodiscard]] constexpr bool is_valid() const noexcept {
        return ump.is_system() && ump.status() == STATUS;
    }

    [[nodiscard]] static constexpr TuneRequest create(uint8_t group = 0) noexcept {
        return {UMP32(1, group, STATUS, 0, 0)};
    }

    [[nodiscard]] static constexpr TuneRequest from_ump(UMP32 u) noexcept { return {u}; }
};

// =============================================================================
// Helper to check if UMP is a realtime message
// =============================================================================

[[nodiscard]] constexpr bool is_realtime_status(uint8_t status) noexcept {
    return status >= 0xF8;
}

[[nodiscard]] constexpr bool is_system_common_status(uint8_t status) noexcept {
    return status >= 0xF0 && status < 0xF8;
}

} // namespace umidi::message
