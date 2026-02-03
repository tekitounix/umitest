#include <span>
#include <umi/test/test.hh>

#include "../mmio.hh"
#include "test_transport.hh"

// Memory-mapped device for DirectTransport testing
struct TestDevice : umi::mmio::Device<> {
    struct Block0 : umi::mmio::Block<TestDevice, 0x1000> {
        struct REG0 : umi::mmio::Register<Block0, 0x00, 32> {};
        struct REG1 : umi::mmio::Register<Block0, 0x04, 32> {
            struct BIT0 : umi::mmio::Field<REG1, 0, 1> {};
            struct BIT1 : umi::mmio::Field<REG1, 1, 1> {};
            struct FIELD : umi::mmio::Field<REG1, 8, 4> {
                using Value0 = umi::mmio::Value<FIELD, 0>;
                using Value5 = umi::mmio::Value<FIELD, 5>;
                using ValueF = umi::mmio::Value<FIELD, 15>;
            };
        };
        struct REG2 : umi::mmio::Register<Block0, 0x08, 32, umi::mmio::RW, 0xDEADBEEF> {};
    };
};

struct TestDevice64 : umi::mmio::Device<> {
    struct REG64 : umi::mmio::Register<TestDevice64, 0x2000, 64> {
        struct LOW32 : umi::mmio::Field<REG64, 0, 32> {};
        struct HIGH16 : umi::mmio::Field<REG64, 32, 16> {};
    };
};

namespace {
// Mock memory for DirectTransport
std::array<std::uint32_t, 1024> mock_memory{};

// Mock I2C driver for umi::mmio::I2cTransport (span-based API)
class MockI2cDriver {
    struct DeviceMemory {
        std::array<std::uint8_t, 512> data{};
    };
    std::array<DeviceMemory, 128> devices{};

  public:
    bool write(std::uint8_t addr7, std::span<const std::uint8_t> data) noexcept {
        if (data.size() < 1) {
            return false;
        }
        auto& dev = devices[addr7 & 0x7F];
        std::uint16_t reg = data[0];
        if (data.size() >= 2) {
            for (std::size_t i = 1; i < data.size(); ++i) {
                dev.data[reg++] = data[i];
            }
        }
        return true;
    }

    bool write_read(std::uint8_t addr7, std::span<const std::uint8_t> tx, std::span<std::uint8_t> rx) noexcept {
        if (tx.size() < 1) {
            return false;
        }
        auto& dev = devices[addr7 & 0x7F];
        std::uint16_t reg = tx[0];
        for (std::size_t i = 0; i < rx.size(); ++i) {
            rx[i] = dev.data[reg + i];
        }
        return true;
    }

    std::uint8_t& operator()(std::uint8_t addr7, std::uint8_t reg) { return devices[addr7 & 0x7F].data[reg]; }
};

// Mock DirectTransport for testing
template <typename CheckPolicy = std::true_type>
class MockDirectTransport : private umi::mmio::RegOps<MockDirectTransport<CheckPolicy>, CheckPolicy> {
    friend class umi::mmio::RegOps<MockDirectTransport<CheckPolicy>, CheckPolicy>;

  public:
    using umi::mmio::RegOps<MockDirectTransport<CheckPolicy>, CheckPolicy>::write;
    using umi::mmio::RegOps<MockDirectTransport<CheckPolicy>, CheckPolicy>::read;
    using umi::mmio::RegOps<MockDirectTransport<CheckPolicy>, CheckPolicy>::modify;
    using umi::mmio::RegOps<MockDirectTransport<CheckPolicy>, CheckPolicy>::is;
    using umi::mmio::RegOps<MockDirectTransport<CheckPolicy>, CheckPolicy>::flip;
    using TransportTag = umi::mmio::DirectTransportTag;

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

// Mock DirectTransport for 64-bit registers
template <typename CheckPolicy = std::true_type>
class MockDirectTransport64 : private umi::mmio::RegOps<MockDirectTransport64<CheckPolicy>, CheckPolicy> {
    friend class umi::mmio::RegOps<MockDirectTransport64<CheckPolicy>, CheckPolicy>;

