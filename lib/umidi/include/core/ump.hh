// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <cstddef>

namespace umidi {

/// 32-bit Universal MIDI Packet, stored as single uint32_t.
/// Layout: [MT:4][Group:4][Status:8][Data1:8][Data2:8]
struct UMP32 {
    uint32_t word = 0;

    constexpr UMP32() noexcept = default;
    constexpr explicit UMP32(uint32_t raw) noexcept : word(raw) {}
    constexpr UMP32(uint8_t mt, uint8_t group, uint8_t status, uint8_t d1, uint8_t d2) noexcept
        : word(((mt & 0x0Fu) << 28) | ((group & 0x0Fu) << 24) |
               (uint32_t(status) << 16) | (uint32_t(d1) << 8) | d2) {}

    // Accessors
    [[nodiscard]] constexpr uint8_t mt() const noexcept { return (word >> 28) & 0x0F; }
    [[nodiscard]] constexpr uint8_t group() const noexcept { return (word >> 24) & 0x0F; }
    [[nodiscard]] constexpr uint8_t status() const noexcept { return (word >> 16) & 0xFF; }
    [[nodiscard]] constexpr uint8_t data1() const noexcept { return (word >> 8) & 0xFF; }
    [[nodiscard]] constexpr uint8_t data2() const noexcept { return word & 0xFF; }
    [[nodiscard]] constexpr uint8_t channel() const noexcept { return (word >> 16) & 0x0F; }
    [[nodiscard]] constexpr uint8_t command() const noexcept { return (word >> 16) & 0xF0; }

    // Setters
    constexpr void set_mt_group(uint8_t mt, uint8_t g) noexcept {
        word = (word & 0x00FFFFFFu) | ((mt & 0x0Fu) << 28) | ((g & 0x0Fu) << 24);
    }
    constexpr void set_status(uint8_t s) noexcept {
        word = (word & 0xFF00FFFFu) | (uint32_t(s) << 16);
    }
    constexpr void set_data1(uint8_t d) noexcept {
        word = (word & 0xFFFF00FFu) | (uint32_t(d) << 8);
    }
    constexpr void set_data2(uint8_t d) noexcept {
        word = (word & 0xFFFFFF00u) | d;
    }

    // Type checks (single mask comparison, 2-3 ARM instructions)
    [[nodiscard]] constexpr bool is_note_on() const noexcept {
        return (word & 0xF0F00000u) == 0x20900000u && (word & 0x7Fu);
    }
    [[nodiscard]] constexpr bool is_note_off() const noexcept {
        uint32_t mt_status = word & 0xF0F00000u;
        return mt_status == 0x20800000u ||
               (mt_status == 0x20900000u && (word & 0x7Fu) == 0);
    }
    [[nodiscard]] constexpr bool is_cc() const noexcept {
        return (word & 0xF0F00000u) == 0x20B00000u;
    }
    [[nodiscard]] constexpr bool is_program_change() const noexcept {
        return (word & 0xF0F00000u) == 0x20C00000u;
    }
    [[nodiscard]] constexpr bool is_channel_pressure() const noexcept {
        return (word & 0xF0F00000u) == 0x20D00000u;
    }
    [[nodiscard]] constexpr bool is_pitch_bend() const noexcept {
        return (word & 0xF0F00000u) == 0x20E00000u;
    }
    [[nodiscard]] constexpr bool is_poly_pressure() const noexcept {
        return (word & 0xF0F00000u) == 0x20A00000u;
    }
    [[nodiscard]] constexpr bool is_midi1_cv() const noexcept {
        return (word >> 28) == 0x2u;
    }
    [[nodiscard]] constexpr bool is_system() const noexcept {
        return (word >> 28) == 0x1u;
    }
    [[nodiscard]] constexpr bool is_realtime() const noexcept {
        return is_system() && ((word >> 16) & 0xF8) == 0xF8;
    }

    // Data accessors
    [[nodiscard]] constexpr uint8_t note() const noexcept { return (word >> 8) & 0x7F; }
    [[nodiscard]] constexpr uint8_t velocity() const noexcept { return word & 0x7F; }
    [[nodiscard]] constexpr uint8_t cc_number() const noexcept { return (word >> 8) & 0x7F; }
    [[nodiscard]] constexpr uint8_t cc_value() const noexcept { return word & 0x7F; }

