# umimmio Naming Conventions

> See also: [README](../README.md) | [Usage Guide](USAGE.md)

## Overview

umimmio is a memory-mapped I/O library that provides type-safe register access. Register definitions follow **hardware datasheet naming conventions**, which typically use ALL_CAPS identifiers.

## Naming Rules

### MMIO Definitions (ALL_CAPS allowed)

| Element | Convention | Examples |
|---------|------------|----------|
| Device | ALL_CAPS | `USART1`, `GPIO`, `RCC`, `TIM2` |
| Block | ALL_CAPS | `APB1`, `AHB1` |
| Register | ALL_CAPS | `CR1`, `CR2`, `BRR`, `ISR`, `DR` |
| Field | ALL_CAPS | `UE`, `RE`, `TE`, `RXNE`, `TXE` |

### Library Implementation (standard naming)

| Element | Convention | Examples |
|---------|------------|----------|
| Classes/Structs | CamelCase | `Device`, `Block`, `Register`, `Field` |
| Functions/Methods | lower_case | `read()`, `write()`, `modify()` |
| Variables | lower_case | `base_address`, `bit_width` |
| Concepts | CamelCase | `RegionLike`, `TransportLike` |
| Namespaces | lower_case | `mm`, `mm::detail` |

## Rationale

Hardware register names like `USART1::CR1::UE` directly correspond to datasheet terminology:
- Improves readability when cross-referencing datasheets
- Reduces cognitive load for embedded developers
- Maintains consistency with vendor HAL/CMSIS definitions

## Code Style Enforcement

This directory has local `.clang-tidy` and `.clangd` configurations that allow ALL_CAPS for:
- Struct/Class names (register definitions)
- Member names (register fields)

The `IgnoredRegexp` pattern `^[A-Z][A-Z0-9_]*$` permits identifiers that are entirely uppercase with underscores.

### Configuration Files

```
lib/umimmio/
├── .clang-tidy    # CLI clang-tidy用
└── .clangd        # VSCode/clangd用
```

Both files override the project-wide strict naming rules **only for this directory**.

## Example

```cpp
namespace umi::stm32h7 {

/// USART1 device - follows RM0433 naming
struct USART1 : mm::Device<mm::RW, mm::DirectTransportTag> {
    static constexpr mm::Addr base_address = 0x4001'1000;

    struct CR1 : mm::Register<USART1, 0x00, 32> {
        struct UE     : mm::Field<CR1, 0, 1> {};   // USART enable
        struct RE     : mm::Field<CR1, 2, 1> {};   // Receiver enable
        struct TE     : mm::Field<CR1, 3, 1> {};   // Transmitter enable
    };

    struct BRR : mm::Register<USART1, 0x0C, 32> {};
};

} // namespace umi::stm32h7
```

## Directories with MMIO Naming Exception

The following directories have local `.clang-tidy` and `.clangd` configurations:

| Directory | Purpose |
|-----------|---------|
| `lib/umimmio/` | MMIO library core |
| `lib/umiport/mcu/` | MCU register definitions (STM32F4, STM32H7, etc.) |
| `lib/umiport/device/` | External device register definitions (codecs, etc.) |

## Adding MMIO Definitions Elsewhere

**Do not use NOLINT comments.** Instead:

1. **Recommended:** Place register definitions in one of the covered directories
2. **Alternative:** Create local `.clang-tidy` and `.clangd` in the new directory

Example `.clang-tidy` for a new MMIO directory:

```yaml
# See lib/umimmio/docs/NAMING.md for rationale
Checks: >
  -*,
  bugprone-*,-bugprone-dynamic-static-initializers,-bugprone-easily-swappable-parameters,
  clang-analyzer-*,
  performance-*,
  modernize-*,-modernize-use-trailing-return-type,
  readability-*,-readability-magic-numbers,-readability-uppercase-literal-suffix,-readability-identifier-length,
  misc-*

CheckOptions:
  - { key: readability-identifier-naming.StructIgnoredRegexp, value: '^[A-Z][A-Z0-9_]*$' }
  - { key: readability-identifier-naming.MemberIgnoredRegexp, value: '^[A-Z][A-Z0-9_]*$' }
  # ... (copy full config from lib/umimmio/.clang-tidy)
```

Example `.clangd`:

```yaml
Diagnostics:
  ClangTidy:
    CheckOptions:
      readability-identifier-naming.StructIgnoredRegexp: '^[A-Z][A-Z0-9_]*$'
      readability-identifier-naming.MemberIgnoredRegexp: '^[A-Z][A-Z0-9_]*$'
      # ... (copy full config from lib/umimmio/.clangd)
```
