#pragma once

#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>

#include "../register.hh"

namespace umi {
namespace mmio {

// Bit-bang SPI transport (mode 0, MSB first)
// Requirements for Pins:
//   void cs_low() noexcept; void cs_high() noexcept;
//   void sck_low() noexcept; void sck_high() noexcept;
//   void mosi_high() noexcept; void mosi_low() noexcept;
//   bool miso_read() noexcept;
//   void delay() noexcept;

template <typename Pins,
          typename CheckPolicy = std::true_type,
          typename ErrorPolicy = AssertOnError,
          typename AddressType = std::uint8_t,
          Endian AddrEndian = Endian::Big,
          Endian DataEndian = Endian::Little,
          std::uint8_t ReadBit = 0x80,
          std::uint8_t CmdMask = 0x7F,
          std::uint8_t WriteBit = 0x00>
class BitBangSpiTransport : public ByteAdapter<BitBangSpiTransport<Pins,
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
    Pins& pins;

  public:
    using TransportTag = SPITransportTag;

    explicit BitBangSpiTransport(Pins& p) noexcept : pins(p) {}

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

        pins.cs_low();
        for (std::size_t i = 0; i < addr_size + size; ++i) {
            transfer_byte(tx_buf[i], nullptr);
        }
        pins.cs_high();
    }

    void raw_read(AddressType reg_addr, void* data, std::size_t size) const noexcept {
        constexpr std::size_t addr_size = sizeof(AddressType);
        static_assert(addr_size == 1 || addr_size == 2, "AddressType must be 8 or 16 bit");
        assert(size <= 8 && "Register size must be <= 64 bits");

        std::array<std::uint8_t, 2 + 8> tx_buf{};
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

        pins.cs_low();
        for (std::size_t i = 0; i < addr_size; ++i) {
            transfer_byte(tx_buf[i], nullptr);
        }
        auto rx = static_cast<std::uint8_t*>(data);
        for (std::size_t i = 0; i < size; ++i) {
            rx[i] = transfer_byte(0x00, nullptr);
        }
        pins.cs_high();
    }

  private:
    std::uint8_t transfer_byte(std::uint8_t tx, [[maybe_unused]] std::uint8_t* rx) const noexcept {
        std::uint8_t value = 0;
        for (int i = 7; i >= 0; --i) {
            if (tx & (1u << i)) {
                pins.mosi_high();
            } else {
                pins.mosi_low();
            }
            pins.sck_high();
            pins.delay();
            if (pins.miso_read()) {
                value |= static_cast<std::uint8_t>(1u << i);
            }
            pins.sck_low();
            pins.delay();
        }
        return value;
    }
};

} // namespace mmio
} // namespace umi