    // Aliases (05-midi.md spec names)
    [[nodiscard]] constexpr uint8_t message_type() const noexcept { return mt(); }
    [[nodiscard]] constexpr uint8_t byte2() const noexcept { return data1(); }
    [[nodiscard]] constexpr uint8_t byte3() const noexcept { return data2(); }
    [[nodiscard]] constexpr uint8_t program() const noexcept { return (word >> 8) & 0x7F; }
    [[nodiscard]] constexpr uint8_t pressure() const noexcept { return (word >> 8) & 0x7F; }
    [[nodiscard]] constexpr uint16_t pitch_bend_value() const noexcept {
        return (uint16_t(word & 0x7F) << 7) | ((word >> 8) & 0x7F);
    }
    [[nodiscard]] constexpr uint32_t raw() const noexcept { return word; }

    // System message checks
    [[nodiscard]] constexpr bool is_timing_clock() const noexcept {
        return (word & 0xF0FF0000u) == 0x10F80000u;
    }
    [[nodiscard]] constexpr bool is_start() const noexcept {
        return (word & 0xF0FF0000u) == 0x10FA0000u;
    }
    [[nodiscard]] constexpr bool is_stop() const noexcept {
        return (word & 0xF0FF0000u) == 0x10FC0000u;
    }
    [[nodiscard]] constexpr bool is_continue() const noexcept {
        return (word & 0xF0FF0000u) == 0x10FB0000u;
    }
    [[nodiscard]] constexpr bool is_active_sensing() const noexcept {
        return (word & 0xF0FF0000u) == 0x10FE0000u;
    }
    [[nodiscard]] constexpr bool is_system_reset() const noexcept {
        return (word & 0xF0FF0000u) == 0x10FF0000u;
    }
    [[nodiscard]] constexpr bool is_tune_request() const noexcept {
        return (word & 0xF0FF0000u) == 0x10F60000u;
    }

    // System common accessors
    [[nodiscard]] constexpr uint8_t mtc_data() const noexcept { return (word >> 8) & 0x7F; }
    [[nodiscard]] constexpr uint16_t song_position() const noexcept {
        return ((word & 0x7F) << 7) | ((word >> 8) & 0x7F);
    }
    [[nodiscard]] constexpr uint8_t song_number() const noexcept { return (word >> 8) & 0x7F; }

    // Factory methods
    [[nodiscard]] static constexpr UMP32
    create(uint8_t mt, uint8_t group, uint8_t status, uint8_t d1, uint8_t d2 = 0) noexcept {
        return UMP32(mt, group, status, d1, d2);
    }

    [[nodiscard]] static constexpr UMP32
    note_on(uint8_t channel, uint8_t note, uint8_t velocity, uint8_t group = 0) noexcept {
        return UMP32(2, group, 0x90 | (channel & 0x0F), note & 0x7F, velocity & 0x7F);
    }

    [[nodiscard]] static constexpr UMP32
    note_off(uint8_t channel, uint8_t note, uint8_t velocity = 0, uint8_t group = 0) noexcept {
        return UMP32(2, group, 0x80 | (channel & 0x0F), note & 0x7F, velocity & 0x7F);
    }

    [[nodiscard]] static constexpr UMP32
    cc(uint8_t channel, uint8_t cc_num, uint8_t value, uint8_t group = 0) noexcept {
        return UMP32(2, group, 0xB0 | (channel & 0x0F), cc_num & 0x7F, value & 0x7F);
    }

    [[nodiscard]] static constexpr UMP32
    program_change(uint8_t channel, uint8_t program, uint8_t group = 0) noexcept {
        return UMP32(2, group, 0xC0 | (channel & 0x0F), program & 0x7F, 0);
    }

    [[nodiscard]] static constexpr UMP32
    pitch_bend(uint8_t channel, uint16_t value, uint8_t group = 0) noexcept {
        return UMP32(2, group, 0xE0 | (channel & 0x0F), value & 0x7F, (value >> 7) & 0x7F);
    }

    [[nodiscard]] static constexpr UMP32
    channel_pressure(uint8_t channel, uint8_t pressure, uint8_t group = 0) noexcept {
        return UMP32(2, group, 0xD0 | (channel & 0x0F), pressure & 0x7F, 0);
    }

    [[nodiscard]] static constexpr UMP32
    poly_pressure(uint8_t channel, uint8_t note, uint8_t pressure, uint8_t group = 0) noexcept {
        return UMP32(2, group, 0xA0 | (channel & 0x0F), note & 0x7F, pressure & 0x7F);
    }

