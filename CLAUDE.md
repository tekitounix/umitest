# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

---

## CRITICAL RULES — Always Follow

### Workflow Rules

| Rule | Description |
|------|-------------|
| **NEVER implement during planning** | When asked to plan/investigate/design, produce ONLY analysis and approach confirmation. No code changes. |
| **NEVER finish on build success alone** | Firmware tasks require: build → flash → debugger verification → then complete |
| **MUST read existing code first** | Before modifying, understand current implementation. Don't blindly rewrite. |
| **MUST make incremental changes** | Large changes should be split into reviewable steps. |
| **MUST NOT revert to older implementations** | Never "fix" by rolling back to old code; it only returns to a previously working state and discards recent changes, so it has no value as a fix. |
| **MUST `git stash` before checkout** | Always stash before checking out past commits |
| **MUST check `git status` first** | Check status and current branch before any commit |
| **MUST ask before deleting files** | Confirm if commit is needed; use `trash` not `rm` |
| **Assume NOT a release** | Unless explicitly instructed, this is not a release commit |
| **Parallel work awareness** | Other work may be ongoing — commit carefully with branch/tag context |
| **MUST automate test/debug** | Tests and debugging must be fully automated via commands or scripts. Never ask the user to perform manual steps. |

### Code Style Rules

| Rule | Example |
|------|---------|
| **Standard** | C++23 |
| **Formatter** | clang-format (LLVM base, 4-space indent, 120 char line limit) |
| **Functions/methods/variables/constexpr** | `lower_case` ✓ — `camelCase` ✗ |
| **Types/classes/concepts** | `CamelCase` ✓ — `lower_case` ✗ |
| **Enum values** | `UPPER_CASE` |
| **Namespaces** | `lower_case` |
| **Member variables** | No prefix/suffix. NO `m_`, NO `_` suffix. Use `this->` if needed |
| **Pointers/references** | Left-aligned: `int* ptr` ✓ — `int *ptr` ✗ |
| **Error handling** | Prefer `Result<T>` or error codes. Avoid exceptions in kernel/audio paths. |
| **constexpr** | `constexpr` only — do NOT add redundant `inline` (C++17以降は暗黙的にinline) |

### Real-time Safety (process() / audio callbacks)

These are **hard constraints** — violation causes undefined behavior or audio glitches:

- **NEVER** allocate heap (`new`, `malloc`, `std::vector` growth)
- **NEVER** use blocking sync (`mutex`, `semaphore`)
- **NEVER** throw exceptions
- **NEVER** use stdio (`printf`, `cout`)

### Testing Rules

| Rule | Description |
|------|-------------|
| **Run tests after changes** | `xmake test` after modifying library code |
| **Run specific tests** | `xmake run test_kernel` for kernel changes |
| **Tests must pass** | Don't commit with failing tests |

### Debug Adapter Issues

- Unresponsive adapter is **NOT** a USB problem
- **MUST** check for orphaned processes: `pgrep -fl pyocd`, `pgrep -fl openocd`
- **MUST** kill by specific PID only — no broad patterns
- Use `pgrep -fl` to identify, then `kill <pid>`

---

## Project Overview

UMI (Universal Musical Instruments) is a universal audio framework enabling multi-target compilation from a single Processor code implementation to embedded, WASM, and desktop plugin platforms.

- One-source multi-target design (same C++ code for all platforms)
- C++23, concepts-based lightweight type system (no vtable overhead)
- Sample-accurate event processing, embedded-optimized (Cortex-M)

## Build System

Uses xmake with custom package repository (`xmake-repo/synthernet`).

```bash
# Host
xmake test                                     # Run all unit tests
xmake build test_kernel test_audio test_midi   # Build specific tests

# ARM embedded
xmake build stm32f4_kernel                     # STM32F4 kernel
xmake flash-kernel                             # Build and flash via pyOCD

# WASM
xmake build headless_webhost                   # WASM build

# Configuration
xmake f -m debug|release                       # Build mode
xmake f --board=stm32f4|stub                   # Target board

# Utility
xmake clean-all                                # Clean all build artifacts
xmake coding format                            # Format source files
```

### Custom Package Development (`xmake-repo/synthernet/`)

Rules and plugins are installed to `~/.xmake/` — source edits are NOT automatically picked up.

```bash
# Edit → sync → rebuild cycle
vim xmake-repo/synthernet/packages/a/arm-embedded/rules/vscode/modules/launch_generator.lua
xmake dev-sync                                  # Copy source to ~/.xmake/
rm -f build/.gens/rules/embedded.vscode.d       # Clear depend cache
xmake build <target>                            # Triggers regeneration
```

See `xmake-repo/synthernet/README.md` for full architecture and troubleshooting.

## Directory Structure

| Directory | Content |
|-----------|---------|
| `lib/` | Core libraries (OS-agnostic): umihal, umiport, umirtm, umibench, umitest, etc. |
| `examples/` | Target applications: stm32f4_kernel, synth_app, headless_webhost |
| `xmake-repo/synthernet/` | Custom xmake packages: arm-embedded (rules, plugins, MCU database) |
| `lib/docs/` | Standards and guides (see table below) |

## Documentation Reference

Read these **when the task matches** — not all at once.

| When you are... | Read this |
|-----------------|-----------|
| Modifying C++ code | `lib/docs/standards/CODING_RULE.md` — full style guide |
| Creating or restructuring a library | `lib/docs/standards/LIBRARY_SPEC.md` — namespace, directory, xmake conventions |
| Writing Doxygen comments | `lib/docs/standards/API_COMMENT_RULE.md` — comment format rules |
| Writing or modifying tests | `lib/docs/guides/TESTING_GUIDE.md` — test strategy and patterns |
| Debugging embedded targets | `lib/docs/guides/DEBUGGING_GUIDE.md` — pyOCD, GDB, RTT setup |
| Editing xmake-repo rules/plugins | `xmake-repo/synthernet/README.md` — package architecture and dev-sync |
| Working on AudioContext API | `docs/archive/DESIGN_CONTEXT_API.md` — API design decisions |
| Working on STM32F4 system design | `docs/archive/UMI_SYSTEM_ARCHITECTURE.md` — OS/app architecture |
| Understanding overall architecture | `docs/refs/ARCHITECTURE.md` — high-level design |
| Working on process() constraints | `docs/refs/API_APPLICATION.md` — real-time API rules |
