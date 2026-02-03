#include <umi/test/test.hh>

#include "../mmio.hh"

// Test device with 8-bit registers
struct TestDevice : umi::mmio::Device<umi::mmio::RW> {
    // Power control register with predefined values
    struct POWER_CTL : umi::mmio::Register<TestDevice, 0x02, 8> {
        // Individual fields
        struct PWR_DOWN : umi::mmio::Field<POWER_CTL, 0, 1> {};
        struct ENABLE_A : umi::mmio::Field<POWER_CTL, 1, 1> {};
        struct ENABLE_B : umi::mmio::Field<POWER_CTL, 2, 1> {};

        // Common register values using Value template
        using PowerDown = umi::mmio::Value<POWER_CTL, 0x01>;
        using PowerUp = umi::mmio::Value<POWER_CTL, 0x9E>;
        using PartialPower = umi::mmio::Value<POWER_CTL, 0x06>;
    };

    // Configuration register
    struct CONFIG : umi::mmio::Register<TestDevice, 0x04, 8> {
        using DefaultConfig = umi::mmio::Value<CONFIG, 0xA5>;
        using TestConfig = umi::mmio::Value<CONFIG, 0x5A>;
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
class TestTransport : private umi::mmio::RegOps<TestTransport<CheckPolicy>, CheckPolicy> {
    friend class umi::mmio::RegOps<TestTransport<CheckPolicy>, CheckPolicy>;

  public:
    using umi::mmio::RegOps<TestTransport<CheckPolicy>, CheckPolicy>::write;
    using umi::mmio::RegOps<TestTransport<CheckPolicy>, CheckPolicy>::read;
    using umi::mmio::RegOps<TestTransport<CheckPolicy>, CheckPolicy>::modify;
    using umi::mmio::RegOps<TestTransport<CheckPolicy>, CheckPolicy>::is;
    using TransportTag = umi::mmio::DirectTransportTag;

    template <typename Reg>
    auto reg_read(Reg reg) const noexcept -> typename Reg::RegValueType {
        return TestBackend::reg_read(reg);
    }

    template <typename Reg>
    auto reg_write(Reg reg, typename Reg::RegValueType value) const noexcept -> void {
        TestBackend::reg_write(reg, value);
    }
};

int main() {
    umi::test::Suite s("mmio_register_value");

    s.section("register Value write");
    s.run("register Value writes", [](umi::test::TestContext& t) {
        TestTransport<> transport;

        transport.write(TestDevice::POWER_CTL::PowerDown{});
        t.assert_eq(static_cast<int>(transport.read(TestDevice::POWER_CTL{})), 0x01);

        transport.write(TestDevice::POWER_CTL::PowerUp{});
        t.assert_eq(static_cast<int>(transport.read(TestDevice::POWER_CTL{})), 0x9E);

        transport.write(TestDevice::POWER_CTL::PartialPower{});
        t.assert_eq(static_cast<int>(transport.read(TestDevice::POWER_CTL{})), 0x06);

        transport.write(TestDevice::CONFIG::DefaultConfig{});
        t.assert_eq(static_cast<int>(transport.read(TestDevice::CONFIG{})), 0xA5);

        transport.write(TestDevice::CONFIG::TestConfig{});
        t.assert_eq(static_cast<int>(transport.read(TestDevice::CONFIG{})), 0x5A);
        return !t.failed;
    });

    s.section("field Value still works");
    s.run("field Value write", [](umi::test::TestContext& t) {
        TestTransport<> transport;
        transport.write(TestDevice::POWER_CTL::PWR_DOWN::Set{});
        t.assert_eq(static_cast<int>(transport.read(TestDevice::POWER_CTL{})), 0x01);
        return !t.failed;
    });

    return s.summary();
}
