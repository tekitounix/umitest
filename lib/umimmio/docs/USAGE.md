# umimmio Usage Guide

```cpp
// Recommended: umbrella header
#include <umimmio.hh>
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

umimmio provides a hierarchical type system for describing hardware registers:

```
Device
  └── Block (optional)
        └── Register
              └── Field
```

Each level inherits properties (base address, access policy, allowed transports) from its parent.

### Hierarchy Example

```cpp
struct MCU : mm::Device<mm::RW, mm::DirectTransportTag> {
    static constexpr mm::Addr base_address = 0x4000'0000;

    struct APB1 : mm::Block<MCU, 0x0000> {
        struct TIM2 : mm::Block<APB1, 0x0000> {
            struct CR1 : mm::Register<TIM2, 0x00, 32> {
                struct CEN : mm::Field<CR1, 0, 1> {};  // Counter enable
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
struct USART1 : mm::Device<> {
    static constexpr mm::Addr base_address = 0x4001'1000;
};

// I2C device
struct PCM3060 : mm::Device<mm::RW, mm::I2CTransportTag> {
    static constexpr mm::Addr base_address = 0x00;  // Register start
};

// Multiple transports allowed
struct EEPROM : mm::Device<mm::RW, mm::I2CTransportTag, mm::SPITransportTag> {
    static constexpr mm::Addr base_address = 0x00;
};
```

### Block

Optional grouping for large peripherals:

```cpp
struct GPIO : mm::Device<> {
    static constexpr mm::Addr base_address = 0x4800'0000;

    struct PORTA : mm::Block<GPIO, 0x0000> { /* registers */ };
    struct PORTB : mm::Block<GPIO, 0x0400> { /* registers */ };
};
```

### Register

Memory-mapped register with bit width:

```cpp
// Basic 32-bit register
struct CR1 : mm::Register<Parent, 0x00, 32> {};

// 16-bit register
struct DR : mm::Register<Parent, 0x04, 16> {};

// Read-only register
struct ISR : mm::Register<Parent, 0x1C, 32, mm::RO> {};

// Write-only register
struct TDR : mm::Register<Parent, 0x28, 32, mm::WO> {};

// Register with reset value
struct CFGR : mm::Register<Parent, 0x08, 32, mm::RW, 0x0000'0000> {};
```

### Field

Bit field within a register:

```cpp
struct CR1 : mm::Register<USART1, 0x00, 32> {
    // 1-bit field (auto-generates Set/Reset types)
    struct UE : mm::Field<CR1, 0, 1> {};   // bit 0
    
    // Multi-bit field
    struct PS : mm::Field<CR1, 9, 1> {};   // bit 9 (parity selection)
    struct PCE : mm::Field<CR1, 10, 1> {}; // bit 10 (parity control enable)
    
