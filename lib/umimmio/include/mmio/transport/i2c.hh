#pragma once

#include "../mmio.hh"
#include <array>
#include <span>
#include <cstdint>

namespace mm {

// I2C Transport for HAL-compatible I2C drivers
template <typename I2C, typename CheckPolicy = std::true_type>
class I2cTransport : private RegOps<I2cTransport<I2C, CheckPolicy>, CheckPolicy> {
    friend class RegOps<I2cTransport<I2C, CheckPolicy>, CheckPolicy>;
    
    I2C& i2c_driver;
    std::uint8_t device_addr;
    
public:
    using RegOps<I2cTransport<I2C, CheckPolicy>, CheckPolicy>::write;
    using RegOps<I2cTransport<I2C, CheckPolicy>, CheckPolicy>::read;
    using RegOps<I2cTransport<I2C, CheckPolicy>, CheckPolicy>::modify;
    using RegOps<I2cTransport<I2C, CheckPolicy>, CheckPolicy>::is;
    using RegOps<I2cTransport<I2C, CheckPolicy>, CheckPolicy>::flip;
    using TransportTag = I2CTransportTag;

    explicit I2cTransport(I2C& i2c, std::uint8_t addr) 
        : i2c_driver(i2c), device_addr(addr) {}

    // RegOps required operations
    template <typename Reg>
    auto reg_read(Reg /*reg*/) const noexcept -> typename Reg::RegValueType {
        std::array<std::uint8_t, 1> reg_addr{static_cast<std::uint8_t>(Reg::address)};
        std::array<std::uint8_t, 1> buffer{};
        
        // device_addr is already in 8-bit format (0x94), convert to 7-bit
        auto result = i2c_driver.write_read(
            device_addr >> 1,  // Convert 8-bit to 7-bit address
            reg_addr,
            buffer
        );
        
        if (!result) {
            // I2C communication failed, return 0
            return 0;
        }
        return static_cast<typename Reg::RegValueType>(buffer[0]);
    }

    template <typename Reg>
    auto reg_write(Reg /*reg*/, typename Reg::RegValueType value) noexcept -> void {
        std::array<std::uint8_t, 2> data{
            static_cast<std::uint8_t>(Reg::address),
            static_cast<std::uint8_t>(value)
        };
        
        // Write register address and value together
        [[maybe_unused]] auto result = i2c_driver.write(device_addr >> 1, data);
    }
    
    template <typename Reg>
    auto reg_modify(Reg reg, typename Reg::RegValueType clear_mask, 
                   typename Reg::RegValueType set_mask) noexcept -> void {
        auto current = reg_read(reg);
        auto new_value = (current & ~clear_mask) | set_mask;
        reg_write(reg, new_value);
    }
};

} // namespace mm