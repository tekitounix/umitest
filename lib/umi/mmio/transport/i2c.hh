#pragma once

#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <span>

#include "../register.hh"

namespace umi {
namespace mmio {

// I2C Transport for HAL-compatible I2C drivers
template <typename I2C,
          typename CheckPolicy = std::true_type,
          typename ErrorPolicy = AssertOnError,
          typename AddressType = std::uint8_t,
          Endian AddrEndian = Endian::Big,
          Endian DataEndian = Endian::Little>
class I2cTransport
    : public ByteAdapter<I2cTransport<I2C, CheckPolicy, ErrorPolicy, AddressType, AddrEndian, DataEndian>,
                         CheckPolicy,
                         ErrorPolicy,
                         AddressType,
                         DataEndian> {
    I2C& i2c_driver;
    std::uint8_t device_addr;

  public:
    using TransportTag = I2CTransportTag;

    explicit I2cTransport(I2C& i2c, std::uint8_t addr) : i2c_driver(i2c), device_addr(addr) {}

    void raw_write(AddressType reg_addr, const void* data, std::size_t size) const noexcept {
        constexpr std::size_t addr_size = sizeof(AddressType);
        static_assert(addr_size == 1 || addr_size == 2, "AddressType must be 8 or 16 bit");
        assert(size <= 8 && "Register size must be <= 64 bits");

        std::array<std::uint8_t, 2 + 8> buffer{};
        if constexpr (addr_size == 1) {
            buffer[0] = static_cast<std::uint8_t>(reg_addr);
        } else {
            if constexpr (AddrEndian == Endian::Little) {
                buffer[0] = static_cast<std::uint8_t>(reg_addr & 0xFF);
                buffer[1] = static_cast<std::uint8_t>((reg_addr >> 8) & 0xFF);
            } else {
                buffer[0] = static_cast<std::uint8_t>((reg_addr >> 8) & 0xFF);
                buffer[1] = static_cast<std::uint8_t>(reg_addr & 0xFF);
            }
        }

        std::memcpy(buffer.data() + addr_size, data, size);
        auto payload = std::span<const std::uint8_t>(buffer.data(), addr_size + size);
        [[maybe_unused]] auto result = i2c_driver.write(device_addr >> 1, payload);
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

        std::array<std::uint8_t, 8> buffer{};
        auto tx = std::span<const std::uint8_t>(addr_bytes.data(), addr_size);
        auto rx = std::span<std::uint8_t>(buffer.data(), size);
        auto result = i2c_driver.write_read(device_addr >> 1, tx, rx);
        if (!result) {
            std::memset(data, 0, size);
            return;
        }
        std::memcpy(data, buffer.data(), size);
    }
};

} // namespace mmio
} // namespace umi
