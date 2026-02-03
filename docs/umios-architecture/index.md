# UMI-OS Architecture Documentation

## Quick Navigation

### I'm an...

- **[Application Developer](01-application/)** → Start with [Fundamentals](00-fundamentals/) → [Application Layer](01-application/)
- **[Kernel Developer](02-kernel/)** → Read everything, start with [Fundamentals](00-fundamentals/)
- **[BSP/Port Developer](03-port/)** → Focus on [Port](03-port/) + [Kernel](02-kernel/)

## Documentation Structure

| Section | Code Location | Description | Docs |
|---------|---------------|-------------|------|
| **[00-fundamentals/](00-fundamentals/)** | `lib/umi/core/` | Core concepts (AudioContext, Processor, Controller) | 3 |
| **[01-application/](01-application/)** | `lib/umi/app/` | App developer focused (Events, Parameters, MIDI) | 6 |
| **[02-kernel/](02-kernel/)** | `lib/umi/kernel/` | RTOS implementation (Scheduler, MPU, Boot) | 4 |
| **[03-port/](03-port/)** | `lib/umi/port/` | Platform-specific (Syscalls, Memory layout) | 2 |
| **[04-services/](04-services/)** | `lib/umi/service/`, `lib/umi/shell/` | System services (Shell, Updater, Storage) | 5 |
| **[05-binary/](05-binary/)** | `lib/umi/boot/`, `lib/umi/crypto/` | Binary format and security | 3 |
| **[06-dsp/](06-dsp/)** | `lib/umi/dsp/` | DSP modules (future integration) | - |
| **[07-usb/](07-usb/)** | `lib/umi/usb/` | USB Audio/MIDI (future integration) | - |
| **[08-gfx/](08-gfx/)** | `lib/umi/gfx/` | Graphics and UI (future integration) | - |
| **[09-midi/](09-midi/)** | `lib/umi/midi/` | MIDI implementation (future integration) | - |
| **[99-proposals/](99-proposals/)** | - | Design proposals | 2 |

## Implementation Status Dashboard

| Section | Status | Last Updated |
|---------|--------|--------------|
| Fundamentals | ✓ 実装済み | 2025-01 |
| Application | ✓ 実装済み | 2025-01 |
| Kernel | ✓ 実装済み | 2025-01 |
| Port | ✓ 実装済み | 2025-01 |
| Services | ✓ 実装済み | 2025-01 |
| Binary | ✓ 実装済み | 2025-01 |
| DSP | 💡 将来 | - |
| USB | 💡 将来 | - |
| GFX | 💡 将来 | - |
| MIDI | 💡 将来 | - |

## All Documents

### [00-fundamentals/](00-fundamentals/) - Core Concepts
- [00-overview](00-fundamentals/00-overview.md) - System overview and task model
- [01-audio-context](00-fundamentals/01-audio-context.md) - AudioContext unified specification
- [02-processor-controller](00-fundamentals/02-processor-controller.md) - Processor / Controller model

### [01-application/](01-application/) - Application Layer
- [03-event-system](01-application/03-event-system.md) - Event routing, queues, and classification
- [04-param-system](01-application/04-param-system.md) - Parameter system with SharedParamState
- [05-midi](01-application/05-midi.md) - MIDI integration (UMP, transport, SysEx)
- [08-backend-adapters](01-application/08-backend-adapters.md) - Platform adapters (embedded/WASM/Plugin)
- [10-shared-memory](01-application/10-shared-memory.md) - SharedMemory structure definition
- [21-config-mismatch](01-application/21-config-mismatch.md) - Configuration mismatch handling

### [02-kernel/](02-kernel/) - Kernel Implementation
- [11-scheduler](02-kernel/11-scheduler.md) - RT-Kernel scheduler and context switch
- [12-memory-protection](02-kernel/12-memory-protection.md) - MPU, fault handling, heap/stack monitoring
- [15-boot-sequence](02-kernel/15-boot-sequence.md) - Boot sequence from reset to RTOS start
- [16-app-loader](02-kernel/16-app-loader.md) - .umia validation, loading, and Processor registration

### [03-port/](03-port/) - Port Layer
- [06-syscall](03-port/06-syscall.md) - Syscall specification and numbering
- [07-memory](03-port/07-memory.md) - Memory layout for embedded targets

### [04-services/](04-services/) - System Services
- [13-system-services](04-services/13-system-services.md) - System services architecture overview
- [17-shell](04-services/17-shell.md) - Shell service (SysEx stdio, commands, auth)
- [18-updater](04-services/18-updater.md) - DFU over SysEx with rollback support
- [19-storage-service](04-services/19-storage-service.md) - Async filesystem (littlefs/FATfs)
- [20-diagnostics](04-services/20-diagnostics.md) - Kernel metrics, fault logs, LED patterns

### [05-binary/](05-binary/) - Binary Format & Security
- [09-app-binary](05-binary/09-app-binary.md) - App binary format specification (.umia/.umim)
- [14-security](05-binary/14-security.md) - Security and cryptography (Ed25519, SHA, CRC)

### [99-proposals/](99-proposals/) - Design Proposals
- [syscall-redesign](99-proposals/syscall-redesign.md) - Syscall redesign proposal
- [implementation-plan](99-proposals/implementation-plan.md) - Implementation tracking

## Document Status Legend

- ✓ **実装済み** - Fully implemented in current version
- 🚧 **新設計** - Spec finalized, implementation in progress
- 💡 **将来** - Future direction only

## Corresponding Code Structure

| Documentation | Code Location |
|---------------|---------------|
| 00-fundamentals/ | `lib/umi/core/` |
| 01-application/ | `lib/umi/app/` |
| 02-kernel/ | `lib/umi/kernel/` |
| 03-port/ | `lib/umi/port/` |
| 04-services/ | `lib/umi/service/`, `lib/umi/shell/` |
| 05-binary/ | `lib/umi/boot/`, `lib/umi/crypto/` |
| 06-dsp/ | `lib/umi/dsp/` |
| 07-usb/ | `lib/umi/usb/` |
| 08-gfx/ | `lib/umi/gfx/` |
| 09-midi/ | `lib/umi/midi/` |

## Legacy Information

This documentation was reorganized from a flat structure (00-21) to match the `lib/umi/` code structure. Previous versions used a numbered flat layout in the root of `docs/umios-architecture/`.
