#include <umimmio.hh>
#include <umitest.hh>

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
        using PowerDown = mm::Value<POWER_CTL, 0x01>;
        using PowerUp = mm::Value<POWER_CTL, 0x9E>;
        using PartialPower = mm::Value<POWER_CTL, 0x06>;
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
    auto reg_write(Reg reg, typename Reg::RegValueType value) const noexcept -> void {
        TestBackend::reg_write(reg, value);
    }
};

int main() {
    umitest::Suite s("mmio_register_value");

    s.section("register Value write");
    s.run("register Value writes", [](umitest::TestContext& t) {
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
    s.run("field Value write", [](umitest::TestContext& t) {
        TestTransport<> transport;
        transport.write(TestDevice::POWER_CTL::PWR_DOWN::Set{});
        t.assert_eq(static_cast<int>(transport.read(TestDevice::POWER_CTL{})), 0x01);
        return !t.failed;
    });

    return s.summary();
}
