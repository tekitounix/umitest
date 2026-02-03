// SPDX-License-Identifier: MIT
// umidi - MIDI Transport Concepts (05-midi.md)
#pragma once

#include "ump.hh"
#include <concepts>

namespace umidi {

/// MIDI input transport concept.
/// Implementations: UsbMidiInput, UartMidiInput
template <typename T>
concept MidiInput = requires(T& t) {
    { t.poll() } -> std::same_as<void>;
    { t.is_connected() } -> std::convertible_to<bool>;
};

/// MIDI output transport concept.
/// Implementations send UMP32 to hardware.
template <typename T>
concept MidiOutput = requires(T& t, const UMP32& ump) {
    { t.send(ump) } -> std::convertible_to<bool>;
    { t.is_connected() } -> std::convertible_to<bool>;
};

} // namespace umidi
