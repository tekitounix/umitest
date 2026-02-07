# umirtm Design

[日本語](ja/DESIGN.md)

## 1. Vision

`umirtm` is a header-only Real-Time Monitor library for C++23:

1. Ring buffer layout is binary-compatible with SEGGER RTT protocol.
2. Debug output works on bare-metal, WASM, and host targets without modification.
3. Printf is a self-contained embedded implementation — no libc dependency.
4. `print()` provides `{}` placeholder syntax as a lightweight alternative to printf.
5. Host-side bridge utilities connect ring buffers to stdout and shared memory for desktop testing.

---

## 2. Non-Negotiable Requirements

### 2.1 RTT Protocol Compatibility

The control block layout (`rtm_control_block_t`) must be discoverable by SEGGER J-Link, pyOCD,
and OpenOCD RTT readers. The magic ID string, buffer descriptor array, and offset fields must
match the RTT specification.

### 2.2 Header-Only

All components are header files under `include/umirtm/`.
No static libraries, no generated code, no link-time registration.

### 2.3 No Heap Allocation

All ring buffer storage is statically allocated inside the `Monitor` class template.
No `new`, no `malloc`, no `std::vector`.

### 2.4 No Exceptions

Write/read operations are `noexcept`.
Buffer overflow handling is controlled by `Mode` (skip, trim, or block).

### 2.5 Dependency Boundaries

Layering is strict:

1. `umirtm` depends only on C++23 standard library headers (plus `<unistd.h>` for host printf).
2. `rtm_host.hh` additionally uses `<iostream>`, `<thread>`, `<chrono>` (host-only).
3. `tests/` depends on `umitest` for assertions.

Reference dependency graph:

```text
application    -> umirtm (rt::Monitor, rt::printf, rt::print)
umirtm/tests   -> umitest
```

### 2.6 Initialization Boundary

`Monitor::init(id)` must be called before any write/read.
It sets the control block magic ID and resets all buffer offsets.

---

## 3. Current Layout

```text
lib/umirtm/
├── README.md
├── xmake.lua
├── docs/
│   ├── INDEX.md
│   ├── DESIGN.md
│   ├── GETTING_STARTED.md
│   ├── USAGE.md
│   ├── EXAMPLES.md
│   ├── TESTING.md
│   └── ja/
├── examples/
│   ├── minimal.cc
│   ├── printf_demo.cc
│   └── print_demo.cc
├── include/umirtm/
│   ├── rtm.hh          # Monitor class + terminal colors
│   ├── printf.hh       # Embedded printf/snprintf implementation
│   ├── print.hh        # {} placeholder print helper
│   └── rtm_host.hh     # Host-side bridge (stdout, shared memory, TCP)
└── tests/
    ├── test_main.cc
    ├── test_monitor.cc
    ├── test_printf.cc
    ├── test_print.cc
    └── xmake.lua
```

---

## 4. Growth Layout

```text
lib/umirtm/
├── include/umirtm/
│   ├── rtm.hh
│   ├── printf.hh
│   ├── print.hh
│   ├── rtm_host.hh
│   └── format.hh        # Future: compile-time format string validation
├── examples/
│   ├── minimal.cc
│   ├── printf_demo.cc
│   ├── print_demo.cc
│   └── host_bridge.cc   # Future: host-side bridge usage demo
└── tests/
    ├── test_main.cc
    ├── test_*.cc
    └── xmake.lua
```

Notes:

1. Public headers stay under `include/umirtm/`.
2. `rtm_host.hh` is host-only (uses `<thread>`, POSIX APIs).
3. `printf.hh` is usable standalone without `rtm.hh` for embedded printf replacement.
4. Future format string validation would leverage `consteval` for compile-time safety.

---

## 5. Programming Model

### 5.0 API Reference

Public entrypoint: `include/umirtm/rtm.hh`

**Monitor API** (`rtm.hh`):

| Method | Description |
|--------|-------------|
| `rtm::init(id)` | Initialise with control block ID string |
| `rtm::write(str)` | Write string to up buffer. Returns bytes written |
| `rtm::log(str)` | Write string, discard return value |
| `rtm::read(span)` | Read from down buffer. Returns bytes read |
| `rtm::read_byte()` | Read one byte (-1 if empty) |
| `rtm::read_line(buf, len)` | Read line from down buffer |
| `rtm::get_available()` | Bytes pending in up buffer |
| `rtm::get_free_space()` | Free bytes in up buffer |

**Printf API** (`printf.hh`):

| Function | Description |
|----------|-------------|
| `rt::snprintf(buf, sz, fmt, ...)` | Format to buffer |
| `rt::vsnprintf(buf, sz, fmt, va)` | va_list variant |
| `rt::printf(fmt, ...)` | Format to stdout |

Supported specifiers: `%d`, `%u`, `%x`, `%X`, `%o`, `%c`, `%s`, `%p`, `%f`, `%e`, `%g`, `%%`

**Print API** (`print.hh`):

