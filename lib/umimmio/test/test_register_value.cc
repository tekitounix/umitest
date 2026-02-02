#include <mmio/mmio.hh>
#include <cassert>
#include <cstdint>
#include <iostream>

// Using mm namespace from mmio.hh

// Test device with 8-bit registers
struct TestDevice : mm::Device<mm::RW> {
    // Power control register with predefined values
    struct POWER_CTL : mm::Register<TestDevice, 0x02, 8> {
        // Individual fields
        struct PWR_DOWN : mm::Field<POWER_CTL, 0, 1> {};
        struct ENABLE_A : mm::Field<POWER_CTL, 1, 1> {};
        struct ENABLE_B : mm::Field<POWER_CTL, 2, 1> {};
        
        // Common register values using Value template
        using PowerDown = mm::Value<POWER_CTL, 0x01>;  // All off
        using PowerUp = mm::Value<POWER_CTL, 0x9E>;    // All on
        using PartialPower = mm::Value<POWER_CTL, 0x06>; // Only A and B enabled
    };
    
    // Configuration register
    struct CONFIG : mm::Register<TestDevice, 0x04, 8> {
        using DefaultConfig = mm::Value<CONFIG, 0xA5>;
        using TestConfig = mm::Value<CONFIG, 0x5A>;
    };
};

// Simple test backend that stores register values
class TestBackend {
    static inline std::uint8_t registers[256] = {};
    
public:
    template <typename Reg>
    static auto reg_read(Reg /*reg*/) noexcept -> typename Reg::RegValueType {
        return registers[Reg::address];
    }
    
    template <typename Reg>
    static auto reg_write(Reg /*reg*/, typename Reg::RegValueType value) noexcept -> void {
        registers[Reg::address] = static_cast<std::uint8_t>(value);
    }
};

// Test transport using the test backend
template <typename CheckPolicy = std::true_type>
class TestTransport : private mm::RegOps<TestTransport<CheckPolicy>, CheckPolicy> {
    friend class mm::RegOps<TestTransport<CheckPolicy>, CheckPolicy>;
public:
    using mm::RegOps<TestTransport<CheckPolicy>, CheckPolicy>::write;
    using mm::RegOps<TestTransport<CheckPolicy>, CheckPolicy>::read;
    using mm::RegOps<TestTransport<CheckPolicy>, CheckPolicy>::modify;
    using mm::RegOps<TestTransport<CheckPolicy>, CheckPolicy>::is;
    using TransportTag = mm::DirectTransportTag;
    
    template <typename Reg>
    auto reg_read(Reg reg) const noexcept -> typename Reg::RegValueType {
        return TestBackend::reg_read(reg);
    }
    
    template <typename Reg>
    auto reg_write(Reg reg, typename Reg::RegValueType value) noexcept -> void {
        TestBackend::reg_write(reg, value);
    }
};

void test_register_value_write() {
    TestTransport<> transport;
    
    // Test 1: Write PowerDown value to register
    transport.write(TestDevice::POWER_CTL::PowerDown{});
    auto val1 = transport.read(TestDevice::POWER_CTL{});
    assert(val1 == 0x01);
    std::cout << "Test 1 passed: PowerDown value written correctly (0x" 
              << std::hex << static_cast<int>(val1) << ")\n";
    
    // Test 2: Write PowerUp value to register
    transport.write(TestDevice::POWER_CTL::PowerUp{});
    auto val2 = transport.read(TestDevice::POWER_CTL{});
    assert(val2 == 0x9E);
    std::cout << "Test 2 passed: PowerUp value written correctly (0x" 
              << std::hex << static_cast<int>(val2) << ")\n";
    
    // Test 3: Write PartialPower value to register
    transport.write(TestDevice::POWER_CTL::PartialPower{});
    auto val3 = transport.read(TestDevice::POWER_CTL{});
    assert(val3 == 0x06);
    std::cout << "Test 3 passed: PartialPower value written correctly (0x" 
              << std::hex << static_cast<int>(val3) << ")\n";
    
    // Test 4: Write to CONFIG register
    transport.write(TestDevice::CONFIG::DefaultConfig{});
    auto val4 = transport.read(TestDevice::CONFIG{});
    assert(val4 == 0xA5);
    std::cout << "Test 4 passed: DefaultConfig value written correctly (0x" 
              << std::hex << static_cast<int>(val4) << ")\n";
    
    // Test 5: Write another value to CONFIG register
    transport.write(TestDevice::CONFIG::TestConfig{});
    auto val5 = transport.read(TestDevice::CONFIG{});
    assert(val5 == 0x5A);
    std::cout << "Test 5 passed: TestConfig value written correctly (0x" 
              << std::hex << static_cast<int>(val5) << ")\n";
}

void test_field_value_still_works() {
    TestTransport<> transport;
    
    // Ensure Field Values still work
    transport.write(TestDevice::POWER_CTL::PWR_DOWN::Set{});
    auto val = transport.read(TestDevice::POWER_CTL{});
    assert(val == 0x01);
    std::cout << "Field Value test passed: PWR_DOWN::Set works correctly\n";
}

int main() {
    std::cout << "Testing Register Value support in MMIO library\n";
    std::cout << "==============================================\n";
    
    test_register_value_write();
    test_field_value_still_works();
    
    std::cout << "\nAll tests passed!\n";
    return 0;
}