#pragma once

#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>

#include "../register.hh"

namespace umi {
namespace mmio {

// Bit-bang I2C transport
// Requirements for Gpio:
//   void scl_high() noexcept; void scl_low() noexcept;
//   void sda_high() noexcept; void sda_low() noexcept;
//   bool sda_read() noexcept; // read SDA line
//   void delay() noexcept;    // short delay for timing

template <typename Gpio,
          typename CheckPolicy = std::true_type,
          typename ErrorPolicy = AssertOnError,
          typename AddressType = std::uint8_t,
          Endian AddrEndian = Endian::Big,
          Endian DataEndian = Endian::Little>
class BitBangI2cTransport
    : public ByteAdapter<BitBangI2cTransport<Gpio, CheckPolicy, ErrorPolicy, AddressType, AddrEndian, DataEndian>,
                         CheckPolicy,
                         ErrorPolicy,
                         AddressType,
                         DataEndian> {
    Gpio& gpio;
    std::uint8_t device_addr;

  public:
    using TransportTag = I2CTransportTag;

    BitBangI2cTransport(Gpio& g, std::uint8_t addr) noexcept : gpio(g), device_addr(addr) {}

    void raw_write(AddressType reg_addr, const void* data, std::size_t size) const noexcept {
        constexpr std::size_t addr_size = sizeof(AddressType);
        static_assert(addr_size == 1 || addr_size == 2, "AddressType must be 8 or 16 bit");
        assert(size <= 8 && "Register size must be <= 64 bits");

        std::array<std::uint8_t, 2> addr_bytes{};
        if constexpr (addr_size == 1) {
            addr_bytes[0] = static_cast<std::uint8_t>(reg_addr);
        } else {
            if constexpr (AddrEndian == Endian::Little) {
                addr_bytes[0] = static_cast<std::uint8_t>(reg_addr & 0xFF);
                addr_bytes[1] = static_cast<std::uint8_t>((reg_addr >> 8) & 0xFF);
            } else {
                addr_bytes[0] = static_cast<std::uint8_t>((reg_addr >> 8) & 0xFF);
                addr_bytes[1] = static_cast<std::uint8_t>(reg_addr & 0xFF);
            }
        }

        start();
        if (!write_byte((device_addr >> 1) << 1)) {
            stop();
            ErrorPolicy::on_range_error("I2C NACK on address");
            return;
        }
        for (std::size_t i = 0; i < addr_size; ++i) {
            if (!write_byte(addr_bytes[i])) {
                stop();
                ErrorPolicy::on_range_error("I2C NACK on register address");
                return;
            }
        }
        auto bytes = static_cast<const std::uint8_t*>(data);
        for (std::size_t i = 0; i < size; ++i) {
            if (!write_byte(bytes[i])) {
                stop();
                ErrorPolicy::on_range_error("I2C NACK on data");
                return;
            }
        }
        stop();
    }

    void raw_read(AddressType reg_addr, void* data, std::size_t size) const noexcept {
        constexpr std::size_t addr_size = sizeof(AddressType);
        static_assert(addr_size == 1 || addr_size == 2, "AddressType must be 8 or 16 bit");
        assert(size <= 8 && "Register size must be <= 64 bits");

        std::array<std::uint8_t, 2> addr_bytes{};
        if constexpr (addr_size == 1) {
            addr_bytes[0] = static_cast<std::uint8_t>(reg_addr);
        } else {
            if constexpr (AddrEndian == Endian::Little) {
                addr_bytes[0] = static_cast<std::uint8_t>(reg_addr & 0xFF);
                addr_bytes[1] = static_cast<std::uint8_t>((reg_addr >> 8) & 0xFF);
            } else {
                addr_bytes[0] = static_cast<std::uint8_t>((reg_addr >> 8) & 0xFF);
                addr_bytes[1] = static_cast<std::uint8_t>(reg_addr & 0xFF);
            }
        }

        start();
        if (!write_byte((device_addr >> 1) << 1)) {
            stop();
            ErrorPolicy::on_range_error("I2C NACK on address");
            return;
        }
        for (std::size_t i = 0; i < addr_size; ++i) {
            if (!write_byte(addr_bytes[i])) {
                stop();
                ErrorPolicy::on_range_error("I2C NACK on register address");
                return;
            }
        }
        start();
        if (!write_byte(((device_addr >> 1) << 1) | 0x01)) {
            stop();
            ErrorPolicy::on_range_error("I2C NACK on read address");
            return;
        }

        auto bytes = static_cast<std::uint8_t*>(data);
        for (std::size_t i = 0; i < size; ++i) {
            bytes[i] = read_byte(i + 1 < size);
        }
        stop();
    }

  private:
    void start() const noexcept {
        gpio.sda_high();
        gpio.scl_high();
        gpio.delay();
        gpio.sda_low();
        gpio.delay();
        gpio.scl_low();
    }

    void stop() const noexcept {
        gpio.sda_low();
        gpio.delay();
        gpio.scl_high();
        gpio.delay();
        gpio.sda_high();
        gpio.delay();
    }

    bool write_byte(std::uint8_t byte) const noexcept {
        for (int i = 7; i >= 0; --i) {
            if (byte & (1u << i)) {
                gpio.sda_high();
            } else {
                gpio.sda_low();
            }
            gpio.delay();
            gpio.scl_high();
            gpio.delay();
            gpio.scl_low();
        }
        gpio.sda_high(); // release
        gpio.delay();
        gpio.scl_high();
        gpio.delay();
        bool ack = !gpio.sda_read();
        gpio.scl_low();
        return ack;
    }

    std::uint8_t read_byte(bool ack) const noexcept {
        std::uint8_t byte = 0;
        gpio.sda_high(); // release
        for (int i = 7; i >= 0; --i) {
            gpio.scl_high();
            gpio.delay();
            if (gpio.sda_read()) {
                byte |= static_cast<std::uint8_t>(1u << i);
            }
            gpio.scl_low();
            gpio.delay();
        }
        if (ack) {
            gpio.sda_low();
        } else {
            gpio.sda_high();
        }
        gpio.delay();
        gpio.scl_high();
        gpio.delay();
        gpio.scl_low();
        gpio.sda_high();
        return byte;
    }
};

} // namespace mmio
} // namespace umi
