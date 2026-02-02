#include <cassert>
#include <cstdint>
#include <type_traits>
#include <mmio/mmio.hh>

// Test device for Value usage
struct TestDevice : mm::Device<mm::RW> {
    // Test register for value constants
    struct CONFIG : mm::Register<TestDevice, 0x00, 8> {
        // Value constants
        using MIN_VALUE = mm::Value<CONFIG, 0>;
        using MAX_VALUE = mm::Value<CONFIG, 255>;
        using DEFAULT_VALUE = mm::Value<CONFIG, 100>;
    };

    // Device-level ID register
    struct ID : mm::Register<TestDevice, 0x10, 8> {
        using DEVICE_ID = mm::Value<ID, 0x42>;
        using REVISION = mm::Value<ID, 3>;
    };
};

int main() {
    // Test Value::get() for register values
    static_assert(TestDevice::CONFIG::MIN_VALUE::get() == 0);
    static_assert(TestDevice::CONFIG::MAX_VALUE::get() == 255);
    static_assert(TestDevice::CONFIG::DEFAULT_VALUE::get() == 100);
    
    // Test Value::get() for register-level values
    static_assert(TestDevice::ID::DEVICE_ID::get() == 0x42);
    static_assert(TestDevice::ID::REVISION::get() == 3);

    // Test ValueType
    static_assert(std::is_same_v<std::remove_const_t<TestDevice::CONFIG::MIN_VALUE::ValueType>, int>);
    static_assert(std::is_same_v<std::remove_const_t<TestDevice::ID::DEVICE_ID::ValueType>, int>);
    
    // Runtime test
    constexpr auto min_val = TestDevice::CONFIG::MIN_VALUE::get();
    constexpr auto max_val = TestDevice::CONFIG::MAX_VALUE::get();
    constexpr auto default_val = TestDevice::CONFIG::DEFAULT_VALUE::get();
    
    assert(min_val == 0);
    assert(max_val == 255);
    assert(default_val == 100);
    
    return 0;
}