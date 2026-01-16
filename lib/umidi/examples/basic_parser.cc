// SPDX-License-Identifier: MIT
/// @file basic_parser.cc
/// @brief Example: Basic MIDI parser usage
///
/// This example shows how to parse a MIDI byte stream into UMP32 messages
/// and dispatch them to appropriate handlers.

#include "umidi/core/ump.hh"
#include "umidi/core/parser.hh"
#include "umidi/messages/channel_voice.hh"

#include <cstdio>

// Example callback handlers
void on_note_on(uint8_t channel, uint8_t note, uint8_t velocity) {
    printf("Note On: ch=%d note=%d vel=%d\n", channel, note, velocity);
}

void on_note_off(uint8_t channel, uint8_t note) {
    printf("Note Off: ch=%d note=%d\n", channel, note);
}

void on_cc(uint8_t channel, uint8_t controller, uint8_t value) {
    printf("CC: ch=%d cc=%d val=%d\n", channel, controller, value);
}

// Parse and dispatch MIDI messages
void process_midi_byte(umidi::Parser& parser, uint8_t byte) {
    umidi::UMP32 ump;

    if (parser.parse(byte, ump)) {
        // Message complete - dispatch based on type
        if (ump.is_note_on()) {
            on_note_on(ump.channel(), ump.note(), ump.velocity());
        } else if (ump.is_note_off()) {
            on_note_off(ump.channel(), ump.note());
        } else if (ump.is_cc()) {
            on_cc(ump.channel(), ump.cc_number(), ump.cc_value());
        }
    }
}

int main() {
    // Example MIDI byte sequence:
    // Note On: channel 0, note 60 (C4), velocity 100
    // Note Off: channel 0, note 60
    // CC: channel 0, controller 1 (mod wheel), value 64
    uint8_t midi_data[] = {
        0x90, 60, 100,  // Note On
        0x80, 60, 0,    // Note Off
        0xB0, 1, 64,    // CC
    };

    umidi::Parser parser;

    printf("=== Basic Parser Example ===\n");
    for (uint8_t byte : midi_data) {
        process_midi_byte(parser, byte);
    }

    return 0;
}
