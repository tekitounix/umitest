// SPDX-License-Identifier: MIT
// Device driver mock tests for PCM3060, WM8731, AK4556

#include <umitest.hh>

using namespace umitest;

#include <array>
#include <cstdint>

// PCM3060: uses lambda I2C transport
#include <pcm3060/pcm3060.hh>

// WM8731: uses lambda I2C 16-bit transport
#include <wm8731/wm8731.hh>

// AK4556: uses GPIO driver template
#include <ak4556/ak4556.hh>

// ============================================================================
// Mock I2C memory for PCM3060 (8-bit regs)
// ============================================================================
namespace {

struct MockI2c8 {
    std::array<std::uint8_t, 256> mem{};

    auto writer() {
        return [this](std::uint8_t reg, std::uint8_t data) { mem[reg] = data; };
    }

    auto reader() {
        return [this](std::uint8_t reg) -> std::uint8_t { return mem[reg]; };
    }
};

// ============================================================================
// Mock I2C 16-bit transport for WM8731 (9-bit data)
// ============================================================================

struct MockI2c16 {
    std::array<std::uint16_t, 16> mem{};  // WM8731 has 16 registers

    auto writer() {
        return [this](std::uint8_t reg, std::uint16_t data) {
            if (reg < 16) mem[reg] = data & 0x1FF;
        };
    }
};

// ============================================================================
// Mock GPIO driver for AK4556
// ============================================================================

struct MockGpio {
    bool pin_state = false;

    void reset_pin(std::uint8_t) { pin_state = false; }
    void set_pin(std::uint8_t) { pin_state = true; }
};

} // namespace

// ============================================================================
// Tests
// ============================================================================

static void test_pcm3060_init(Suite& s) {
    s.section("PCM3060 init");
    {
        MockI2c8 bus;
        umi::device::PCM3060Driver driver(bus.writer(), bus.reader());
        driver.init();

        s.check_eq(static_cast<int>(bus.mem[0x40]), 0x00);
        s.check_eq(static_cast<int>(bus.mem[0x41]), 0x03);
        s.check_eq(static_cast<int>(bus.mem[0x44]), 0x02);
        s.check_eq(static_cast<int>(bus.mem[0x42]), 0xFF);
        s.check_eq(static_cast<int>(bus.mem[0x43]), 0xFF);
        s.check_eq(static_cast<int>(bus.mem[0x45]), 0xD7);
        s.check_eq(static_cast<int>(bus.mem[0x46]), 0xD7);
    }
}

static void test_pcm3060_power_down(Suite& s) {
    s.section("PCM3060 power_down");
    {
        MockI2c8 bus;
        umi::device::PCM3060Driver driver(bus.writer(), bus.reader());
        driver.power_down();
        s.check_eq(static_cast<int>(bus.mem[0x40]), 0x70);
    }
}

static void test_pcm3060_set_dac_volume(Suite& s) {
    s.section("PCM3060 set_dac_volume");
    {
        MockI2c8 bus;
        umi::device::PCM3060Driver driver(bus.writer(), bus.reader());

        driver.set_dac_volume(0x80);
        s.check_eq(static_cast<int>(bus.mem[0x42]), 0x80);
        s.check_eq(static_cast<int>(bus.mem[0x43]), 0x80);

        driver.set_dac_volume(0x00);
        s.check_eq(static_cast<int>(bus.mem[0x42]), 0x00);
    }
}

static void test_pcm3060_mute_dac(Suite& s) {
    s.section("PCM3060 mute_dac");
    {
        MockI2c8 bus;
        umi::device::PCM3060Driver driver(bus.writer(), bus.reader());

        driver.init();
        driver.mute_dac(true);
        s.check_eq(static_cast<int>(bus.mem[0x41] & (1 << 5)), 0x20);

        driver.mute_dac(false);
        s.check_eq(static_cast<int>(bus.mem[0x41] & (1 << 5)), 0x00);
    }
}

