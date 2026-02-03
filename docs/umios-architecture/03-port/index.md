# Port

## Overview

Platform-specific implementation layer. Contains syscall interface and memory layout definitions for embedded targets.

Corresponds to: `lib/umi/port/` (especially `port/mcu/`, `port/platform/`, `port/arch/`)

## Documents

| File | Description | Status |
|------|-------------|--------|
| [06-syscall](06-syscall.md) | Syscall specification and numbering | 実装済み |
| [07-memory](07-memory.md) | Memory layout for embedded targets | 実装済み |

## Quick Links

- [Previous: Kernel ←](../02-kernel/)
- [Next: Services →](../04-services/)

## Related Sections

- [Kernel](../02-kernel/) - RTOS implementation
- [Application](../01-application/) - Uses syscalls
- [Services](../04-services/) - System services
