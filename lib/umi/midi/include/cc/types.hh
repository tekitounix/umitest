// SPDX-License-Identifier: MIT
// UMI-OS MIDI Library - Control Change Types
// Type-safe CC definitions with compile-time names
#pragma once

#include <cstdint>
#include <cstddef>
#include <algorithm>

namespace umidi::cc {

// =============================================================================
// String Literal for Template Parameters (C++20)
// =============================================================================

template <std::size_t N>
struct StringLiteral {
    char value[N];

    constexpr StringLiteral(const char (&str)[N]) noexcept {
        std::copy_n(str, N, value);
    }

    constexpr operator const char*() const noexcept { return value; }
};

template <std::size_t N>
StringLiteral(const char (&)[N]) -> StringLiteral<N>;

// =============================================================================
// 7-bit Control Change
// =============================================================================

template <uint8_t CC, StringLiteral Name>
struct CC7Bit {
    static_assert(CC < 128, "CC number must be < 128");

    static constexpr uint8_t number() noexcept { return CC; }
    static constexpr uint8_t number_msb() noexcept { return CC; }
    static constexpr uint8_t number_lsb() noexcept { return 0xFF; }  // Not used
    static constexpr const char* name() noexcept { return Name.value; }

    // No state needed for 7-bit CC
    struct State {};

    [[nodiscard]] static constexpr uint8_t parse(State&, uint8_t value) noexcept {
        return value & 0x7F;
    }

    [[nodiscard]] static constexpr uint8_t format(uint8_t value) noexcept {
        return value & 0x7F;
    }
};

// =============================================================================
// Switch-type Control Change (on/off)
// =============================================================================

template <uint8_t CC, StringLiteral Name>
struct SwitchCC {
    static_assert(CC < 128, "CC number must be < 128");

    static constexpr uint8_t number() noexcept { return CC; }
    static constexpr uint8_t number_msb() noexcept { return CC; }
    static constexpr uint8_t number_lsb() noexcept { return 0xFF; }
    static constexpr const char* name() noexcept { return Name.value; }

    struct State {};

    // Parse: >= 64 is on
    [[nodiscard]] static constexpr bool parse(State&, uint8_t value) noexcept {
        return value >= 64;
    }

    [[nodiscard]] static constexpr uint8_t format(bool on) noexcept {
        return on ? 127 : 0;
    }
};

// =============================================================================
// 14-bit Control Change (MSB/LSB pair)
// =============================================================================

template <uint8_t MSB, StringLiteral Name>
struct CC14Bit {
    static_assert(MSB < 32, "MSB must be < 32 for 14-bit CC");

    static constexpr uint8_t number() noexcept { return MSB; }
    static constexpr uint8_t number_msb() noexcept { return MSB; }
    static constexpr uint8_t number_lsb() noexcept { return MSB + 32; }
    static constexpr const char* name() noexcept { return Name.value; }

    struct State {
        uint8_t cached_msb = 0;
        uint16_t current_value = 0;
    };

    [[nodiscard]] static constexpr uint16_t parse(State& state, uint8_t) noexcept {
        return state.current_value;
    }

    [[nodiscard]] static constexpr uint16_t parse_msb(State& state, uint8_t value) noexcept {
        state.cached_msb = value & 0x7F;
        state.current_value = uint16_t(state.cached_msb) << 7;
        return state.current_value;
    }

    [[nodiscard]] static constexpr uint16_t parse_lsb(State& state, uint8_t value) noexcept {
        state.current_value = (uint16_t(state.cached_msb) << 7) | (value & 0x7F);
        return state.current_value;
    }

    struct Value {
        uint8_t msb;
        uint8_t lsb;
    };

    [[nodiscard]] static constexpr Value format(uint16_t value) noexcept {
        return {uint8_t((value >> 7) & 0x7F), uint8_t(value & 0x7F)};
    }
};

// =============================================================================
// RPN/NRPN (Registered/Non-Registered Parameter Number)
// =============================================================================

template <uint16_t Number, bool IsNRPN = false, StringLiteral Name = "">
struct ParameterNumber {
    static_assert(Number < 16384, "Parameter number must be 14-bit");

    static constexpr uint16_t number() noexcept { return Number; }
    static constexpr uint8_t number_msb() noexcept { return (Number >> 7) & 0x7F; }
    static constexpr uint8_t number_lsb() noexcept { return Number & 0x7F; }
    static constexpr bool is_nrpn() noexcept { return IsNRPN; }
    static constexpr const char* name() noexcept { return Name.value; }

    [[nodiscard]] static constexpr uint16_t parse(uint16_t raw_value) noexcept {
        return raw_value & 0x3FFF;
    }

    struct Value {
        uint8_t msb;
        uint8_t lsb;
    };

    [[nodiscard]] static constexpr Value format(uint16_t value) noexcept {
        return {uint8_t((value >> 7) & 0x7F), uint8_t(value & 0x7F)};
    }
};

// Alias templates
template <uint16_t Number, StringLiteral Name = "">
using RPN = ParameterNumber<Number, false, Name>;

template <uint16_t Number, StringLiteral Name = "">
using NRPN = ParameterNumber<Number, true, Name>;

// =============================================================================
// Type traits for CC detection
// =============================================================================

template <typename T>
struct is_rpn_type : std::false_type {};

template <uint16_t N, bool IsNRPN, StringLiteral Name>
struct is_rpn_type<ParameterNumber<N, IsNRPN, Name>> : std::true_type {};

template <typename T>
inline constexpr bool is_rpn_type_v = is_rpn_type<T>::value;

// =============================================================================
// Pitch Bend Sensitivity helpers
// =============================================================================

namespace pitch_bend_sensitivity {

[[nodiscard]] constexpr uint8_t get_semitones(uint16_t value) noexcept {
    return (value >> 7) & 0x7F;
}

[[nodiscard]] constexpr uint8_t get_cents(uint16_t value) noexcept {
    return value & 0x7F;
}

[[nodiscard]] constexpr uint16_t make_value(uint8_t semitones, uint8_t cents) noexcept {
    return (uint16_t(semitones & 0x7F) << 7) | (cents & 0x7F);
}

} // namespace pitch_bend_sensitivity

} // namespace umidi::cc
