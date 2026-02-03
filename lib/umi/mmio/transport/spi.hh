#pragma once

#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>

#include "../register.hh"

namespace umi {
namespace mmio {

// SPI Transport for byte-addressed devices
// Requirements for SpiDevice:
//   void transfer(const std::uint8_t* tx, std::uint8_t* rx, std::size_t size) noexcept;

template <typename SpiDevice,
          typename CheckPolicy = std::true_type,
          typename ErrorPolicy = AssertOnError,
          typename AddressType = std::uint8_t,
          Endian AddrEndian = Endian::Big,
          Endian DataEndian = Endian::Little,
          std::uint8_t ReadBit = 0x80,
          std::uint8_t CmdMask = 0x7F,
          std::uint8_t WriteBit = 0x00>
class SpiTransport : public ByteAdapter<SpiTransport<SpiDevice,
                                                     CheckPolicy,
                                                     ErrorPolicy,
                                                     AddressType,
                                                     AddrEndian,
                                                     DataEndian,
                                                     ReadBit,
                                                     CmdMask,
                                                     WriteBit>,
                                        CheckPolicy,
                                        ErrorPolicy,
                                        AddressType,
                                        DataEndian> {
    SpiDevice& device;

  public:
    using TransportTag = SPITransportTag;

    explicit SpiTransport(SpiDevice& dev) noexcept : device(dev) {}

    void raw_write(AddressType reg_addr, const void* data, std::size_t size) const noexcept {
        constexpr std::size_t addr_size = sizeof(AddressType);
        static_assert(addr_size == 1 || addr_size == 2, "AddressType must be 8 or 16 bit");
        assert(size <= 8 && "Register size must be <= 64 bits");

        std::array<std::uint8_t, 2 + 8> tx_buf{};
        if constexpr (addr_size == 1) {
            tx_buf[0] = (static_cast<std::uint8_t>(reg_addr) & CmdMask) | WriteBit;
        } else {
            if constexpr (AddrEndian == Endian::Little) {
                tx_buf[0] = static_cast<std::uint8_t>(reg_addr & 0xFF);
                tx_buf[1] = static_cast<std::uint8_t>((reg_addr >> 8) & 0xFF);
            } else {
                tx_buf[0] = static_cast<std::uint8_t>((reg_addr >> 8) & 0xFF);
                tx_buf[1] = static_cast<std::uint8_t>(reg_addr & 0xFF);
            }
            tx_buf[0] = (tx_buf[0] & CmdMask) | WriteBit;
        }

        std::memcpy(tx_buf.data() + addr_size, data, size);
        device.transfer(tx_buf.data(), nullptr, addr_size + size);
    }

    void raw_read(AddressType reg_addr, void* data, std::size_t size) const noexcept {
        constexpr std::size_t addr_size = sizeof(AddressType);
        static_assert(addr_size == 1 || addr_size == 2, "AddressType must be 8 or 16 bit");
        assert(size <= 8 && "Register size must be <= 64 bits");

        std::array<std::uint8_t, 2 + 8> tx_buf{};
        std::array<std::uint8_t, 2 + 8> rx_buf{};
        if constexpr (addr_size == 1) {
            tx_buf[0] = (static_cast<std::uint8_t>(reg_addr) & CmdMask) | ReadBit;
        } else {
            if constexpr (AddrEndian == Endian::Little) {
                tx_buf[0] = static_cast<std::uint8_t>(reg_addr & 0xFF);
                tx_buf[1] = static_cast<std::uint8_t>((reg_addr >> 8) & 0xFF);
            } else {
                tx_buf[0] = static_cast<std::uint8_t>((reg_addr >> 8) & 0xFF);
                tx_buf[1] = static_cast<std::uint8_t>(reg_addr & 0xFF);
            }
            tx_buf[0] = (tx_buf[0] & CmdMask) | ReadBit;
        }

        device.transfer(tx_buf.data(), rx_buf.data(), addr_size + size);
        std::memcpy(data, rx_buf.data() + addr_size, size);
    }
};

} // namespace mmio
} // namespace umi
