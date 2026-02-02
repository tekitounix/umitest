#include <mmio/mmio.hh>
#include "test_transport.hh"
#include <cassert>
#include <cstring>
#include <print>

// Memory-mapped device for DirectTransport testing
struct TestDevice : mm::Device<> {
    struct Block0 : mm::Block<TestDevice, 0x1000> {
        struct REG0 : mm::Register<Block0, 0x00, 32> {};
        struct REG1 : mm::Register<Block0, 0x04, 32> {
            struct BIT0 : mm::Field<REG1, 0, 1> {};
            struct BIT1 : mm::Field<REG1, 1, 1> {};
            struct FIELD : mm::Field<REG1, 8, 4> {
                using Value0 = mm::Value<FIELD, 0>;
                using Value5 = mm::Value<FIELD, 5>;
                using ValueF = mm::Value<FIELD, 15>;
            };
        };
        struct REG2 : mm::Register<Block0, 0x08, 32, mm::RW, 0xDEADBEEF> {};
    };
};

namespace {
// Mock memory for DirectTransport
std::array<std::uint32_t, 1024> mock_memory{};

// Mock DirectTransport for testing
template <typename CheckPolicy = std::true_type>
class MockDirectTransport : private mm::RegOps<MockDirectTransport<CheckPolicy>, CheckPolicy> {
    friend class mm::RegOps<MockDirectTransport<CheckPolicy>, CheckPolicy>;
public:
    using mm::RegOps<MockDirectTransport<CheckPolicy>, CheckPolicy>::write;
    using mm::RegOps<MockDirectTransport<CheckPolicy>, CheckPolicy>::read;
    using mm::RegOps<MockDirectTransport<CheckPolicy>, CheckPolicy>::modify;
    using mm::RegOps<MockDirectTransport<CheckPolicy>, CheckPolicy>::is;
    using mm::RegOps<MockDirectTransport<CheckPolicy>, CheckPolicy>::flip;
    using TransportTag = mm::DirectTransportTag;
    
    template <typename Reg>
    [[nodiscard]] auto reg_read(Reg /*reg*/) const noexcept -> typename Reg::RegValueType {
        using T = typename Reg::RegValueType;
        auto idx = (Reg::address - 0x1000) / sizeof(T);
        return static_cast<T>(mock_memory[idx]);
    }
    