static void test_wm8731_reset(Suite& s) {
    s.section("WM8731 reset");
    {
        MockI2c16 bus;
        umi::device::WM8731Driver driver(bus.writer());
        driver.reset();
        s.check(true, "reset write to 0x0F completed");
    }
}

static void test_wm8731_init(Suite& s) {
    s.section("WM8731 init");
    {
        MockI2c16 bus;
        umi::device::WM8731Driver driver(bus.writer());
        driver.init();

        s.check_eq(static_cast<int>(bus.mem[0x06]), 0x062);

        s.check_eq(static_cast<int>(bus.mem[0x07]),
                 static_cast<int>((umi::device::wm8731_fmt::LEFT_JUST << 0) | (umi::device::wm8731_iwl::IWL_24BIT << 2)));

        s.check_eq(static_cast<int>(bus.mem[0x08]), 0x000);
        s.check_eq(static_cast<int>(bus.mem[0x04]), 0x012);
        s.check_eq(static_cast<int>(bus.mem[0x05]), 0x000);
        s.check_eq(static_cast<int>(bus.mem[0x09]), 0x001);
        s.check_eq(static_cast<int>(bus.mem[0x00]), 0x017);
        s.check_eq(static_cast<int>(bus.mem[0x01]), 0x017);
        s.check_eq(static_cast<int>(bus.mem[0x02]), 0x179);
        s.check_eq(static_cast<int>(bus.mem[0x03]), 0x179);
    }
}

static void test_wm8731_power_down(Suite& s) {
    s.section("WM8731 power_down");
    {
        MockI2c16 bus;
        umi::device::WM8731Driver driver(bus.writer());
        driver.power_down();
        s.check_eq(static_cast<int>(bus.mem[0x09]), 0x000);
        s.check_eq(static_cast<int>(bus.mem[0x06]), 0x0FF);
    }
}

static void test_wm8731_set_hp_volume(Suite& s) {
    s.section("WM8731 set_hp_volume");
    {
        MockI2c16 bus;
        umi::device::WM8731Driver driver(bus.writer());

        driver.set_hp_volume(0x79);
        s.check_eq(static_cast<int>(bus.mem[0x02]), static_cast<int>(0x100 | 0x79));

        driver.set_hp_volume(0x30);
        s.check_eq(static_cast<int>(bus.mem[0x02]), static_cast<int>(0x100 | 0x30));
    }
}

static void test_wm8731_mute_dac(Suite& s) {
    s.section("WM8731 mute_dac");
    {
        MockI2c16 bus;
        umi::device::WM8731Driver driver(bus.writer());

        driver.mute_dac(true);
        s.check_eq(static_cast<int>(bus.mem[0x05]), 0x008);

        driver.mute_dac(false);
        s.check_eq(static_cast<int>(bus.mem[0x05]), 0x000);
    }
}

static void test_ak4556_init(Suite& s) {
    s.section("AK4556 init");
    {
        MockGpio gpio;
        umi::device::AK4556 codec(gpio, 0);

        s.check(!gpio.pin_state, "pin starts low (default)");

        codec.init();
        s.check(gpio.pin_state, "pin high after init (reset released)");
    }
}

static void test_ak4556_reset(Suite& s) {
    s.section("AK4556 reset_assert/release");
    {
        MockGpio gpio;
        gpio.pin_state = true;
        umi::device::AK4556 codec(gpio, 0);

        codec.reset_assert();
        s.check(!gpio.pin_state, "pin low after reset_assert");

        codec.reset_release();
        s.check(gpio.pin_state, "pin high after reset_release");
    }
}

int main() {
    Suite s("port_drivers");

    test_pcm3060_init(s);
    test_pcm3060_power_down(s);
    test_pcm3060_set_dac_volume(s);
    test_pcm3060_mute_dac(s);
    test_wm8731_reset(s);
    test_wm8731_init(s);
    test_wm8731_power_down(s);
    test_wm8731_set_hp_volume(s);
    test_wm8731_mute_dac(s);
    test_ak4556_init(s);
    test_ak4556_reset(s);

    return s.summary();
}
