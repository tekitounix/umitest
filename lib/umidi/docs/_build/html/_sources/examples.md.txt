# Examples

This page provides practical examples for using the umidi library.

## Basic MIDI Parsing

Parse a MIDI byte stream and handle messages:

```cpp
#include <umidi/core/ump.hh>
#include <umidi/core/parser.hh>

umidi::Parser parser;
umidi::UMP32 ump;

void process_midi_byte(uint8_t byte) {
    if (parser.parse(byte, ump)) {
        if (ump.is_note_on()) {
            synth.note_on(ump.channel(), ump.note(), ump.velocity());
        } else if (ump.is_note_off()) {
            synth.note_off(ump.channel(), ump.note());
        } else if (ump.is_cc()) {
            params.set_cc(ump.cc_number(), ump.cc_value());
        }
    }
}
```

## Running Status Support

For memory-constrained systems, running status reduces MIDI bandwidth:

```cpp
umidi::Parser parser;

void process_with_running_status(uint8_t byte) {
    umidi::UMP32 ump;
    if (parser.parse_running(byte, ump)) {
        handle_message(ump);
    }
}
```

## Type-Safe Message Wrappers

Use the message wrappers for compile-time type safety:

```cpp
#include <umidi/messages/channel_voice.hh>

using namespace umidi::message;

void handle_note(const NoteOn& msg) {
    printf("Note %d on channel %d, velocity %d\n",
           msg.note(), msg.channel(), msg.velocity());
}

// Create messages
auto note = NoteOn::create(0, 60, 100);  // Channel 0, Middle C, velocity 100
handle_note(note);
```

## Message Dispatch

Dispatch messages to handlers based on type:

```cpp
#include <umidi/messages/channel_voice.hh>

class Synthesizer {
public:
    void process(const umidi::UMP32& ump) {
        umidi::message::dispatch(ump, [this](auto&& msg) {
            handle(msg);
        });
    }

private:
    void handle(const umidi::message::NoteOn& msg) {
        voice_on(msg.note(), msg.velocity());
    }

    void handle(const umidi::message::NoteOff& msg) {
        voice_off(msg.note());
    }

    void handle(const umidi::message::ControlChange& msg) {
        if (msg.controller() == 1) {  // Mod wheel
            set_modulation(msg.value());
        }
    }

    // Catch-all for other message types
    template<typename T>
    void handle(const T&) {}
};
```

## MIDI Output

Convert UMP32 back to MIDI 1.0 bytes:

```cpp
#include <umidi/core/parser.hh>  // Contains Serializer

umidi::UMP32 ump = umidi::UMP32::note_on(0, 60, 100);

uint8_t buffer[3];
size_t len = umidi::Serializer::serialize(ump, buffer);

midi_send(buffer, len);  // Send 3 bytes: 0x90 0x3C 0x64
```

## SysEx Communication

Use the UMI SysEx protocol for bidirectional communication:

```cpp
#include <umidi/protocol/message.hh>
#include <umidi/protocol/standard_io.hh>

using namespace umidi::protocol;

StandardIO<512, 512> stdio;

// Send text to host
void print(const char* text) {
    stdio.write_stdout(
        reinterpret_cast<const uint8_t*>(text),
        strlen(text),
        [](const uint8_t* data, size_t len) {
            midi_send_sysex(data, len);
        }
    );
}

// Receive text from host
void on_sysex_received(const uint8_t* data, size_t len) {
    stdio.process_message(data, len);
}

// Set callback for stdin data
stdio.set_stdin_callback(
    [](const uint8_t* data, size_t len, void*) {
        process_command(data, len);
    },
    nullptr
);
```

## Pitch Bend Handling

Pitch bend uses a 14-bit value (0-16383):

```cpp
#include <umidi/messages/channel_voice.hh>

using namespace umidi::message;

// Create pitch bend at center (8192)
auto pb = PitchBend::create(0, 8192);

// Or use signed value (-8192 to +8191)
auto pb_up = PitchBend::create_signed(0, 4096);   // Bend up
auto pb_down = PitchBend::create_signed(0, -4096); // Bend down

// Read pitch bend
void handle_pitch_bend(const PitchBend& msg) {
    int16_t semitones = msg.signed_value() / (8192 / 2);  // +-2 semitones range
    set_pitch_offset(semitones);
}
```

## Building UMI Protocol Messages

Build custom protocol messages:

```cpp
#include <umidi/protocol/message.hh>

using namespace umidi::protocol;

void send_firmware_query() {
    MessageBuilder<64> builder;
    builder.begin(Command::FW_QUERY, 0x00);
    size_t len = builder.finalize();
    midi_send_sysex(builder.data(), len);
}

void send_data_with_payload(const uint8_t* data, size_t data_len) {
    MessageBuilder<256> builder;
    builder.begin(Command::STDOUT_DATA, sequence_++);
    builder.add_data(data, data_len);  // Auto-encodes to 7-bit
    size_t len = builder.finalize();
    midi_send_sysex(builder.data(), len);
}
```

## Interrupt-Safe Usage

For real-time audio callbacks:

```cpp
// All operations are lock-free and deterministic
void audio_callback(uint8_t* midi_in, size_t midi_len,
                    float* audio_out, size_t frames) {
    // Parse MIDI (constant time per byte)
    for (size_t i = 0; i < midi_len; ++i) {
        umidi::UMP32 ump;
        if (parser_.parse(midi_in[i], ump)) {
            // Handle message (constant time)
            if (ump.is_note_on()) {
                synth_.note_on(ump.note(), ump.velocity());
            }
        }
    }

    // Generate audio
    synth_.render(audio_out, frames);
}
```
