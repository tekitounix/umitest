# Application Layer

## Overview

Documentation for application developers. Covers event system, parameter handling, MIDI integration, and backend adapters for multi-target deployment.

Corresponds to: `lib/umi/app/`

## Documents

| File | Description | Status |
|------|-------------|--------|
| [03-event-system](03-event-system.md) | Event routing, queues, and classification | 実装済み |
| [04-param-system](04-param-system.md) | Parameter system with SharedParamState | 実装済み |
| [05-midi](05-midi.md) | MIDI integration (UMP, transport, SysEx) | 実装済み |
| [08-backend-adapters](08-backend-adapters.md) | Platform adapters (embedded/WASM/Plugin) | 実装済み |
| [10-shared-memory](10-shared-memory.md) | SharedMemory structure definition | 実装済み |
| [21-config-mismatch](21-config-mismatch.md) | Configuration mismatch handling | 実装済み |

## Quick Links

- [Previous: Fundamentals ←](../00-fundamentals/)
- [Next: Kernel →](../02-kernel/)

## Related Sections

- [Fundamentals](../00-fundamentals/) - Core concepts
- [Port](../03-port/) - Platform abstraction layer
- [Services](../04-services/) - System services
