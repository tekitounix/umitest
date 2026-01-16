# umidi Design Document

## Overview

umidi is a MIDI library designed for embedded systems, with a focus on:

- **Performance**: Optimized for ARM Cortex-M microcontrollers
- **Simplicity**: Minimal dependencies, header-only where practical
- **Type Safety**: Compile-time checks where possible
- **Zero Allocation**: All operations use pre-allocated buffers

## Architecture

### Layer Structure

```
┌─────────────────────────────────────────────────┐
│                 Application                      │
├─────────────────────────────────────────────────┤
│   messages/       │   protocol/                  │
│   - channel_voice │   - encoding                 │
│   - system        │   - commands                 │
│   - sysex         │   - message                  │
│   - utility       │   - standard_io              │
├─────────────────────────────────────────────────┤
│                   core/                          │
│   - ump.hh        - parser.hh                   │
│   - result.hh     - sysex_buffer.hh             │
└─────────────────────────────────────────────────┘
```

### Core Design: UMP-Opt Format

The library uses an optimized UMP (Universal MIDI Packet) format:

```cpp
struct UMP32 {
    uint32_t word;  // [MT:4][Group:4][Status:8][Data1:8][Data2:8]
};
```

Benefits:
- Single 32-bit comparison for message type checks
- Efficient memory layout for ARM Cortex-M
- Compatible with MIDI 2.0 UMP while optimized for MIDI 1.0

### Type Checking Optimization

```cpp
// Single mask operation to check Note On
bool is_note_on() const noexcept {
    return (word & 0xF0F00000u) == 0x20900000u && (word & 0x7Fu);
}
```

This compiles to just 2-3 ARM instructions.

## Module Reference

### core/

| File | Description |
|------|-------------|
| `ump.hh` | UMP32/UMP64 types with optimized accessors |
| `parser.hh` | MIDI 1.0 byte stream to UMP32 parser |
| `result.hh` | Error handling with `Result<T>` type |
| `sysex_buffer.hh` | Buffer for accumulating SysEx data |

### messages/

| File | Description |
|------|-------------|
| `channel_voice.hh` | NoteOn, NoteOff, CC, etc. wrappers |
| `system.hh` | System real-time and common messages |
| `sysex.hh` | SysEx message builders |
| `utility.hh` | Utility messages (MT=0) |

### protocol/

| File | Description |
|------|-------------|
| `encoding.hh` | 7-bit SysEx encoding utilities |
| `commands.hh` | Protocol command definitions |
| `message.hh` | Message builder and parser |
| `standard_io.hh` | stdin/stdout over SysEx |

## Usage Patterns

### Basic Parsing

```cpp
umidi::Parser parser;
umidi::UMP32 ump;

void midi_rx_callback(uint8_t byte) {
    if (parser.parse(byte, ump)) {
        process_message(ump);
    }
}
```

### Type-Safe Dispatch

```cpp
using namespace umidi::message;

dispatch(ump, [](auto&& msg) {
    using T = std::decay_t<decltype(msg)>;
    if constexpr (std::is_same_v<T, NoteOn>) {
        synth.note_on(msg.note(), msg.velocity());
    }
});
```

### Factory Methods

```cpp
// Create messages
auto note_on = NoteOn::create(channel, note, velocity);
auto cc = ControlChange::create(channel, controller, value);

// Send via serializer
uint8_t buffer[3];
size_t len = Serializer::serialize(note_on.ump, buffer);
midi_tx(buffer, len);
```

## Performance Considerations

### Memory

- UMP32: 4 bytes per message
- Parser state: ~8 bytes
- No dynamic allocation required

### Timing

Typical ARM Cortex-M4 @ 168MHz:
- UMP32 type check: ~10 cycles
- Parser (per byte): ~30-50 cycles
- Serializer: ~20-40 cycles

## Thread Safety

The library is designed for single-threaded use within an audio callback.
For multi-threaded applications, external synchronization is required.

## Error Handling

Uses `Result<T>` pattern instead of exceptions:

```cpp
Result<T> result = operation();
if (result.is_ok()) {
    T value = result.unwrap();
} else {
    Error err = result.error();
}
```
