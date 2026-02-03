# Kernel

## Overview

RTOS kernel implementation for embedded targets. Includes scheduler, memory protection, boot sequence, and application loader.

Corresponds to: `lib/umi/kernel/` and `lib/umi/port/`

## Documents

| File | Description | Status |
|------|-------------|--------|
| [11-scheduler](11-scheduler.md) | RT-Kernel scheduler and context switch | 実装済み |
| [12-memory-protection](12-memory-protection.md) | MPU, fault handling, heap/stack monitoring | 実装済み |
| [15-boot-sequence](15-boot-sequence.md) | Boot sequence from reset to RTOS start | 実装済み |
| [16-app-loader](16-app-loader.md) | .umia validation, loading, and Processor registration | 実装済み |

## Quick Links

- [Previous: Application Layer ←](../01-application/)
- [Next: Port →](../03-port/)

## Related Sections

- [Fundamentals](../00-fundamentals/) - Core abstractions
- [Port](../03-port/) - Platform-specific code
- [Services](../04-services/) - System services layer
- [Binary](../05-binary/) - App binary format and security
