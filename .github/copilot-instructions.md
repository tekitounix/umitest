# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

---

## ⚠️ CRITICAL RULES — Always Follow

### Workflow Rules

| Rule                                         | Description                                                                                                                                      |
| -------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------ |
| **NEVER implement during planning**          | When asked to plan/investigate/design, produce ONLY analysis and approach confirmation. No code changes.                                         |
| **NEVER finish on build success alone**      | Firmware tasks require: build → flash → debugger verification → then complete                                                                    |
| **MUST read existing code first**            | Before modifying, understand current implementation. Don't blindly rewrite.                                                                      |
| **MUST make incremental changes**            | Large changes should be split into reviewable steps.                                                                                             |
| **MUST NOT revert to older implementations** | Never “fix” by rolling back to old code; it only returns to a previously working state and discards recent changes, so it has no value as a fix. |
| **MUST `git stash` before checkout**         | Always stash before checking out past commits                                                                                                    |
| **MUST check `git status` first**            | Check status and current branch before any commit                                                                                                |
| **MUST ask before deleting files**           | Confirm if commit is needed; use `trash` not `rm`                                                                                                |
| **Assume NOT a release**                     | Unless explicitly instructed, this is not a release commit                                                                                       |
| **Parallel work awareness**                  | Other work may be ongoing — commit carefully with branch/tag context                                                                             |
| **MUST automate test/debug**                 | Tests and debugging must be fully automated via commands or scripts. Never ask the user to perform manual steps.                                 |

### Planning Phase Checklist

When planning (before any implementation):

1. Read relevant docs (Architecture, Design, Debug Guide)
2. Identify files & APIs to touch
3. Confirm build/flash/test path for the target
4. Define done criteria (what will be verified)
5. Get confirmation before proceeding to implementation

### Code Style Rules

| Rule                                      | Example                                                                      |
| ----------------------------------------- | ---------------------------------------------------------------------------- |
| **Standard**                              | C++23                                                                        |
| **Formatter**                             | clang-format (LLVM base, 4-space indent, 120 char line limit)                |
| **Functions/methods/variables/constexpr** | `lower_case` ✓ — `camelCase` ✗                                               |
| **Types/classes/concepts**                | `CamelCase` ✓ — `lower_case` ✗                                               |
| **Enum values**                           | `UPPER_CASE`                                                                 |
| **Namespaces**                            | `lower_case`                                                                 |
| **Member variables**                      | No prefix/suffix. NO `m_`, NO `_` suffix. Use `this->` if needed             |
| **Pointers/references**                   | Left-aligned: `int* ptr` ✓ — `int *ptr` ✗                                    |
| **Error handling**                        | Prefer `Result<T>` or error codes. Avoid exceptions in kernel/audio paths.   |
| **constexpr**                             | `constexpr` only — do NOT add redundant `inline` (C++17以降は暗黙的にinline) |

### Real-time Safety (process() / audio callbacks)

These are **hard constraints** — violation causes undefined behavior or audio glitches:

- **NEVER** allocate heap (`new`, `malloc`, `std::vector` growth)
- **NEVER** use blocking sync (`mutex`, `semaphore`)
- **NEVER** throw exceptions
- **NEVER** use stdio (`printf`, `cout`)

### Testing Rules

| Rule                        | Description                                |
| --------------------------- | ------------------------------------------ |
| **Run tests after changes** | `xmake test` after modifying library code  |
| **Run specific tests**      | `xmake run test_kernel` for kernel changes |
| **Tests must pass**         | Don't commit with failing tests            |

### Debug Adapter Issues

- Unresponsive adapter is **NOT** a USB problem
- **MUST** check for orphaned processes: `pgrep -fl pyocd`, `pgrep -fl openocd`
- **MUST** kill by specific PID only — no broad patterns
- Use `pgrep -fl` to identify, then `kill <pid>`

---

## Project Overview

UMI (Universal Musical Instruments) is a universal audio framework enabling multi-target compilation from a single Processor code implementation to embedded, WASM, and desktop plugin platforms.

Key characteristics:

- One-source multi-target design (same C++ code for all platforms)
- Modular, patchable DSP architecture
- C++23, heavily using C++20 concepts-based lightweight type system (no vtable overhead)
- Sample-accurate event processing
- Embedded-optimized (Cortex-M support)

## Build System

Uses xmake with custom package repository (`.refs/arm-embedded-xmake-repo`) for embedded build automation.

**Custom Packages:**

- `arm-embedded`: ARM cross-compiler (gcc-arm and armclang supported), embedded rules, VSCode integration
- `coding-rules`: clangd/clang-tidy/clang-format configuration automation

```bash
# Host build and test
xmake build test_kernel test_audio test_midi  # Build specific host tests
xmake test                                     # Run host unit tests

# ARM embedded targets (auto-detects toolchain)
xmake build stm32f4_kernel    # STM32F4 kernel
xmake build synth_app         # Synth application (.umia)
xmake build firmware          # Basic embedded example
xmake flash-kernel            # Build and flash kernel via pyOCD
xmake flash-synth-app         # Build and flash synth app

# WASM (requires Emscripten)
xmake build headless_webhost  # WASM build
xmake webhost                  # Build WASM and copy to web/
xmake webhost-serve            # Build and start local server

# Renode emulator
xmake renode                   # Interactive Renode session
xmake renode-test              # Automated Renode tests
xmake robot                    # Robot Framework tests

# Code style (from coding-rules package)
xmake coding init              # Generate .clangd, .clang-tidy, .clang-format
xmake coding format            # Format source files

# Run a single test binary
xmake run test_kernel

# Configuration options
xmake f -m debug|release       # Build mode
xmake f --board=stm32f4|stub   # Target board
xmake f --kernel=mono|micro    # Kernel variant

# After config changes
xmake build <target>           # Rebuild required after xmake f

# Utility
xmake clean-all                # Clean all build artifacts
xmake info                     # Show build configuration
```

