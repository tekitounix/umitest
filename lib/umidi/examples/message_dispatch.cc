// SPDX-License-Identifier: MIT
/// @file message_dispatch.cc
/// @brief Example: Type-safe message dispatch using visitor pattern
///
/// This example demonstrates how to use the message wrapper types and
/// the dispatch helper for type-safe message handling.

#include "umidi/core/ump.hh"
#include "umidi/messages/channel_voice.hh"

#include <cstdio>

// Handler class demonstrating various message types
class MidiHandler {
public:
    void handle(const umidi::message::NoteOn& msg) {
        printf("NoteOn: ch=%d note=%d vel=%d\n",
               msg.channel(), msg.note(), msg.velocity());
    }

    void handle(const umidi::message::NoteOff& msg) {
        printf("NoteOff: ch=%d note=%d\n",
               msg.channel(), msg.note());
    }

    void handle(const umidi::message::ControlChange& msg) {
        printf("CC: ch=%d controller=%d value=%d\n",
               msg.channel(), msg.controller(), msg.value());
    }

    void handle(const umidi::message::PitchBend& msg) {
        printf("PitchBend: ch=%d value=%d (signed=%d)\n",
               msg.channel(), msg.value(), msg.signed_value());
    }

    void handle(const umidi::message::ProgramChange& msg) {
        printf("ProgramChange: ch=%d program=%d\n",
               msg.channel(), msg.program());
    }

    void handle(const umidi::message::ChannelPressure& msg) {
        printf("ChannelPressure: ch=%d pressure=%d\n",
               msg.channel(), msg.pressure());
    }

    void handle(const umidi::message::PolyPressure& msg) {
        printf("PolyPressure: ch=%d note=%d pressure=%d\n",
               msg.channel(), msg.note(), msg.pressure());
    }
};

int main() {
    using namespace umidi;

    printf("=== Message Dispatch Example ===\n");

    // Create messages using factory methods
    auto note_on = message::NoteOn::create(0, 60, 100);
    auto note_off = message::NoteOff::create(0, 60);
    auto cc = message::ControlChange::create(0, 1, 64);
    auto pb = message::PitchBend::create_signed(0, 0);  // Center position

    MidiHandler handler;

    // Direct handling with type-safe wrappers
    handler.handle(note_on);
    handler.handle(note_off);
    handler.handle(cc);
    handler.handle(pb);

    printf("\n--- Using dispatch helper ---\n");

    // Using the dispatch helper for runtime dispatch
    UMP32 messages[] = {
        note_on.ump,
        note_off.ump,
        cc.ump,
        pb.ump,
    };

    for (const auto& ump : messages) {
        message::dispatch(ump, [&handler](auto&& msg) {
            handler.handle(msg);
        });
    }

    return 0;
}
