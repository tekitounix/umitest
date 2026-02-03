#include <type_traits>
#include <umi/test/test.hh>

#include "../mmio.hh"

struct TestDevice : umi::mmio::Device<umi::mmio::RW> {
    struct CONFIG : umi::mmio::Register<TestDevice, 0x00, 8> {
        using MIN_VALUE = umi::mmio::Value<CONFIG, 0>;
        using MAX_VALUE = umi::mmio::Value<CONFIG, 255>;
        using DEFAULT_VALUE = umi::mmio::Value<CONFIG, 100>;
    };

    struct ID : umi::mmio::Register<TestDevice, 0x10, 8> {
        using DEVICE_ID = umi::mmio::Value<ID, 0x42>;
        using REVISION = umi::mmio::Value<ID, 3>;
    };
};

static_assert(TestDevice::CONFIG::MIN_VALUE::get() == 0);
static_assert(TestDevice::CONFIG::MAX_VALUE::get() == 255);
static_assert(TestDevice::CONFIG::DEFAULT_VALUE::get() == 100);
static_assert(TestDevice::ID::DEVICE_ID::get() == 0x42);
static_assert(TestDevice::ID::REVISION::get() == 3);
static_assert(std::is_same_v<std::remove_const_t<TestDevice::CONFIG::MIN_VALUE::ValueType>, int>);
static_assert(std::is_same_v<std::remove_const_t<TestDevice::ID::DEVICE_ID::ValueType>, int>);

int main() {
    umi::test::Suite s("mmio_value_get");

    s.section("value get");
    s.run("Value::get()", [](umi::test::TestContext& t) {
        constexpr auto min_val = TestDevice::CONFIG::MIN_VALUE::get();
        constexpr auto max_val = TestDevice::CONFIG::MAX_VALUE::get();
        constexpr auto default_val = TestDevice::CONFIG::DEFAULT_VALUE::get();

        t.assert_eq(min_val, 0);
        t.assert_eq(max_val, 255);
        t.assert_eq(default_val, 100);
        return !t.failed;
    });

    return s.summary();
}