**Dependencies:**

- Add packages via `add_requires` in `xmake.lua`
- If a package is missing, add it to `.refs/arm-embedded-xmake-repo` and sync to the remote repo

## Directory Structure

- `docs/`: Specs, guides, references
- `examples/`: Sample applications and targets
- `lib/`: Core libraries (OS-agnostic)
- `port/`: Platform-specific ports
- `tests/`: Host unit tests and test utilities
- `tools/`: Build helpers, Renode scripts
- `third_party/`: External dependencies
- `xmake.lua`: Main build configuration

## Architecture

### Core Libraries (`lib/`)

- **umios/core/**: Fundamental types — AudioContext, Event, Processor concept
- **umios/kernel/**: STM32F4 kernel, loader, MPU configuration
- **umios/backend/**: Platform backends (cm for Cortex-M, wasm)
- **umidsp/**: DSP components (oscillators, filters, envelopes) with optimized implementations
- **umidi/**: MIDI support (UMP, MIDI 1.0, SysEx)
- **umiboot/**: Bootloader and firmware verification
- **umisynth/**: Common synthesizer implementations

### OS/Application Architecture (Embedded)

Complete binary separation between OS and application (`.umia`):

**Task Priorities:**

- Realtime (0): Audio processing, DMA callbacks
- Server (1): Drivers, I/O handlers (shell, USB SysEx)
- User (2): Application tasks (main as Controller task)
- Idle (3): Background, sleep management

**Execution Model:**

- `main()` runs as Controller task, registers Processor tasks at init via syscall
- After init, main loop or coroutine runtime for cooperative multitasking
- ISRs only notify server tasks; actual processing happens in tasks

**OS/App Communication:**

- Syscalls: `RegisterProc`, `WaitEvent`, `Yield`, `GetTime`, `Sleep`, `GetShared`, `MidiSend/Recv`, etc.
- Shared memory (`_shared_start`): Buffer exchange between OS and app
- OS provides: memory protection (MPU), heap allocation/monitoring, `std::chrono::now`

**MIDI as Central Hub:**

- OS handles MIDI from all transports (USB, UART) uniformly
- App receives MIDI as events through syscall
- SysEx protocol carries: stdio, DFU, profiler, test functions

**OS-side Applications:**

- Shell, updater run as OS-side apps using same syscall/shared memory interface

### Core APIs

**AudioContext** (`lib/umios/core/audio_context.hh`): Unified interface for sample-accurate audio processing. Provides I/O buffers (std::span), events, and timing information.

**Processor Concept** (`lib/umios/core/processor.hh`):

```cpp
template<typename P>
concept ProcessorLike = requires(P& p, AudioContext& ctx) {
    { p.process(ctx) } -> std::same_as<void>;
};
```

No inheritance required, no vtable. Extended via Port/Parameter descriptors.

**Event System** (`lib/umios/core/event.hh`): EventType includes Midi, Param, Raw, ButtonDown/Up. Sample-accurate processing with buffer position specification.

### Examples (`examples/`)

- **stm32f4_kernel/**: STM32F4 kernel with full OS implementation
- **synth_app/**: Multi-platform synthesizer (app binary)
- **headless_webhost/**: Web WASM implementation

## Quick Start

**Host (unit tests):**

```bash
xmake build test_kernel test_audio test_midi
xmake test
```

**STM32F4 (kernel/app):**

```bash
xmake build stm32f4_kernel
xmake build synth_app
xmake flash-kernel
xmake flash-synth-app
```

**WASM:**

```bash
xmake build headless_webhost
xmake webhost
xmake webhost-serve
```

## Testing

- **Framework:** Minimal in-tree framework (`tests/test_common.hh`, no exceptions/RTTI)
- **Location:** `tests/`
- **Naming:** `test_*.cc`
- **Run:** `xmake run test_kernel`
- **Details:** `docs/refs/guides/TESTING.md`

## Key Documentation

| Document                                 | Content                       |
| ---------------------------------------- | ----------------------------- |
| `docs/refs/specs/ARCHITECTURE.md`        | High-level architecture       |
| `docs/new/UMI_SYSTEM_ARCHITECTURE.md`    | STM32F4 detailed design       |
| `lib/docs/CODING_STYLE.md`               | Detailed style guide          |
| `lib/docs/LIBRARY_STRUCTURE.md`          | Library structure conventions |
| `lib/docs/TESTING.md`                    | Test strategy                 |
| `lib/docs/DEBUG_GUIDE.md`                | Debugging (pyOCD, GDB, RTT)   |
| `docs/new/DESIGN_CONTEXT_API.md`         | AudioContext API design       |
| `docs/refs/reference/API_APPLICATION.md` | `process()` constraints       |