    // System messages
    [[nodiscard]] static constexpr UMP32 timing_clock(uint8_t group = 0) noexcept {
        return UMP32(1, group, 0xF8, 0, 0);
    }
    [[nodiscard]] static constexpr UMP32 start(uint8_t group = 0) noexcept {
        return UMP32(1, group, 0xFA, 0, 0);
    }
    [[nodiscard]] static constexpr UMP32 stop(uint8_t group = 0) noexcept {
        return UMP32(1, group, 0xFC, 0, 0);
    }
    [[nodiscard]] static constexpr UMP32 continue_msg(uint8_t group = 0) noexcept {
        return UMP32(1, group, 0xFB, 0, 0);
    }
    [[nodiscard]] static constexpr UMP32 active_sensing(uint8_t group = 0) noexcept {
        return UMP32(1, group, 0xFE, 0, 0);
    }
    [[nodiscard]] static constexpr UMP32 system_reset(uint8_t group = 0) noexcept {
        return UMP32(1, group, 0xFF, 0, 0);
    }
    [[nodiscard]] static constexpr UMP32 tune_request(uint8_t group = 0) noexcept {
        return UMP32(1, group, 0xF6, 0, 0);
    }
    [[nodiscard]] static constexpr UMP32 mtc_quarter_frame(uint8_t data, uint8_t group = 0) noexcept {
        return UMP32(1, group, 0xF1, data & 0x7F, 0);
    }
    [[nodiscard]] static constexpr UMP32 song_position(uint16_t beats, uint8_t group = 0) noexcept {
        return UMP32(1, group, 0xF2, beats & 0x7F, (beats >> 7) & 0x7F);
    }
    [[nodiscard]] static constexpr UMP32 song_select(uint8_t song, uint8_t group = 0) noexcept {
        return UMP32(1, group, 0xF3, song & 0x7F, 0);
    }

    constexpr bool operator==(const UMP32& other) const noexcept { return word == other.word; }
    constexpr bool operator!=(const UMP32& other) const noexcept { return word != other.word; }
};

static_assert(sizeof(UMP32) == 4, "UMP32 must be 4 bytes");

/// 64-bit Universal MIDI Packet (for SysEx, MIDI 2.0 CV).
struct UMP64 {
    uint32_t word0 = 0;
    uint32_t word1 = 0;

    constexpr UMP64() noexcept = default;
    constexpr UMP64(uint32_t w0, uint32_t w1) noexcept : word0(w0), word1(w1) {}

    [[nodiscard]] constexpr uint8_t mt() const noexcept { return (word0 >> 28) & 0x0F; }
    [[nodiscard]] constexpr uint8_t group() const noexcept { return (word0 >> 24) & 0x0F; }
    [[nodiscard]] constexpr uint8_t status() const noexcept { return (word0 >> 16) & 0xFF; }
    [[nodiscard]] constexpr uint8_t data1() const noexcept { return (word0 >> 8) & 0xFF; }
    [[nodiscard]] constexpr uint8_t data2() const noexcept { return word0 & 0xFF; }
    [[nodiscard]] constexpr uint8_t sysex_status() const noexcept { return (word0 >> 20) & 0x0F; }
    [[nodiscard]] constexpr uint8_t sysex_num_bytes() const noexcept { return (word0 >> 16) & 0x0F; }
    [[nodiscard]] constexpr uint16_t data_16_hi() const noexcept { return word1 >> 16; }
    [[nodiscard]] constexpr uint16_t data_16_lo() const noexcept { return word1 & 0xFFFF; }
    [[nodiscard]] constexpr uint32_t data_32() const noexcept { return word1; }

    constexpr void set_mt_group(uint8_t mt, uint8_t g) noexcept {
        word0 = (word0 & 0x00FFFFFFu) | ((mt & 0x0Fu) << 28) | ((g & 0x0Fu) << 24);
    }
    constexpr void set_data_32(uint32_t d) noexcept { word1 = d; }

    [[nodiscard]] static constexpr UMP64
    sysex7_complete(uint8_t group, const uint8_t* data, uint8_t len) noexcept {
        UMP64 msg;
        uint8_t count = (len > 6) ? 6 : len;
        msg.word0 = (3u << 28) | ((group & 0x0Fu) << 24) | (count << 16);
        if (count >= 1) msg.word0 |= (uint32_t(data[0]) << 8);
        if (count >= 2) msg.word0 |= data[1];
        if (count >= 3) msg.word1 |= (uint32_t(data[2]) << 24);
        if (count >= 4) msg.word1 |= (uint32_t(data[3]) << 16);
        if (count >= 5) msg.word1 |= (uint32_t(data[4]) << 8);
        if (count >= 6) msg.word1 |= data[5];
        return msg;
    }
};

static_assert(sizeof(UMP64) == 8, "UMP64 must be 8 bytes");

/// Get UMP size in bytes from message type.
[[nodiscard]] constexpr size_t get_ump_size(uint8_t mt) noexcept {
    switch (mt) {
    case 0: case 1: case 2: case 6: case 7:
        return 4;
    case 3: case 4: case 8: case 9: case 10:
        return 8;
    case 5: case 13: case 14: case 15:
        return 16;
    default:
        return 0;
    }
}

} // namespace umidi
