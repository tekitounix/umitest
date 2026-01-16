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

Full documentation is in [docs/](docs/):

| Document | Description |
|----------|-------------|
| [CORE.md](docs/CORE.md) | UMP32, UMP64, Parser, Result, SysExBuffer |
| [MESSAGES.md](docs/MESSAGES.md) | Channel Voice, System, SysEx, Utility messages |
| [CC.md](docs/CC.md) | Control Change types, standards, RPN/NRPN decoder |
| [PROTOCOL.md](docs/PROTOCOL.md) | UMI SysEx Protocol, Standard IO, Firmware Update |
| [TRANSPORT.md](docs/TRANSPORT.md) | Transport abstraction (SysEx/Bulk) |
| [STATE.md](docs/STATE.md) | State sync, Resume, Boot verification |
| [OBJECT.md](docs/OBJECT.md) | Object transfer (sequences, samples, presets) |
| [API.md](docs/API.md) | Complete API reference |

### Building Documentation

```bash
cd docs
pip install -r requirements.txt

# Build English
sphinx-build -b html . _build/html

# Build Japanese
sphinx-build -b html ja _build/html/ja

open _build/html/index.html
```

## Building & Testing

```bash
# Standalone build (from lib/umidi directory)
xmake f -P .
xmake build -P . umidi_test_core
xmake run -P . umidi_test_core

# Or from UMI-OS root
xmake build umidi_test_core
xmake run umidi_test_core
```

## Directory Structure

```
lib/umidi/
├── README.md             # This file
├── xmake.lua             # Standalone build config
├── include/umidi/        # Header files
│   ├── umidi.hh          # Main header
│   ├── core/             # Core types
│   ├── messages/         # Message wrappers
│   └── protocol/         # UMI SysEx protocol
├── test/                 # Unit tests
├── examples/             # Working examples
└── docs/                 # Documentation (en + ja)
```

## Independence

umidi is **completely independent** from UMI-OS core:
- No dependency on UMI-OS types
- Uses its own namespace `umidi::`
- Can be built and used standalone

## Requirements

- C++23 (`std::expected`)
- No exceptions (`-fno-exceptions` compatible)
- No heap allocation
- Little-endian target (ARM, x86)

## License

MIT License