    template <typename Reg>
    void reg_write(Reg /*reg*/, typename Reg::RegValueType value) const noexcept {
        using T = typename Reg::RegValueType;
        auto idx = (Reg::address - 0x1000) / sizeof(T);
        mock_memory[idx] = static_cast<std::uint32_t>(value);
    }
};

void test_basic_operations() {
    std::println("Test basic operations...");
    mock_memory.fill(0);
    
    MockDirectTransport<> mcu;
    
    // Test write with value()
    mcu.write(TestDevice::Block0::REG0::value(0x12345678));
    assert(mock_memory[0] == 0x12345678);
    
    // Test read
    auto val = mcu.read(TestDevice::Block0::REG0{});
    assert(val == 0x12345678);
    
    // Test field write with value()
    mcu.write(TestDevice::Block0::REG1::FIELD::value(5));
    assert(mock_memory[1] == 0x00000500);
    
    // Test field read
    auto field_val = mcu.read(TestDevice::Block0::REG1::FIELD{});
    assert(field_val == 5);
    
    std::println("  Basic operations: PASS");
}

void test_enumerated_values() {
    std::println("Test enumerated values...");
    mock_memory.fill(0);
    
    MockDirectTransport<> mcu;
    
    // Write enumerated value
    mcu.write(TestDevice::Block0::REG1::FIELD::Value5{});
    assert(mock_memory[1] == 0x00000500);
    
    // Check with is()
    assert(mcu.is(TestDevice::Block0::REG1::FIELD::Value5{}));
    assert(!mcu.is(TestDevice::Block0::REG1::FIELD::Value0{}));
    
    // Write different enumerated value
    mcu.write(TestDevice::Block0::REG1::FIELD::ValueF{});
    assert(mock_memory[1] == 0x00000F00);
    assert(mcu.is(TestDevice::Block0::REG1::FIELD::ValueF{}));
    
    std::println("  Enumerated values: PASS");
}

void test_modify() {
    std::println("Test modify operations...");
    mock_memory.fill(0);
    
    MockDirectTransport<> mcu;
    
    // Set initial value
    mcu.write(TestDevice::Block0::REG1::value(0xFFFFFFFF));
    
    // Modify single field with value()
    mcu.modify(TestDevice::Block0::REG1::FIELD::value(0x5));
    assert(mock_memory[1] == 0xFFFFF5FF);
    
    // Modify multiple fields
    mcu.modify(
        TestDevice::Block0::REG1::BIT0::Reset{},
        TestDevice::Block0::REG1::BIT1::Set{},
        TestDevice::Block0::REG1::FIELD::value(0xA)
    );
    assert(mock_memory[1] == 0xFFFFFAFE);
    
    std::println("  Modify operations: PASS");
}

void test_bit_operations() {
    std::println("Test bit operations...");
    mock_memory.fill(0);
    
    MockDirectTransport<> mcu;
    
    // Test 1-bit field Set/Reset
    mcu.write(TestDevice::Block0::REG1::BIT0::Set{});
    assert(mock_memory[1] == 0x00000001);
    assert(mcu.is(TestDevice::Block0::REG1::BIT0::Set{}));
    
    mcu.write(TestDevice::Block0::REG1::BIT0::Reset{});
    assert(mock_memory[1] == 0x00000000);
    assert(mcu.is(TestDevice::Block0::REG1::BIT0::Reset{}));
    
    // Test flip
    mcu.flip(TestDevice::Block0::REG1::BIT0{});
    assert(mock_memory[1] == 0x00000001);
    
    mcu.flip(TestDevice::Block0::REG1::BIT0{});
    assert(mock_memory[1] == 0x00000000);
    
    std::println("  Bit operations: PASS");
}

void test_reset_values() {
    std::println("Test reset values...");
    mock_memory.fill(0);
    
    MockDirectTransport<> mcu;
    
    // Write to register with reset value
    mcu.write(TestDevice::Block0::REG2::value(0));
    assert(mock_memory[2] == 0);
    
    // Writing a field should use register's reset value
    mcu.write(TestDevice::Block0::REG1::BIT0::Set{});
    assert(mock_memory[1] == 0x00000001);  // Other bits are 0 (reset value)
    
    std::println("  Reset values: PASS");
}

void test_value_interface() {
    std::println("Test value() interface...");
    mock_memory.fill(0);
    
    MockDirectTransport<> mcu;
    
    // Test register value()
    mcu.write(TestDevice::Block0::REG0::value(0xABCDEF00));
    assert(mock_memory[0] == 0xABCDEF00);
    
    // Test field value()
    mcu.write(TestDevice::Block0::REG1::FIELD::value(0x7));
    assert(mock_memory[1] == 0x00000700);
    
    // Test is() with value()
    assert(mcu.is(TestDevice::Block0::REG1::FIELD::value(0x7)));
    assert(!mcu.is(TestDevice::Block0::REG1::FIELD::value(0x8)));
    
    // Test modify with value()
    mcu.write(TestDevice::Block0::REG1::value(0xFFFFFFFF));
    mcu.modify(TestDevice::Block0::REG1::FIELD::value(0x3));
    assert(mock_memory[1] == 0xFFFFF3FF);
    
    std::println("  value() interface: PASS");
}

void test_i2c_transport() {
    std::println("Test I2C transport...");
    
    test::MockI2cBus i2c_bus;
    test::I2cTransport i2c_dev(i2c_bus, 0x50);
    
    // Test 8-bit register
    i2c_dev.write(test::TestDevice8::REG0::value(0xAB));
    assert(i2c_bus(0x50, 0x00) == 0xAB);
    
    auto val8 = i2c_dev.read(test::TestDevice8::REG0{});
    assert(val8 == 0xAB);
    
    // Test 16-bit register (little-endian)
    i2c_dev.write(test::TestDevice16::REG0::value(0x1234));
    assert(i2c_bus(0x50, 0x00) == 0x34);  // LSB
    assert(i2c_bus(0x50, 0x01) == 0x12);  // MSB
    
    auto val16 = i2c_dev.read(test::TestDevice16::REG0{});
    assert(val16 == 0x1234);
    
    // Test 32-bit register
    test::I2cTransport i2c_dev32(i2c_bus, 0x60);
    i2c_dev32.write(test::TestDevice32::REG0::value(0xDEADBEEF));
    assert(i2c_bus(0x60, 0x00) == 0xEF);
    assert(i2c_bus(0x60, 0x01) == 0xBE);
    assert(i2c_bus(0x60, 0x02) == 0xAD);
    assert(i2c_bus(0x60, 0x03) == 0xDE);
    
    std::println("  I2C transport: PASS");
}

void test_spi_transport() {
    std::println("Test SPI transport...");
    
    test::MockSpiBus spi_bus;
    test::MockCsPin cs;
    test::SpiDevice spi_device(spi_bus, cs);
    test::SpiTransport spi_transport(spi_device);
    
    // Test 8-bit register write
    spi_transport.write(test::TestDevice8::REG1::value(0xCD));
    assert(spi_bus[0x01] == 0xCD);
    
    // Test 8-bit register read
    auto val = spi_transport.read(test::TestDevice8::REG1{});
    assert(val == 0xCD);
    
    // Test 16-bit register (little-endian)
    spi_transport.write(test::TestDevice16::REG2::value(0x5678));
    assert(spi_bus[0x02] == 0x78);  // LSB
    assert(spi_bus[0x03] == 0x56);  // MSB
    
    auto val16 = spi_transport.read(test::TestDevice16::REG2{});
    assert(val16 == 0x5678);
    
    std::println("  SPI transport: PASS");
}

void test_transport_constraints() {
    std::println("Test transport constraints...");
    
    // DirectOnlyDevice should only work with DirectTransport
    MockDirectTransport<> mcu;
    mcu.write(test::DirectOnlyDevice::REG0::value(0x12345678));
    
    // These would cause compile errors:
    // test::MockI2cBus i2c_bus;
    // test::I2cTransport i2c(i2c_bus, 0x50);
    // i2c.write(test::DirectOnlyDevice::REG0::value(0));  // Error!
    
    std::println("  Transport constraints: PASS");
}

// Test compile-time range checking
void test_compile_time_checks() {
    std::println("Test compile-time checks...");
    
    // These should compile
    [[maybe_unused]] auto v1 = TestDevice::Block0::REG1::FIELD::value(0);
    [[maybe_unused]] auto v2 = TestDevice::Block0::REG1::FIELD::value(15);
    
    // This would cause compile error if uncommented:
    // [[maybe_unused]] auto v3 = TestDevice::Block0::REG1::FIELD::value(16);
    
    std::println("  Compile-time checks: PASS");
}

} // anonymous namespace

int main() {
    std::println("Running mmio tests...\n");
    
    test_basic_operations();
    test_enumerated_values();
    test_modify();
    test_bit_operations();
    test_reset_values();
    test_value_interface();
    test_i2c_transport();
    test_spi_transport();
    test_transport_constraints();
    test_compile_time_checks();
    
    std::println("\nAll tests passed!");
    return 0;
}