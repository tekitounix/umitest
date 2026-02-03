# Services

## Overview

System services running on the kernel. Includes shell, updater, storage, and diagnostics services.

Corresponds to: `lib/umi/service/`, `lib/umi/shell/`

## Documents

| File | Description | Status |
|------|-------------|--------|
| [13-system-services](13-system-services.md) | System services architecture overview | 実装済み |
| [17-shell](17-shell.md) | Shell service (SysEx stdio, commands, auth) | 実装済み |
| [18-updater](18-updater.md) | DFU over SysEx with rollback support | 実装済み |
| [19-storage-service](19-storage-service.md) | Async filesystem (littlefs/FATfs) | 実装済み |
| [20-diagnostics](20-diagnostics.md) | Kernel metrics, fault logs, LED patterns | 実装済み |

## Quick Links

- [Previous: Port ←](../03-port/)
- [Next: Binary →](../05-binary/)

## Related Sections

- [Kernel](../02-kernel/) - Underlying RTOS
- [Port](../03-port/) - Platform abstraction
- [Binary](../05-binary/) - App loading
