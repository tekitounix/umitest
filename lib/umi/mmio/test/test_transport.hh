#pragma once
#include <array>
#include <cassert>
#include <cstring>

#include "../mmio.hh"

namespace test {

// Mock I2C bus for testing
class MockI2cBus {
    // Simulated device memory (address -> memory)
    struct DeviceMemory {
        std::array<std::uint8_t, 256> data{};
    };

    std::array<DeviceMemory, 128> devices{}; // 7-bit addressing

  public:
    using AddressType = std::uint8_t;

    // Simple write
    void write(std::uint8_t device_addr, std::uint8_t reg_addr, const void* data, std::size_t size) noexcept {
        auto& dev = devices[device_addr & 0x7F];
        std::memcpy(&dev.data[reg_addr], data, size);
    }

    // Write then read (typical I2C pattern)
    void write_then_read(std::uint8_t device_addr,
                         const void* tx_data,
                         std::size_t tx_size,
                         void* rx_data,
                         std::size_t rx_size) noexcept {
        if (tx_size == 1) {
            // Standard register read
            auto reg_addr = *static_cast<const std::uint8_t*>(tx_data);
            auto& dev = devices[device_addr & 0x7F];
            std::memcpy(rx_data, &dev.data[reg_addr], rx_size);
        }
    }

    // Direct memory access for testing
    std::uint8_t& operator()(std::uint8_t device_addr, std::uint8_t reg_addr) {
        return devices[device_addr & 0x7F].data[reg_addr];
    }
};

// I2C transport implementation
template <typename I2cBus, typename CheckPolicy = std::true_type>
class I2cTransport : public umi::mmio::ByteAdapter<I2cTransport<I2cBus, CheckPolicy>, CheckPolicy> {
    I2cBus& bus;
    std::uint8_t device_address;

  public:
    using TransportTag = umi::mmio::I2CTransportTag;

    I2cTransport(I2cBus& bus, std::uint8_t addr) noexcept : bus(bus), device_address(addr) {}

    void raw_write(std::uint8_t reg_addr, const void* data, std::size_t size) const noexcept {
        assert(size <= 8 && "Register size must be <= 64 bits");
        bus.write(device_address, reg_addr, data, size);
    }

    void raw_read(std::uint8_t reg_addr, void* data, std::size_t size) const noexcept {
        assert(size <= 8 && "Register size must be <= 64 bits");
        bus.write_then_read(device_address, &reg_addr, 1, data, size);
    }
};

// Mock SPI bus for testing
class MockSpiBus {
    std::array<std::uint8_t, 256> memory{};

  public:
    void transfer(const std::uint8_t* tx_data, std::uint8_t* rx_data, std::size_t size) noexcept {
        // Simple echo for testing
        if (rx_data != nullptr && tx_data != nullptr) {
            std::memcpy(rx_data, tx_data, size);
        }

        // Simulate register access
        if (size >= 2 && tx_data != nullptr) {
            std::uint8_t addr = tx_data[0] & 0x7F; // Remove R/W bit
            bool is_read = (tx_data[0] & 0x80) != 0;

            if (is_read && rx_data != nullptr) {
                // Read: return memory content
                for (std::size_t i = 1; i < size; ++i) {
                    rx_data[i] = memory[addr + i - 1];
                }
            } else {
                // Write: store to memory
                for (std::size_t i = 1; i < size; ++i) {
                    memory[addr + i - 1] = tx_data[i];
                }
            }
        }
    }

    // Direct memory access for testing
    std::uint8_t& operator[](std::uint8_t addr) { return memory[addr]; }
};

// SPI device with chip select
template <typename SpiBus, typename CsPin>
class SpiDevice {
    SpiBus& bus;
    CsPin& cs;

  public:
    SpiDevice(SpiBus& bus, CsPin& cs) : bus(bus), cs(cs) {}

    void transfer(const std::uint8_t* tx_data, std::uint8_t* rx_data, std::size_t size) noexcept {
        cs.set_low();
        bus.transfer(tx_data, rx_data, size);
        cs.set_high();
    }
};

// Mock CS pin
struct MockCsPin {
  private:
    bool state = true;

