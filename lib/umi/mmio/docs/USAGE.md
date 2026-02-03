# umi::mmio Usage Guide

```cpp
// Recommended: umbrella header
#include <umi/mmio/mmio.hh>
```

## Table of Contents

- [Core Concepts](#core-concepts)
- [Defining Registers](#defining-registers)
- [Transport Layer](#transport-layer)
- [Register Operations](#register-operations)
- [Access Policies](#access-policies)
- [Value Types](#value-types)
- [Advanced Usage](#advanced-usage)
- [Best Practices](#best-practices)
- [Architecture](#architecture)
- [Type Safety](#type-safety)
- [Optimization](#optimization)
- [Practical Examples](#practical-examples)
- [Related Documentation](#related-documentation)

---

## Core Concepts

umi::mmio provides a hierarchical type system for describing hardware registers:

```
Device
  └── Block (optional)
        └── Register
              └── Field
```

Each level inherits properties (base address, access policy, allowed transports) from its parent.

### Hierarchy Example

```cpp
struct MCU : umi::mmio::Device<umi::mmio::RW, umi::mmio::DirectTransportTag> {
    static constexpr umi::mmio::Addr base_address = 0x4000'0000;

    struct APB1 : umi::mmio::Block<MCU, 0x0000> {
        struct TIM2 : umi::mmio::Block<APB1, 0x0000> {
            struct CR1 : umi::mmio::Register<TIM2, 0x00, 32> {
                struct CEN : umi::mmio::Field<CR1, 0, 1> {};  // Counter enable
            };
        };
    };
};
```

---

## Defining Registers

### Device

Top-level container with transport constraints:

```cpp
// Direct memory access only (default)
struct USART1 : umi::mmio::Device<> {
    static constexpr umi::mmio::Addr base_address = 0x4001'1000;
};

// I2C device
struct PCM3060 : umi::mmio::Device<umi::mmio::RW, umi::mmio::I2CTransportTag> {
    static constexpr umi::mmio::Addr base_address = 0x00;  // Register start
};

// Multiple transports allowed
struct EEPROM : umi::mmio::Device<umi::mmio::RW, umi::mmio::I2CTransportTag, umi::mmio::SPITransportTag> {
    static constexpr umi::mmio::Addr base_address = 0x00;
};
```

### Block

Optional grouping for large peripherals:

```cpp
struct GPIO : umi::mmio::Device<> {
    static constexpr umi::mmio::Addr base_address = 0x4800'0000;

    struct PORTA : umi::mmio::Block<GPIO, 0x0000> { /* registers */ };
    struct PORTB : umi::mmio::Block<GPIO, 0x0400> { /* registers */ };
};
```

### Register

Memory-mapped register with bit width:

```cpp
// Basic 32-bit register
struct CR1 : umi::mmio::Register<Parent, 0x00, 32> {};

// 16-bit register
struct DR : umi::mmio::Register<Parent, 0x04, 16> {};

// Read-only register
struct ISR : umi::mmio::Register<Parent, 0x1C, 32, umi::mmio::RO> {};

// Write-only register
struct TDR : umi::mmio::Register<Parent, 0x28, 32, umi::mmio::WO> {};

// Register with reset value
struct CFGR : umi::mmio::Register<Parent, 0x08, 32, umi::mmio::RW, 0x0000'0000> {};
```

### Field

Bit field within a register:

```cpp
struct CR1 : umi::mmio::Register<USART1, 0x00, 32> {
    // 1-bit field (auto-generates Set/Reset types)
    struct UE : umi::mmio::Field<CR1, 0, 1> {};   // bit 0
    
    // Multi-bit field
    struct PS : umi::mmio::Field<CR1, 9, 1> {};   // bit 9 (parity selection)
    struct PCE : umi::mmio::Field<CR1, 10, 1> {}; // bit 10 (parity control enable)
    
    // Multi-bit field with enumerated values
    struct M : umi::mmio::Field<CR1, 12, 2> {     // bits 12-13 (word length)
        using Bits8 = umi::mmio::Value<M, 0b00>;
        using Bits9 = umi::mmio::Value<M, 0b01>;
        using Bits7 = umi::mmio::Value<M, 0b10>;
    };
};
```

---

## Transport Layer

### DirectTransport (Memory-Mapped)

For MCU peripherals accessed via memory addresses:

```cpp
#include <umi/mmio/transport/direct.hh>

umi::mmio::DirectTransport<> mcu;

// Read/write operations
mcu.write(USART1::CR1::UE::Set{});
auto val = mcu.read(USART1::ISR{});
```

### I2cTransport

For I2C-connected devices (codecs, sensors, etc.):

```cpp
#include <umi/mmio/transport/i2c.hh>

// I2C driver must implement write() and write_read()
I2cDriver i2c;
umi::mmio::I2cTransport codec(i2c, 0x94);  // 8-bit register address, little-endian data (default)

// 16-bit register address + big-endian data
umi::mmio::I2cTransport<I2cDriver, std::true_type, umi::mmio::AssertOnError, std::uint16_t, umi::mmio::Endian::Big, umi::mmio::Endian::Big>
    codec16be(i2c, 0x94);

codec.write(PCM3060::REG64::MRST::Set{});
auto id = codec.read(PCM3060::REG64{});
```

### SpiTransport

For SPI-connected devices:

```cpp
#include <umi/mmio/transport/spi.hh>

SpiDevice spi;
umi::mmio::SpiTransport<SpiDevice> spi_dev(spi);

spi_dev.write(EEPROM::REG0::value(0xAB));
auto v = spi_dev.read(EEPROM::REG0{});
```

### CheckPolicy

Enable/disable runtime range checks:

```cpp
// With runtime checks (default)
umi::mmio::DirectTransport<std::true_type> mcu_checked;

// Without runtime checks (for release builds)
umi::mmio::DirectTransport<std::false_type> mcu_unchecked;
```

---

## Register Operations

### write()

Write value to register or field:

```cpp
umi::mmio::DirectTransport<> mcu;

// Write to register with value()
mcu.write(USART1::BRR::value(0x683));

// Write enumerated value
mcu.write(USART1::CR1::M::Bits8{});

// Write to 1-bit field
mcu.write(USART1::CR1::UE::Set{});
mcu.write(USART1::CR1::UE::Reset{});

// Write multiple fields at once (single bus transaction)
mcu.write(
    USART1::CR1::UE::Set{},
    USART1::CR1::TE::Set{},
    USART1::CR1::RE::Set{}
);
```

### read()

Read register or field value:

```cpp
// Read entire register
auto cr1 = mcu.read(USART1::CR1{});

// Read field (returns field-width type)
auto word_length = mcu.read(USART1::CR1::M{});  // Returns 2-bit value
auto is_enabled = mcu.read(USART1::CR1::UE{});  // Returns 1-bit value
```

### modify()

Read-Modify-Write operation:

```cpp
// Modify single field (preserves other bits)
mcu.modify(USART1::CR1::UE::Set{});

// Modify multiple fields (single RMW)
mcu.modify(
    USART1::CR1::UE::Set{},
    USART1::CR1::TE::Reset{}
);
```

**Note**: `modify()` is read-modify-write and **NOT atomic**.  
Protect with critical section in interrupt/multi-threaded environments.

### is()

Check if field matches value:

```cpp
// Check enumerated value
if (mcu.is(USART1::CR1::M::Bits8{})) {
    // 8-bit mode
}

// Check 1-bit field
if (mcu.is(USART1::CR1::UE::Set{})) {
    // USART enabled
}

// Check dynamic value
if (mcu.is(USART1::CR1::M::value(0b01))) {
    // 9-bit mode
}
```

### flip()

Toggle 1-bit field:

```cpp
mcu.flip(USART1::CR1::UE{});  // Toggle enable bit
```

---

## Access Policies

### Policy Types

| Policy | Read | Write | Use Case |
|--------|------|-------|----------|
| `umi::mmio::RW` | ✅ | ✅ | Control registers |
| `umi::mmio::RO` | ✅ | ❌ | Status registers |
| `umi::mmio::WO` | ❌ | ✅ | Command registers |
| `umi::mmio::Inherit` | Parent | Parent | Default for fields |

### Compile-Time Enforcement

```cpp
struct STATUS : umi::mmio::Register<Device, 0x00, 32, umi::mmio::RO> {};
struct COMMAND : umi::mmio::Register<Device, 0x04, 32, umi::mmio::WO> {};

mcu.read(STATUS{});   // OK
mcu.write(STATUS::value(0));  // Compile error: Cannot write to read-only

mcu.write(COMMAND::value(1)); // OK
mcu.read(COMMAND{});  // Compile error: Cannot read from write-only
```

---

## Value Types

### Static Values (Enumerated)

Compile-time constant values:

```cpp
struct BAUD : umi::mmio::Field<BRR, 0, 16> {
    using Baud9600 = umi::mmio::Value<BAUD, 0x683>;
    using Baud115200 = umi::mmio::Value<BAUD, 0x08B>;
};

mcu.write(BAUD::Baud9600{});  // Type-safe, no runtime overhead
```

### Dynamic Values

Runtime-determined values:

```cpp
uint16_t calculated_baud = compute_baud(clock, target_rate);
mcu.write(BRR::BAUD::value(calculated_baud));

// Range checking (if CheckPolicy enabled)
mcu.write(FIELD4BIT::value(20));  // Assert: value > 15 for 4-bit field
```

### 1-bit Field Auto Types

1-bit fields automatically get `Set` and `Reset` types:

```cpp
struct EN : umi::mmio::Field<CR, 0, 1> {};
// Automatically provides:
// EN::Set  = umi::mmio::Value<EN, 1>
// EN::Reset = umi::mmio::Value<EN, 0>

mcu.write(EN::Set{});
mcu.write(EN::Reset{});
```

---

## Best Practices

### 1. Use Datasheet Names

```cpp
// ✅ Good: matches RM0433
struct USART1 : umi::mmio::Device<> {
    struct CR1 : umi::mmio::Register<USART1, 0x00, 32> {
        struct UE : umi::mmio::Field<CR1, 0, 1> {};
    };
};

// ❌ Bad: invented names
struct Uart1 : umi::mmio::Device<> {
    struct ControlReg1 : umi::mmio::Register<Uart1, 0x00, 32> {
        struct EnableBit : umi::mmio::Field<ControlReg1, 0, 1> {};
    };
};
```

### 2. Define Enumerated Values

```cpp
// ✅ Good: self-documenting
struct PLLSRC : umi::mmio::Field<PLLCFGR, 22, 2> {
    using HSI = umi::mmio::Value<PLLSRC, 0>;
    using HSE = umi::mmio::Value<PLLSRC, 1>;
};
mcu.write(PLLSRC::HSE{});

// ❌ Bad: magic numbers
mcu.write(PLLSRC::value(1));
```

### 3. Group Related Operations

```cpp
// ✅ Good: single write for related fields
mcu.write(
    CR1::UE::Set{},
    CR1::TE::Set{},
    CR1::RE::Set{}
);

// ❌ Bad: multiple writes
mcu.write(CR1::UE::Set{});
mcu.write(CR1::TE::Set{});
mcu.write(CR1::RE::Set{});
```

### 4. Use modify() for Partial Updates

```cpp
// ✅ Good: preserves other bits
mcu.modify(CR1::UE::Set{});

// ⚠️ Caution: write() uses reset value, may clear other bits
mcu.write(CR1::UE::Set{});
```

### 5. Leverage Compile-Time Checks

```cpp
// Transport constraint prevents mistakes
struct I2CCodec : umi::mmio::Device<umi::mmio::RW, umi::mmio::I2CTransportTag> {};

// Access policy prevents mistakes
struct STATUS : umi::mmio::Register<Dev, 0x00, 32, umi::mmio::RO> {};

// Errors caught at compile time, not runtime
```

---

## Related Documentation

- [Naming Conventions](NAMING.md) — Code style for MMIO definitions
