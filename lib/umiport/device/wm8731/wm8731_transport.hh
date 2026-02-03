#pragma once

#include <umimmio.hh>
#include <array>
#include <cstdint>
#include <span>

namespace mm {

// WM8731 special transport (7-bit register address + 9-bit data)
// Requirements for I2C driver:
//   bool write(std::uint8_t addr7, std::span<const std::uint8_t> data) noexcept;

template <typename I2C,
          typename CheckPolicy = std::true_type,
          typename ErrorPolicy = AssertOnError>
class Wm8731Transport : private RegOps<Wm8731Transport<I2C, CheckPolicy, ErrorPolicy>, CheckPolicy, ErrorPolicy> {
    friend class RegOps<Wm8731Transport<I2C, CheckPolicy, ErrorPolicy>, CheckPolicy, ErrorPolicy>;

    I2C& i2c_driver;
    std::uint8_t device_addr;

public:
    using TransportTag = I2CTransportTag;
    using RegOps<Wm8731Transport<I2C, CheckPolicy, ErrorPolicy>, CheckPolicy, ErrorPolicy>::write;

    explicit Wm8731Transport(I2C& i2c, std::uint8_t addr) noexcept
        : i2c_driver(i2c), device_addr(addr) {}

    template <typename Reg>
    [[nodiscard]] auto reg_read(Reg /*reg*/) const noexcept -> typename Reg::RegValueType {
        static_assert(sizeof(Reg) == 0, "WM8731 registers are write-only");
        return 0;
    }

    template <typename Reg>
    void reg_write(Reg /*reg*/, typename Reg::RegValueType value) const noexcept {
        std::uint16_t word = (static_cast<std::uint16_t>(Reg::address) << 9) |
                             (static_cast<std::uint16_t>(value) & 0x01FFu);
        std::array<std::uint8_t, 2> buf{
            static_cast<std::uint8_t>(word >> 8),
            static_cast<std::uint8_t>(word & 0xFF)
        };
        [[maybe_unused]] auto result = i2c_driver.write(device_addr >> 1, std::span<const std::uint8_t>(buf.data(), buf.size()));
    }
};

} // namespace mm
