# Binary

## Overview

Application binary format (.umia/.umim) specification and security features including Ed25519 signature verification.

Corresponds to: `lib/umi/kernel/app_header.hh`, `lib/umi/crypto/`, `lib/umi/boot/`

## Documents

| File | Description | Status |
|------|-------------|--------|
| [09-app-binary](09-app-binary.md) | App binary format specification (.umia/.umim) | 実装済み |
| [14-security](14-security.md) | Security and cryptography (Ed25519, SHA, CRC) | 実装済み |
| [21-config-mismatch](21-config-mismatch.md) | Configuration mismatch handling | 実装済み |

## Quick Links

- [Previous: Services ←](../04-services/)
- [Next: Proposals →](../99-proposals/)

## Related Sections

- [Kernel](../02-kernel/) - App loading
- [Services](../04-services/) - Update service
- [Port](../03-port/) - Memory layout
