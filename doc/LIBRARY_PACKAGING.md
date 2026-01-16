# UMI-OS Library Packaging Guide

This guide describes how to structure and document libraries in UMI-OS.

## Directory Structure

```
lib/<library_name>/
├── core/               # Core types and utilities
│   ├── types.hh
│   └── ...
├── <module>/           # Feature modules
│   ├── feature.hh
│   └── ...
├── test/               # Unit tests
│   ├── test_core.cc
│   └── ...
├── examples/           # Example code (primary source)
│   ├── basic_usage.cc
│   └── ...
├── docs/               # Documentation
│   ├── Doxyfile        # Doxygen configuration
│   ├── conf.py         # Sphinx configuration
│   ├── requirements.txt
│   ├── index.rst       # Main documentation entry
│   ├── design.md       # Design document
│   └── examples.md     # Usage examples
└── doc/                # (Optional) Additional markdown docs
    └── README.md
```

## Documentation Strategy

### 1. Source Code Comments (Minimal)

Use Doxygen-style comments only for **public APIs**:

```cpp
/// @file ump.hh
/// @brief Core UMP types for MIDI handling.

/// @brief 32-bit Universal MIDI Packet.
///
/// @par Example:
/// @code
/// auto note = UMP32::note_on(0, 60, 100);
/// @endcode
struct UMP32 {
    /// @brief Raw packet data.
    uint32_t word = 0;

    /// @brief Create a Note On message.
    static constexpr UMP32 note_on(uint8_t ch, uint8_t note, uint8_t vel);
};
```

### 2. Doxygen Configuration

Create `docs/Doxyfile` for XML generation:

```
PROJECT_NAME     = "library_name"
INPUT            = ../core ../module
GENERATE_XML     = YES
GENERATE_HTML    = NO
EXTRACT_ALL      = NO
HIDE_UNDOC_MEMBERS = YES
```

Key settings:
- `EXTRACT_ALL = NO`: Only document items with Doxygen comments
- `HIDE_UNDOC_MEMBERS = YES`: Hide undocumented internals
- `GENERATE_XML = YES`: Required for Breathe

### 3. Sphinx + Breathe + Exhale

Create `docs/conf.py`:

```python
extensions = ['breathe', 'exhale', 'myst_parser']

breathe_projects = {'library': '_build/doxygen/xml'}
breathe_default_project = 'library'

exhale_args = {
    'containmentFolder': './api',
    'rootFileName': 'library_root.rst',
    'exhaleExecutesDoxygen': True,
    'exhaleDoxygenStdin': '''
INPUT = ../core ../module
GENERATE_XML = YES
''',
}
```

### 4. Hand-Written Documentation

Write design and usage documentation in Markdown:

- `design.md`: Architecture, design decisions, internals
- `examples.md`: Practical usage examples with code

Use MyST parser for Markdown in Sphinx.

### 5. Examples as Primary Source

Put working examples in `examples/`:

```cpp
// examples/basic_usage.cc
/// @file basic_usage.cc
/// @brief Example: Basic library usage

#include "../core/types.hh"

int main() {
    // This example demonstrates...
}
```

Examples should:
- Be compilable and runnable
- Include Doxygen comments for documentation
- Cover common use cases

## Build Integration

### xmake.lua

Add test targets for the library:

```lua
-- Library tests
target("mylib_test_core")
    set_kind("binary")
    set_default(false)
    add_files("lib/mylib/test/test_core.cc")
    add_includedirs("lib/mylib")
```

### Documentation Build

Add to `docs/Makefile`:

```makefile
html:
    sphinx-build -b html . _build/html

clean:
    rm -rf _build
```

Or use a shell script:

```bash
#!/bin/bash
cd lib/mylib/docs
pip install -r requirements.txt
sphinx-build -b html . _build/html
```

## Documentation Dependencies

Create `docs/requirements.txt`:

```
sphinx>=7.0
sphinx-rtd-theme>=2.0
breathe>=4.35
exhale>=0.3
myst-parser>=2.0
```

## Checklist

- [ ] Minimal Doxygen comments on public APIs
- [ ] `docs/Doxyfile` configured for XML output
- [ ] `docs/conf.py` with Breathe and Exhale
- [ ] `docs/index.rst` as entry point
- [ ] `docs/design.md` with architecture overview
- [ ] `docs/examples.md` with usage examples
- [ ] `examples/` directory with working code
- [ ] Test targets in `xmake.lua`
- [ ] `docs/requirements.txt` for dependencies

## Example: umidi Library

The `lib/umidi` directory demonstrates this structure:

```
lib/umidi/
├── core/           # UMP, Parser, Result types
├── messages/       # Message wrappers
├── protocol/       # SysEx protocol
├── test/           # Comprehensive tests
├── examples/       # Working examples
├── docs/
│   ├── Doxyfile
│   ├── conf.py
│   ├── index.rst
│   ├── design.md
│   └── examples.md
└── doc/            # Additional markdown docs
```

Generate documentation:

```bash
cd lib/umidi/docs
pip install -r requirements.txt
sphinx-build -b html . _build/html
open _build/html/index.html
```
