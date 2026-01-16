# umidi - UMI-OS MIDI Library

High-performance MIDI 1.0/2.0 processing library optimized for ARM Cortex-M.

## Features

- **UMP-Opt Format**: UMP32 as single `uint32_t` for efficient comparison
- **Single-Mask Type Checking**: `is_note_on()`, `is_cc()` in one operation
- **40% Memory Reduction**: 8 bytes/event (vs 20 bytes traditional)
- **Zero-Copy Parsing**: Incremental UMP construction
- **No Heap Allocation**: Fully static memory management

## Quick Start

```cpp
#include <umidi/umidi.hh>

umidi::Parser parser;
umidi::UMP32 ump;

void process_byte(uint8_t byte) {
    if (parser.parse(byte, ump)) {
        if (ump.is_note_on()) {
            uint8_t ch = ump.channel();
            uint8_t note = ump.note();
            uint8_t vel = ump.velocity();
            // Handle Note On
        }
    }
}
```

## Documentation

| Document | Description |
|----------|-------------|
| [CORE.md](CORE.md) | UMP32, UMP64, Parser, Result, SysExBuffer |
| [MESSAGES.md](MESSAGES.md) | Channel Voice, System, SysEx, Utility messages |
| [CC.md](CC.md) | Control Change types, standards, RPN/NRPN decoder |
| [PROTOCOL.md](PROTOCOL.md) | UMI SysEx Protocol, Standard IO, Firmware Update |
| [TRANSPORT.md](TRANSPORT.md) | Transport abstraction (SysEx/Bulk) |
| [STATE.md](STATE.md) | State sync, Resume, Boot verification |
| [OBJECT.md](OBJECT.md) | Object transfer (sequences, samples, presets) |
| [API.md](API.md) | Complete API reference |

## Directory Structure

```
lib/umidi/
├── umidi.hh              # Main header (includes all)
├── core/
│   ├── ump.hh            # UMP32/UMP64 types
│   ├── parser.hh         # MIDI 1.0 byte stream parser
│   ├── result.hh         # Result<T> and Error types
│   └── sysex_buffer.hh   # Ring buffer for SysEx data
├── messages/
│   ├── channel_voice.hh  # NoteOn, NoteOff, CC, etc.
│   ├── system.hh         # System messages
│   ├── sysex.hh          # SysEx7 (UMP64)
│   └── utility.hh        # JR Timestamp, NOOP
├── cc/
│   ├── types.hh          # CC type definitions
│   ├── standards.hh      # Standard CC definitions
│   └── decoder.hh        # RPN/NRPN state machine
├── codec/
│   └── decoder.hh        # Template static decoder
├── protocol/
│   ├── umi_sysex.hh      # UMI SysEx Protocol, StandardIO
│   ├── umi_auth.hh       # Authentication (HMAC-SHA256)
│   ├── umi_firmware.hh   # Firmware header/validation
│   ├── umi_bootloader.hh # A/B partition management
│   ├── umi_session.hh    # Timeout/flow control
│   ├── umi_transport.hh  # Transport abstraction
│   ├── umi_state.hh      # State sync, resume
│   └── umi_object.hh     # Object transfer
├── util/
│   └── convert.hh        # Value conversions
├── event.hh              # Sample-accurate Event type
├── test/                 # Standalone tests
│   ├── test_core.cc
│   ├── test_messages.cc
│   ├── test_protocol.cc
│   ├── test_extended_protocol.cc
│   └── test_renode.cc
└── doc/                  # Documentation
    └── *.md
```

## Independence

umidi is **completely independent** from UMI-OS core:
- No dependency on UMI-OS types (`Event`, `Processor`, etc.)
- Uses its own namespace `umidi::`
- Defines its own `Result<T>` type
- Can be built and used standalone

## Requirements

- C++23 (`std::expected`)
- No exceptions (`-fno-exceptions` compatible)
- No heap allocation
- Little-endian target (ARM, x86)

## License

MIT License
