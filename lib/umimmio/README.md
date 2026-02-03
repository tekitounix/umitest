# umimmio

Type-safe Memory-Mapped I/O Library for C++23

## Features

- **Type-safe register access** — Compile-time checked read/write operations
- **Zero-overhead abstraction** — No runtime cost, all checks at compile time
- **Transport layer architecture** — Supports direct memory, I2C, SPI
- **Access policy enforcement** — RO/WO/RW enforced at compile time
- **Hardware datasheet naming** — Use UPPER_CASE names matching vendor documentation

## Quick Start

```cpp
#include <umimmio.hh>
#include <transport/direct.hh>

// Define device registers (matches STM32 datasheet naming)
struct USART1 : mm::Device<mm::RW, mm::DirectTransportTag> {
    static constexpr mm::Addr base_address = 0x4001'1000;

    struct CR1 : mm::Register<USART1, 0x00, 32> {
        struct UE : mm::Field<CR1, 0, 1> {};   // USART enable
        struct RE : mm::Field<CR1, 2, 1> {};   // Receiver enable
        struct TE : mm::Field<CR1, 3, 1> {};   // Transmitter enable
    };

    struct BRR : mm::Register<USART1, 0x0C, 32> {};
    struct ISR : mm::Register<USART1, 0x1C, 32, mm::RO> {};  // Read-only
};

// Usage
mm::DirectTransport<> mcu;

// Enable USART with TX and RX
mcu.write(USART1::CR1::UE::Set{}, USART1::CR1::TE::Set{}, USART1::CR1::RE::Set{});

// Set baud rate
mcu.write(USART1::BRR::value(0x683));  // 9600 baud @ 16MHz

// Check if USART is enabled
if (mcu.is(USART1::CR1::UE::Set{})) {
    // ...
}

// Read status register
auto status = mcu.read(USART1::ISR{});
```

## Documentation

- [Usage Guide](docs/USAGE.md) — Detailed API reference and examples
- [Naming Conventions](docs/NAMING.md) — Code style for MMIO definitions

## Directory Structure

```
lib/umimmio/
├── include/
│   ├── umimmio.hh           # Core library
│   └── transport/
│       ├── direct.hh        # Direct memory access
│       ├── i2c.hh           # I2C transport
│       ├── spi.hh           # SPI transport
│       ├── bitbang_i2c.hh   # Bit-bang I2C
│       └── bitbang_spi.hh   # Bit-bang SPI
├── docs/
│   ├── NAMING.md            # Naming conventions
│   └── USAGE.md             # Detailed usage guide
├── test/                    # Unit tests
├── .clang-tidy              # Local naming rules
└── .clangd                  # Local clangd config
```

## Requirements

- C++23 compiler (GCC 12+, Clang 16+)
- No external dependencies

## License

MIT