  public:
    void set_high() noexcept { state = true; }
    void set_low() noexcept { state = false; }
};

// SPI transport implementation
template <typename SpiDev, typename CheckPolicy = std::true_type>
class SpiTransport : public umi::mmio::ByteAdapter<SpiTransport<SpiDev, CheckPolicy>, CheckPolicy> {
    SpiDev& device;

  public:
    using TransportTag = umi::mmio::SPITransportTag;

    explicit SpiTransport(SpiDev& device) noexcept : device(device) {}

    void raw_write(std::uint8_t reg_addr, const void* data, std::size_t size) const noexcept {
        assert(size <= 8 && "Register size must be <= 64 bits");
        // Write command (MSB clear)
        std::uint8_t cmd = reg_addr & 0x7F;
        std::array<std::uint8_t, 9> tx_buf; // cmd + up to 8 bytes
        tx_buf[0] = cmd;
        std::memcpy(&tx_buf[1], data, size);

        device.transfer(tx_buf.data(), nullptr, size + 1);
    }

    void raw_read(std::uint8_t reg_addr, void* data, std::size_t size) const noexcept {
        assert(size <= 8 && "Register size must be <= 64 bits");
        // Read command (MSB set)
        std::uint8_t cmd = reg_addr | 0x80;
        std::array<std::uint8_t, 9> tx_buf{};
        std::array<std::uint8_t, 9> rx_buf{};
        tx_buf[0] = cmd;

        device.transfer(tx_buf.data(), rx_buf.data(), size + 1);
        std::memcpy(data, &rx_buf[1], size);
    }
};

// Test devices
// NOLINTBEGIN(readability-identifier-naming)
struct TestDevice8 : umi::mmio::Device<umi::mmio::RW, umi::mmio::I2CTransportTag, umi::mmio::SPITransportTag> {
    struct REG0 : umi::mmio::Register<TestDevice8, 0x00, 8> {};
    struct REG1 : umi::mmio::Register<TestDevice8, 0x01, 8> {
        struct BIT0 : umi::mmio::Field<REG1, 0, 1> {};
        struct BIT1 : umi::mmio::Field<REG1, 1, 1> {};
        struct FIELD : umi::mmio::Field<REG1, 4, 4> {
            using Value0 = umi::mmio::Value<FIELD, 0>;
            using Value5 = umi::mmio::Value<FIELD, 5>;
            using ValueF = umi::mmio::Value<FIELD, 15>;
        };
    };
};

struct TestDevice16 : umi::mmio::Device<umi::mmio::RW, umi::mmio::I2CTransportTag, umi::mmio::SPITransportTag> {
    struct REG0 : umi::mmio::Register<TestDevice16, 0x00, 16> {};
    struct REG2 : umi::mmio::Register<TestDevice16, 0x02, 16> {
        struct FIELD_L : umi::mmio::Field<REG2, 0, 8> {};
        struct FIELD_H : umi::mmio::Field<REG2, 8, 8> {};
    };
};

struct TestDevice32 : umi::mmio::Device<umi::mmio::RW, umi::mmio::I2CTransportTag> {
    struct REG0 : umi::mmio::Register<TestDevice32, 0x00, 32> {};
    struct REG4 : umi::mmio::Register<TestDevice32, 0x04, 32> {
        struct BYTE0 : umi::mmio::Field<REG4, 0, 8> {};
        struct BYTE1 : umi::mmio::Field<REG4, 8, 8> {};
        struct BYTE2 : umi::mmio::Field<REG4, 16, 8> {};
        struct BYTE3 : umi::mmio::Field<REG4, 24, 8> {};
    };
};

// Direct-only device
struct DirectOnlyDevice : umi::mmio::Device<> { // Default is DirectTransportTag only
    struct REG0 : umi::mmio::Register<DirectOnlyDevice, 0x1000, 32> {};
};
// NOLINTEND(readability-identifier-naming)

} // namespace test