    // Multi-bit field with enumerated values
    struct M : mm::Field<CR1, 12, 2> {     // bits 12-13 (word length)
        using Bits8 = mm::Value<M, 0b00>;
        using Bits9 = mm::Value<M, 0b01>;
        using Bits7 = mm::Value<M, 0b10>;
    };
};
```

---

## Transport Layer

### DirectTransport (Memory-Mapped)

For MCU peripherals accessed via memory addresses:

```cpp
#include <transport/direct.hh>

mm::DirectTransport<> mcu;

// Read/write operations
mcu.write(USART1::CR1::UE::Set{});
auto val = mcu.read(USART1::ISR{});
```

### I2cTransport

For I2C-connected devices (codecs, sensors, etc.):

```cpp
#include <transport/i2c.hh>

// I2C driver must implement write() and write_read()
I2cDriver i2c;
mm::I2cTransport codec(i2c, 0x94);  // 8-bit register address, little-endian data (default)

// 16-bit register address + big-endian data
mm::I2cTransport<I2cDriver, std::true_type, mm::AssertOnError, std::uint16_t, mm::Endian::Big, mm::Endian::Big>
    codec16be(i2c, 0x94);

codec.write(PCM3060::REG64::MRST::Set{});
auto id = codec.read(PCM3060::REG64{});
```

### SpiTransport

For SPI-connected devices:

```cpp
#include <transport/spi.hh>

SpiDevice spi;
mm::SpiTransport<SpiDevice> spi_dev(spi);

spi_dev.write(EEPROM::REG0::value(0xAB));
auto v = spi_dev.read(EEPROM::REG0{});
```

### CheckPolicy

Enable/disable runtime range checks:

```cpp
// With runtime checks (default)
mm::DirectTransport<std::true_type> mcu_checked;

// Without runtime checks (for release builds)
mm::DirectTransport<std::false_type> mcu_unchecked;
```

---

## Register Operations

### write()

Write value to register or field:

```cpp
mm::DirectTransport<> mcu;

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

**注意**: `modify()` は read-modify-write であり **atomic ではありません**。  
割り込みやマルチスレッド環境ではクリティカルセクションで保護してください。

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
| `mm::RW` | ✅ | ✅ | Control registers |
| `mm::RO` | ✅ | ❌ | Status registers |
| `mm::WO` | ❌ | ✅ | Command registers |
| `mm::Inherit` | Parent | Parent | Default for fields |

### Compile-Time Enforcement

```cpp
struct STATUS : mm::Register<Device, 0x00, 32, mm::RO> {};
struct COMMAND : mm::Register<Device, 0x04, 32, mm::WO> {};

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
struct BAUD : mm::Field<BRR, 0, 16> {
    using Baud9600 = mm::Value<BAUD, 0x683>;
    using Baud115200 = mm::Value<BAUD, 0x08B>;
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
struct EN : mm::Field<CR, 0, 1> {};
// Automatically provides:
// EN::Set  = mm::Value<EN, 1>
// EN::Reset = mm::Value<EN, 0>

mcu.write(EN::Set{});
mcu.write(EN::Reset{});
```

---

## Advanced Usage

### Custom Transport

Implement your own transport:

```cpp
class SpiTransport : private mm::RegOps<SpiTransport> {
    friend class mm::RegOps<SpiTransport>;
    SpiDriver& spi;
    
public:
    using mm::RegOps<SpiTransport>::write;
    using mm::RegOps<SpiTransport>::read;
    using mm::RegOps<SpiTransport>::modify;
    using mm::RegOps<SpiTransport>::is;
    using mm::RegOps<SpiTransport>::flip;
    using TransportTag = mm::SPITransportTag;
    
    explicit SpiTransport(SpiDriver& s) : spi(s) {}
    
    template <typename Reg>
    auto reg_read(Reg) const noexcept -> typename Reg::RegValueType {
        // SPI read implementation
    }
    
    template <typename Reg>
    void reg_write(Reg, typename Reg::RegValueType value) const noexcept {
        // SPI write implementation
    }
};
```

### WM8731 Transport (9-bit registers)

WM8731 は 7-bit アドレス + 9-bit データの特殊I2Cプロトコルです。

```cpp
#include <wm8731/wm8731_transport.hh>

I2cDriver i2c;
mm::Wm8731Transport<decltype(i2c)> wm(i2c, 0x1A << 1);

wm.write(WM8731::POWER::value(0x1FF));
```

### BitBang Transport

GPIO を使った簡易I2C/SPI（低速・デバッグ用途）:

```cpp
#include <transport/bitbang_i2c.hh>
#include <transport/bitbang_spi.hh>

BitBangGpio gpio;
mm::BitBangI2cTransport<BitBangGpio> i2c_bb(gpio, 0x50);
mm::BitBangSpiTransport<BitBangGpio> spi_bb(gpio);
```

### Transport Constraints

Prevent wrong transport usage at compile time:

```cpp
// Device only allows I2C
struct Codec : mm::Device<mm::RW, mm::I2CTransportTag> { /* ... */ };

mm::DirectTransport<> mcu;
mm::I2cTransport codec_i2c(i2c_driver, 0x94);

mcu.write(Codec::REG::value(1));       // Compile error: DirectTransport not allowed
codec_i2c.write(Codec::REG::value(1)); // OK
```

### Register Reset Values

Use reset values for partial writes:

```cpp
struct CFGR : mm::Register<RCC, 0x08, 32, mm::RW, 0x0000'0000> {
    struct SW : mm::Field<CFGR, 0, 2> {};
    struct HPRE : mm::Field<CFGR, 4, 4> {};
};

// write() to field starts from reset value
mcu.write(CFGR::SW::value(0b10));  // Other bits get reset value (0)

// modify() preserves current register value
mcu.modify(CFGR::SW::value(0b10)); // Other bits unchanged
```

---

## Best Practices

### 1. Use Datasheet Names

```cpp
// ✅ Good: matches RM0433
struct USART1 : mm::Device<> {
    struct CR1 : mm::Register<USART1, 0x00, 32> {
        struct UE : mm::Field<CR1, 0, 1> {};
    };
};

// ❌ Bad: invented names
struct Uart1 : mm::Device<> {
    struct ControlReg1 : mm::Register<Uart1, 0x00, 32> {
        struct EnableBit : mm::Field<ControlReg1, 0, 1> {};
    };
};
```

### 2. Define Enumerated Values

```cpp
// ✅ Good: self-documenting
struct PLLSRC : mm::Field<PLLCFGR, 22, 2> {
    using HSI = mm::Value<PLLSRC, 0>;
    using HSE = mm::Value<PLLSRC, 1>;
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
struct I2CCodec : mm::Device<mm::RW, mm::I2CTransportTag> {};

// Access policy prevents mistakes
struct STATUS : mm::Register<Dev, 0x00, 32, mm::RO> {};

// Errors caught at compile time, not runtime
```

---

## Architecture

### Layer Diagram

```
┌─────────────────────────────────────────┐
│          Application Code               │
├─────────────────────────────────────────┤
│            RegOps API                   │
│  (write/modify/read/is/flip)            │
├─────────────┬───────────────────────────┤
│ ByteAdapter │    DirectTransport        │
│ (I2C/SPI)   │   (Memory-mapped)         │
├─────────────┴───────────────────────────┤
│    Register Definitions (Device)        │
│  Device → Block → Register → Field      │
│    (with transport constraints)         │
└─────────────────────────────────────────┘
```

### Address Calculation

All addresses are computed at **compile time**:

```cpp
struct STM32F4 : mm::Device<> {
    static constexpr mm::Addr base_address = 0x4000'0000;
    
    struct GPIOD : mm::Block<STM32F4, 0x0002'0C00> {
        // GPIOD::base_address = 0x4000'0000 + 0x0002'0C00 = 0x4002'0C00
        
        struct ODR : mm::Register<GPIOD, 0x14, 32> {
            // ODR::address = 0x4002'0C00 + 0x14 = 0x4002'0C14
        };
    };
};
```

---

## Type Safety

### Compile-Time Guarantees

| Check | Description | Example |
|-------|-------------|---------|
| **Bit width** | `value()` validates range at compile time | 4-bit field rejects value > 15 |
| **Access policy** | RO/WO/RW enforced | `write()` to RO register fails |
| **Transport constraint** | Device-level transport restriction | I2C device rejects DirectTransport |
| **[[nodiscard]]** | Unused values detected | `value()` result must be used |
| **1-bit field** | `flip()` restricted to 1-bit | Multi-bit flip is compile error |

### Runtime Protection (Optional)

When `CheckPolicy = std::true_type` (default):

```cpp
// Assert on out-of-range value
mcu.write(FIELD_4BIT::value(20));  // Runtime assert: 20 > 15

// Alignment check for DirectTransport
// static_assert at compile time if address is misaligned
```

### Error Policy

範囲チェック失敗時の挙動を切り替えられます（デフォルトは `AssertOnError`）。

```cpp
// assert で停止（デフォルト）
mm::DirectTransport<> mcu_default;

// trap 命令で停止
mm::DirectTransport<std::true_type, mm::TrapOnError> mcu_trap;

// 無視（最速・検証済みコード向け）
mm::DirectTransport<std::true_type, mm::IgnoreError> mcu_ignore;
```

Note: `value()` only triggers a compile-time error when the argument is a constant expression.
For runtime values, range checks are enforced by `CheckPolicy`.

Disable for release builds:

```cpp
mm::DirectTransport<std::false_type> mcu;  // No runtime checks
```

---

## Optimization

### Zero-Overhead Abstraction

- All functions are `noexcept` and `constexpr`-friendly
- Templates are fully inlined
- No virtual functions, no heap allocation
- Masks and shifts computed at compile time

### Single-Store Optimization

Multiple fields in the same register are combined into a single write:

```cpp
// This generates ONE store instruction:
mcu.write(
    GPIOD::MODER::MODE12::Output{},
    GPIOD::MODER::MODE13::Output{},
    GPIOD::MODER::MODE14::Output{},
    GPIOD::MODER::MODE15::Output{}
);

// Equivalent to:
// *GPIOD_MODER = (reset_value & ~mask12 & ~mask13 & ~mask14 & ~mask15)
//              | (1 << 24) | (1 << 26) | (1 << 28) | (1 << 30);
```

### Single RMW for modify()

Multiple field modifications use one Read-Modify-Write:

```cpp
// ONE read, ONE write:
mcu.modify(
    RCC::AHB1ENR::GPIOAEN::Set{},
    RCC::AHB1ENR::GPIOBEN::Set{},
    RCC::AHB1ENR::GPIOCEN::Set{}
);
```

---

## Practical Examples

### GPIO Configuration (STM32)

```cpp
struct STM32F4 : mm::Device<> {
    static constexpr mm::Addr base_address = 0x4000'0000;
    
    struct RCC : mm::Block<STM32F4, 0x0002'3800> {
        struct AHB1ENR : mm::Register<RCC, 0x30, 32> {
            struct GPIOAEN : mm::Field<AHB1ENR, 0, 1> {};
            struct GPIOBEN : mm::Field<AHB1ENR, 1, 1> {};
            struct GPIODEN : mm::Field<AHB1ENR, 3, 1> {};
        };
    };
    
    struct GPIOD : mm::Block<STM32F4, 0x0002'0C00> {
        struct MODER : mm::Register<GPIOD, 0x00, 32> {
            template<std::size_t Pin>
            struct MODE : mm::Field<MODER, Pin * 2, 2> {
                using Input = mm::Value<MODE, 0>;
                using Output = mm::Value<MODE, 1>;
                using Alternate = mm::Value<MODE, 2>;
                using Analog = mm::Value<MODE, 3>;
            };
            using MODE12 = MODE<12>;
            using MODE13 = MODE<13>;
            using MODE14 = MODE<14>;
            using MODE15 = MODE<15>;
        };
        
        struct ODR : mm::Register<GPIOD, 0x14, 32> {
            template<std::size_t Pin>
            using PIN = mm::Field<ODR, Pin, 1>;
            using PIN12 = PIN<12>;
            using PIN13 = PIN<13>;
            using PIN14 = PIN<14>;
            using PIN15 = PIN<15>;
        };
    };
};

// Usage: Configure and blink LEDs
mm::DirectTransport<> mcu;

// Enable GPIOD clock
mcu.modify(STM32F4::RCC::AHB1ENR::GPIODEN::Set{});

// Configure PD12-15 as outputs
mcu.modify(
    STM32F4::GPIOD::MODER::MODE12::Output{},
    STM32F4::GPIOD::MODER::MODE13::Output{},
    STM32F4::GPIOD::MODER::MODE14::Output{},
    STM32F4::GPIOD::MODER::MODE15::Output{}
);

// Turn on all LEDs
mcu.write(STM32F4::GPIOD::ODR::value(0xF000));

// Toggle single LED
mcu.flip(STM32F4::GPIOD::ODR::PIN12{});
```

### I2C Audio Codec

```cpp
struct CS43L22 : mm::Device<mm::RW, mm::I2CTransportTag> {
    static constexpr mm::Addr base_address = 0;
    
    struct ID : mm::Register<CS43L22, 0x01, 8, mm::RO> {};
    
    struct POWER_CTL1 : mm::Register<CS43L22, 0x02, 8> {
        using PowerDown = mm::Value<POWER_CTL1, 0x01>;
        using PowerUp = mm::Value<POWER_CTL1, 0x9E>;
    };
    
    struct POWER_CTL2 : mm::Register<CS43L22, 0x04, 8> {
        struct SPKA : mm::Field<POWER_CTL2, 0, 2> {
            using Off = mm::Value<SPKA, 0>;
            using On = mm::Value<SPKA, 3>;
        };
        struct SPKB : mm::Field<POWER_CTL2, 2, 2> {
            using Off = mm::Value<SPKB, 0>;
            using On = mm::Value<SPKB, 3>;
        };
    };
    
    struct MASTER_VOL_A : mm::Register<CS43L22, 0x20, 8> {};
    struct MASTER_VOL_B : mm::Register<CS43L22, 0x21, 8> {};
};

// Usage
I2cDriver i2c;
mm::I2cTransport<decltype(i2c)> codec(i2c, 0x94);

// Read chip ID
auto id = codec.read(CS43L22::ID{});

// Power up
codec.write(CS43L22::POWER_CTL1::PowerUp{});

// Enable speakers
codec.modify(
    CS43L22::POWER_CTL2::SPKA::On{},
    CS43L22::POWER_CTL2::SPKB::On{}
);

// Set volume
codec.write(CS43L22::MASTER_VOL_A::value(0xE0));
codec.write(CS43L22::MASTER_VOL_B::value(0xE0));
```

---

## Related Documentation

- [Naming Conventions](NAMING.md) — Code style for MMIO definitions
- [umiport Integration](../../umiport/docs/design/06-mmio-integration.md) — How umimmio fits into the umiport HAL layer
