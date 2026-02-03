# umi::mmio Naming Conventions

> See also: [Usage Guide](USAGE.md)

## Overview

umi::mmio is a memory-mapped I/O library that provides type-safe register access. Register definitions follow **hardware datasheet naming conventions**, which typically use ALL_CAPS identifiers.

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
| Namespaces | lower_case | `umi::mmio` |

## Rationale

Hardware register names like `USART1::CR1::UE` directly correspond to datasheet terminology:
- Improves readability when cross-referencing datasheets
- Reduces cognitive load for embedded developers
- Maintains consistency with vendor HAL/CMSIS definitions

## Code Style

The library follows standard C++ naming conventions with one exception: hardware register definitions use ALL_CAPS to match datasheet naming.

### Local Configuration

If you need to configure clang-tidy for MMIO definitions in your project:

```yaml
# .clang-tidy
CheckOptions:
  - key: readability-identifier-naming.StructCase
    value: CamelCase
  - key: readability-identifier-naming.MemberCase
    value: lower_case
  # Allow ALL_CAPS for register definitions via IgnoredRegexp
```

### Example: Mixed Naming

```cpp
// Hardware register definition (ALL_CAPS - matches datasheet)
struct USART1 : umi::mmio::Device<> {
    static constexpr umi::mmio::Addr base_address = 0x4001'1000;
    
    struct CR1 : umi::mmio::Register<USART1, 0x00, 32> {
        struct UE : umi::mmio::Field<CR1, 0, 1> {};   // USART Enable
        struct TE : umi::mmio::Field<CR1, 3, 1> {};   // Transmitter Enable
        struct RE : umi::mmio::Field<CR1, 2, 1> {};   // Receiver Enable
    };
};

// Application code (standard naming)
void configure_usart(umi::mmio::DirectTransport<>& mcu) {
    auto baud_rate = calculate_baud(115200);  // lower_case variables
    mcu.write(USART1::BRR::value(baud_rate));  // ALL_CAPS for registers
    mcu.write(USART1::CR1::TE::Set{});         // ALL_CAPS for fields
}
```

## Transport Naming

Transport classes follow standard naming:

| Transport | Class Name |
|-----------|------------|
| Memory-mapped | `DirectTransport` |
| I2C | `I2cTransport` |
| SPI | `SpiTransport` |
| Bit-bang I2C | `BitBangI2cTransport` |
| Bit-bang SPI | `BitBangSpiTransport` |

## File Organization

```
lib/umi/mmio/
├── mmio.hh              # Umbrella header
├── register.hh          # Core register definitions
├── xmake.lua            # Build configuration
└── transport/
    ├── direct.hh        # DirectTransport
    ├── i2c.hh           # I2cTransport
    ├── spi.hh           # SpiTransport
    ├── bitbang_i2c.hh   # BitBangI2cTransport
    └── bitbang_spi.hh   # BitBangSpiTransport
```

## Testing Conventions

Test files follow the library naming:
- Test namespace: `test` (nested in appropriate namespaces)
- Mock classes: `Mock` + ClassName (e.g., `MockI2cBus`)
- Test devices: Use ALL_CAPS like real hardware

```cpp
// test_transport.hh
namespace test {
    struct TestDevice8 : umi::mmio::Device<umi::mmio::RW, umi::mmio::I2CTransportTag> {
        struct REG0 : umi::mmio::Register<TestDevice8, 0x00, 8> {};
    };
}
```
