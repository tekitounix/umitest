#include <cassert>
#include <print>
#include "test_transport.hh"
#include <cs43l22/cs43l22.hh>

namespace {

void test_verify_id() {
    test::MockI2cBus bus;
    test::I2cTransport<test::MockI2cBus> transport(bus, device::CS43L22::i2c_address);

    // Set chip ID: 0xE0 (CHIPID=0x1C, REVID=0)
    bus(device::CS43L22::i2c_address, 0x01) = 0xE0;

    device::CS43L22Driver driver(transport);
    assert(driver.verify_id());
    std::println("  verify_id (valid): PASS");

    // Invalid ID
    bus(device::CS43L22::i2c_address, 0x01) = 0x00;
    assert(!driver.verify_id());
    std::println("  verify_id (invalid): PASS");
}

void test_init() {
    test::MockI2cBus bus;
    test::I2cTransport<test::MockI2cBus> transport(bus, device::CS43L22::i2c_address);

    // Set valid chip ID
    bus(device::CS43L22::i2c_address, 0x01) = 0xE3;

    device::CS43L22Driver driver(transport);
    bool ok = driver.init(false);
    assert(ok);

    // Verify key register writes
    assert(bus(device::CS43L22::i2c_address, 0x02) == 0x01);  // PowerDown
    assert(bus(device::CS43L22::i2c_address, 0x04) == 0xAF);  // HeadphoneOn
    assert(bus(device::CS43L22::i2c_address, 0x05) == 0x81);  // Clock auto-detect
    assert(bus(device::CS43L22::i2c_address, 0x06) == 0x04);  // I2S 16-bit
    std::println("  init (16-bit): PASS");

    // Re-init with 24-bit
    bus(device::CS43L22::i2c_address, 0x01) = 0xE3;
    driver.init(true);
    assert(bus(device::CS43L22::i2c_address, 0x06) == 0x06);  // I2S 24-bit
    std::println("  init (24-bit): PASS");
}

void test_power() {
    test::MockI2cBus bus;
    test::I2cTransport<test::MockI2cBus> transport(bus, device::CS43L22::i2c_address);

    device::CS43L22Driver driver(transport);
    driver.power_on();
    assert(bus(device::CS43L22::i2c_address, 0x02) == 0x9E);
    std::println("  power_on: PASS");

    driver.power_off();
    assert(bus(device::CS43L22::i2c_address, 0x02) == 0x01);
    std::println("  power_off: PASS");
}

void test_volume() {
    test::MockI2cBus bus;
    test::I2cTransport<test::MockI2cBus> transport(bus, device::CS43L22::i2c_address);

    device::CS43L22Driver driver(transport);

    // 0dB -> reg 0x00
    driver.set_volume(0);
    assert(bus(device::CS43L22::i2c_address, 0x20) == 0x00);
    assert(bus(device::CS43L22::i2c_address, 0x21) == 0x00);
    std::println("  volume 0dB: PASS");

    // +12dB -> reg 0x18
    driver.set_volume(12);
    assert(bus(device::CS43L22::i2c_address, 0x20) == 0x18);
    std::println("  volume +12dB: PASS");

    // -1dB -> reg 0xFE
    driver.set_volume(-1);
    assert(bus(device::CS43L22::i2c_address, 0x20) == 0xFE);
    std::println("  volume -1dB: PASS");
}

void test_mute() {
    test::MockI2cBus bus;
    test::I2cTransport<test::MockI2cBus> transport(bus, device::CS43L22::i2c_address);

    device::CS43L22Driver driver(transport);

    driver.mute(true);
    assert(bus(device::CS43L22::i2c_address, 0x04) == 0xFF);
    std::println("  mute: PASS");

    driver.mute(false);
    assert(bus(device::CS43L22::i2c_address, 0x04) == 0xAF);
    std::println("  unmute: PASS");
}

} // namespace

int main() {
    std::println("Running CS43L22 driver tests...");
    test_verify_id();
    test_init();
    test_power();
    test_volume();
    test_mute();
    std::println("\nAll CS43L22 tests passed!");
    return 0;
}