| Function | Description |
|----------|-------------|
| `rt::print(fmt, args...)` | `{}` placeholder output to stdout |
| `rt::println(fmt, args...)` | Same + newline |

### 5.1 Minimal Path

Required minimal flow:

1. Call `rtm::init("MY_RTM")`.
2. Call `rtm::write<0>("message")` or `rtm::log<0>("message")`.
3. Host debugger reads the up buffer.

### 5.2 Three Output Layers

**Layer 1: Raw ring buffer** (`rtm.hh`):

```cpp
rtm::init("MY_RTM");
rtm::log<0>("hello\n");
```

**Layer 2: Printf** (`printf.hh`):

```cpp
rt::printf("value = %d\n", 42);
```

**Layer 3: Print** (`print.hh`):

```cpp
rt::println("value = {}", 42);
```

### 5.3 Printf Configuration

Printf behavior is controlled by `PrintConfig` template:

```cpp
using Minimal = rt::PrintConfig<false, false, false, false, false, false, false, false>;
using Full    = rt::PrintConfig<true, true, true, true, true, true, false, true>;
```

Features can be individually disabled to reduce code size on embedded targets.

### 5.4 Advanced Path

Advanced usage includes:

1. multiple up/down channels for separate log streams,
2. down buffer reading for host-to-target commands,
3. line-oriented input via `read_line()`,
4. host-side bridge for desktop testing (`HostMonitor`),
5. shared memory export for RTT viewer integration on macOS.

---

## 6. Ring Buffer Architecture

### 6.1 Control Block

The control block contains:

1. 16-byte magic ID string (NUL-terminated, max 15 usable chars).
2. Up buffer descriptors (target → host).
3. Down buffer descriptors (host → target).

Each descriptor holds: name pointer, data pointer, size, write offset, read offset, flags.

### 6.2 Memory Ordering

- `write_up_buffer`: writes data first, then `release` fence, then updates `write_offset`.
- `read_down_buffer`: reads `write_offset` with `acquire` fence before reading data.
- Single-producer/single-consumer model — no mutex required.

### 6.3 Overflow Modes

| Mode | Behavior |
|------|----------|
| `NoBlockSkip` | Drop entire write if buffer is full (default) |
| `NoBlockTrim` | Write as much as fits, discard excess |
| `BlockIfFifoFull` | Spin until space is available |

---

## 7. Printf Specification

### 7.1 Supported Specifiers

`%d`, `%i`, `%u`, `%x`, `%X`, `%o`, `%c`, `%s`, `%p`, `%f`, `%e`, `%g`, `%a`, `%%`.

Optional (config-dependent): `%b`/`%B` (binary), `%n` (write-back).

### 7.2 Length Modifiers

`h`, `hh`, `l`, `ll` (if `use_large`), `j`, `z`, `t`, `L`.

### 7.3 Output Destinations

- `rt::printf()` — writes to stdout via `::write(1, ...)`.
- `rt::snprintf()` — writes to caller-provided buffer.
- `rt::vsnprintf()` — va_list version.

---

## 8. Test Strategy

1. Tests split by concern: monitor, printf, print.
2. Monitor tests verify write/read semantics, capacity limits, and buffer wrapping.
3. Printf tests verify format specifiers against expected output strings.
4. Print tests verify `{}` placeholder conversion and output.
5. All tests run on host via `xmake test`.
6. CI runs host tests on all supported platforms.

### 8.1 Test Layout

- `tests/test_main.cc`: test entrypoint
- `tests/test_monitor.cc`: Monitor write/read, capacity, buffer wrapping, overflow modes
- `tests/test_printf.cc`: printf/snprintf format specifiers and edge cases
- `tests/test_print.cc`: `{}` placeholder conversion and output

### 8.2 Running Tests

```bash
xmake test                    # all targets
xmake test 'test_umirtm/*'   # umirtm only
```

### 8.3 Quality Gates

- All host tests pass
- Monitor tests verify buffer integrity under boundary conditions
- Printf tests cover all enabled format specifiers per DefaultConfig
- Print tests verify `{}` → `%` conversion correctness

---

## 9. Example Strategy

Examples represent learning stages:

1. `minimal`: Monitor init and write.
2. `printf_demo`: all printf format specifiers demonstrated.
3. `print_demo`: `{}` placeholder print/println.

---

## 10. Near-Term Improvement Plan

1. Implement TCP server in `rtm_host.hh` for J-Link RTT over TCP.
2. Add compile-time format string validation via `consteval`.
3. Add `fmt::format_to` style API for zero-copy formatting into ring buffer.
4. Add Linux shared memory support (currently macOS only).

---

## 11. Design Principles

1. RTT-compatible — works with existing debug infrastructure.
2. Header-only — include and use, no build step.
3. Embedded-safe — no heap, no exceptions, no RTTI.
4. Layered output — raw → printf → print, pick the right level.
5. Configurable footprint — disable unused printf features to save code size.