  public:
    using umi::mmio::RegOps<MockDirectTransport64<CheckPolicy>, CheckPolicy>::write;
    using umi::mmio::RegOps<MockDirectTransport64<CheckPolicy>, CheckPolicy>::read;
    using umi::mmio::RegOps<MockDirectTransport64<CheckPolicy>, CheckPolicy>::modify;
    using umi::mmio::RegOps<MockDirectTransport64<CheckPolicy>, CheckPolicy>::is;
    using umi::mmio::RegOps<MockDirectTransport64<CheckPolicy>, CheckPolicy>::flip;
    using TransportTag = umi::mmio::DirectTransportTag;

    mutable std::array<std::uint64_t, 16> mem{};

    template <typename Reg>
    [[nodiscard]] auto reg_read(Reg /*reg*/) const noexcept -> typename Reg::RegValueType {
        using T = typename Reg::RegValueType;
        auto idx = (Reg::address - 0x2000) / sizeof(T);
        return static_cast<T>(mem[idx]);
    }

    template <typename Reg>
    void reg_write(Reg /*reg*/, typename Reg::RegValueType value) const noexcept {
        using T = typename Reg::RegValueType;
        auto idx = (Reg::address - 0x2000) / sizeof(T);
        mem[idx] = static_cast<std::uint64_t>(value);
    }
};

} // anonymous namespace

int main() {
    umi::test::Suite s("mmio");

    s.section("basic operations");
    s.run("write with value()", [](umi::test::TestContext& t) {
        mock_memory.fill(0);
        MockDirectTransport<> mcu;
        mcu.write(TestDevice::Block0::REG0::value(0x12345678));
        t.assert_eq(mock_memory[0], 0x12345678u);
        auto val = mcu.read(TestDevice::Block0::REG0{});
        t.assert_eq(val, 0x12345678u);
        return !t.failed;
    });

    s.section("field write with value()");
    s.run("field write with value()", [](umi::test::TestContext& t) {
        mock_memory.fill(0);
        MockDirectTransport<> mcu;
        mcu.write(TestDevice::Block0::REG1::FIELD::value(5));
        t.assert_eq(mock_memory[1], 0x00000500u);
        auto field_val = mcu.read(TestDevice::Block0::REG1::FIELD{});
        t.assert_eq(field_val, 5u);
        return !t.failed;
    });

    s.section("enumerated values");
    s.run("enumerated value write and is()", [](umi::test::TestContext& t) {
        mock_memory.fill(0);
        MockDirectTransport<> mcu;
        mcu.write(TestDevice::Block0::REG1::FIELD::Value5{});
        t.assert_eq(mock_memory[1], 0x00000500u);
        t.assert_true(mcu.is(TestDevice::Block0::REG1::FIELD::Value5{}));
        t.assert_true(!mcu.is(TestDevice::Block0::REG1::FIELD::Value0{}));
        mcu.write(TestDevice::Block0::REG1::FIELD::ValueF{});
        t.assert_eq(mock_memory[1], 0x00000F00u);
        t.assert_true(mcu.is(TestDevice::Block0::REG1::FIELD::ValueF{}));
        return !t.failed;
    });

    s.section("modify operations");
    s.run("modify single field", [](umi::test::TestContext& t) {
        mock_memory.fill(0);
        MockDirectTransport<> mcu;
        mcu.write(TestDevice::Block0::REG1::value(0xFFFFFFFF));
        mcu.modify(TestDevice::Block0::REG1::FIELD::value(0x5));
        t.assert_eq(mock_memory[1], 0xFFFFF5FFu);
        return !t.failed;
    });

    s.run("modify multiple fields", [](umi::test::TestContext& t) {
        mock_memory.fill(0);
        MockDirectTransport<> mcu;
        mcu.write(TestDevice::Block0::REG1::value(0xFFFFFFFF));
        mcu.modify(TestDevice::Block0::REG1::BIT0::Reset{},
                   TestDevice::Block0::REG1::BIT1::Set{},
                   TestDevice::Block0::REG1::FIELD::value(0xA));
        t.assert_eq(mock_memory[1], 0xFFFFFAFEu);
        return !t.failed;
    });

    s.section("bit operations");
    s.run("Set/Reset/flip", [](umi::test::TestContext& t) {
        mock_memory.fill(0);
        MockDirectTransport<> mcu;
        mcu.write(TestDevice::Block0::REG1::BIT0::Set{});
        t.assert_eq(mock_memory[1], 0x00000001u);
        t.assert_true(mcu.is(TestDevice::Block0::REG1::BIT0::Set{}));
        mcu.write(TestDevice::Block0::REG1::BIT0::Reset{});
        t.assert_eq(mock_memory[1], 0x00000000u);
        t.assert_true(mcu.is(TestDevice::Block0::REG1::BIT0::Reset{}));
        mcu.flip(TestDevice::Block0::REG1::BIT0{});
        t.assert_eq(mock_memory[1], 0x00000001u);
        mcu.flip(TestDevice::Block0::REG1::BIT0{});
        t.assert_eq(mock_memory[1], 0x00000000u);
        return !t.failed;
    });

    s.section("reset values");
    s.run("register reset value", [](umi::test::TestContext& t) {
        mock_memory.fill(0);
        MockDirectTransport<> mcu;
        mcu.write(TestDevice::Block0::REG2::value(0));
        t.assert_eq(mock_memory[2], 0u);
        mcu.write(TestDevice::Block0::REG1::BIT0::Set{});
        t.assert_eq(mock_memory[1], 0x00000001u);
        return !t.failed;
    });

    s.section("value() interface");
    s.run("register and field value()", [](umi::test::TestContext& t) {
        mock_memory.fill(0);
        MockDirectTransport<> mcu;
        mcu.write(TestDevice::Block0::REG0::value(0xABCDEF00));
        t.assert_eq(mock_memory[0], 0xABCDEF00u);
        mcu.write(TestDevice::Block0::REG1::FIELD::value(0x7));
        t.assert_eq(mock_memory[1], 0x00000700u);
        t.assert_true(mcu.is(TestDevice::Block0::REG1::FIELD::value(0x7)));
        t.assert_true(!mcu.is(TestDevice::Block0::REG1::FIELD::value(0x8)));
        mcu.write(TestDevice::Block0::REG1::value(0xFFFFFFFF));
        mcu.modify(TestDevice::Block0::REG1::FIELD::value(0x3));
        t.assert_eq(mock_memory[1], 0xFFFFF3FFu);
        return !t.failed;
    });

    s.section("I2C transport");
    s.run("8-bit register read/write", [](umi::test::TestContext& t) {
        test::MockI2cBus i2c_bus;
        test::I2cTransport i2c_dev(i2c_bus, 0x50);
        i2c_dev.write(test::TestDevice8::REG0::value(0xAB));
        t.assert_eq(i2c_bus(0x50, 0x00), static_cast<uint8_t>(0xAB));
        auto val8 = i2c_dev.read(test::TestDevice8::REG0{});
        t.assert_eq(val8, static_cast<uint8_t>(0xAB));
        return !t.failed;
    });

    s.run("16-bit register (little-endian)", [](umi::test::TestContext& t) {
        test::MockI2cBus i2c_bus;
        test::I2cTransport i2c_dev(i2c_bus, 0x50);
        i2c_dev.write(test::TestDevice16::REG0::value(0x1234));
        t.assert_eq(i2c_bus(0x50, 0x00), static_cast<uint8_t>(0x34));
        t.assert_eq(i2c_bus(0x50, 0x01), static_cast<uint8_t>(0x12));
        auto val16 = i2c_dev.read(test::TestDevice16::REG0{});
        t.assert_eq(val16, static_cast<uint16_t>(0x1234));
        return !t.failed;
    });

    s.run("32-bit register", [](umi::test::TestContext& t) {
        test::MockI2cBus i2c_bus;
        test::I2cTransport i2c_dev32(i2c_bus, 0x60);
        i2c_dev32.write(test::TestDevice32::REG0::value(0xDEADBEEF));
        t.assert_eq(i2c_bus(0x60, 0x00), static_cast<uint8_t>(0xEF));
        t.assert_eq(i2c_bus(0x60, 0x01), static_cast<uint8_t>(0xBE));
        t.assert_eq(i2c_bus(0x60, 0x02), static_cast<uint8_t>(0xAD));
        t.assert_eq(i2c_bus(0x60, 0x03), static_cast<uint8_t>(0xDE));
        return !t.failed;
    });

    s.section("SPI transport");
    s.run("8-bit register read/write", [](umi::test::TestContext& t) {
        test::MockSpiBus spi_bus;
        test::MockCsPin cs;
        test::SpiDevice spi_device(spi_bus, cs);
        test::SpiTransport spi_transport(spi_device);
        spi_transport.write(test::TestDevice8::REG1::value(0xCD));
        t.assert_eq(spi_bus[0x01], static_cast<uint8_t>(0xCD));
        auto val = spi_transport.read(test::TestDevice8::REG1{});
        t.assert_eq(val, static_cast<uint8_t>(0xCD));
        return !t.failed;
    });

    s.run("16-bit register (little-endian)", [](umi::test::TestContext& t) {
        test::MockSpiBus spi_bus;
        test::MockCsPin cs;
        test::SpiDevice spi_device(spi_bus, cs);
        test::SpiTransport spi_transport(spi_device);
        spi_transport.write(test::TestDevice16::REG2::value(0x5678));
        t.assert_eq(spi_bus[0x02], static_cast<uint8_t>(0x78));
        t.assert_eq(spi_bus[0x03], static_cast<uint8_t>(0x56));
        auto val16 = spi_transport.read(test::TestDevice16::REG2{});
        t.assert_eq(val16, static_cast<uint16_t>(0x5678));
        return !t.failed;
    });

    s.section("umi::mmio::I2cTransport");
    s.run("8-bit register read/write (library transport)", [](umi::test::TestContext& t) {
        MockI2cDriver i2c_driver;
        umi::mmio::I2cTransport<MockI2cDriver> i2c(i2c_driver, 0x50 << 1);
        i2c.write(test::TestDevice8::REG0::value(0xAA));
        t.assert_eq(i2c_driver(0x50, 0x00), static_cast<uint8_t>(0xAA));
        auto val = i2c.read(test::TestDevice8::REG0{});
        t.assert_eq(val, static_cast<uint8_t>(0xAA));
        return !t.failed;
    });

    s.section("umi::mmio::SpiTransport");
    s.run("8-bit register read/write (library transport)", [](umi::test::TestContext& t) {
        test::MockSpiBus spi_bus;
        test::MockCsPin cs;
        test::SpiDevice spi_device(spi_bus, cs);
        umi::mmio::SpiTransport<decltype(spi_device)> spi_transport(spi_device);
        spi_transport.write(test::TestDevice8::REG1::value(0xCD));
        t.assert_eq(spi_bus[0x01], static_cast<uint8_t>(0xCD));
        auto val = spi_transport.read(test::TestDevice8::REG1{});
        t.assert_eq(val, static_cast<uint8_t>(0xCD));
        return !t.failed;
    });

    s.section("transport constraints");
    s.run("DirectOnlyDevice works with DirectTransport", [](umi::test::TestContext& t) {
        mock_memory.fill(0);
        MockDirectTransport<> mcu;
        mcu.write(test::DirectOnlyDevice::REG0::value(0x12345678));
        t.assert_true(true, "DirectOnlyDevice compiled with DirectTransport");
        return !t.failed;
    });

    s.section("compile-time checks");
    s.run("value() range compiles", [](umi::test::TestContext& t) {
        [[maybe_unused]] auto v1 = TestDevice::Block0::REG1::FIELD::value(0);
        [[maybe_unused]] auto v2 = TestDevice::Block0::REG1::FIELD::value(15);
        t.assert_true(true, "compile-time value range accepted");
        return !t.failed;
    });

    s.section("64-bit registers");
    s.run("64-bit register read/write", [](umi::test::TestContext& t) {
        MockDirectTransport64<> mcu64;
        mcu64.write(TestDevice64::REG64::value(0x1122334455667788ULL));
        auto val = mcu64.read(TestDevice64::REG64{});
        t.assert_eq(val, 0x1122334455667788ULL);
        mcu64.modify(TestDevice64::REG64::HIGH16::value(0xABCD));
        auto updated = mcu64.read(TestDevice64::REG64{});
        t.assert_eq(updated, 0x1122ABCD55667788ULL);
        return !t.failed;
    });

    return s.summary();
}
