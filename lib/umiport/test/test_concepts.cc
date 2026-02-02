// SPDX-License-Identifier: MIT
// umiport concept satisfaction tests
// Verifies that stub implementations satisfy all HAL concepts

#include <umitest.hh>

using namespace umitest;

#include <cstdint>
#include <expected>
#include <functional>
#include <span>

#include "hal/result.hh"
#include "hal/arch.hh"
#include "hal/board.hh"
#include "hal/codec.hh"
#include "hal/fault.hh"
#include "hal/gpio.hh"
#include "hal/interrupt.hh"
#include "hal/timer.hh"
#include "hal/uart.hh"
#include "hal/i2c.hh"
#include "hal/i2s.hh"
#include "hal/audio.hh"

// ============================================================================
// Stub implementations for concept satisfaction
// ============================================================================

namespace stub {

using hal::ErrorCode;
using hal::Result;

// --- CacheOps ---
struct Cache {
    void enable_icache() {}
    void enable_dcache() {}
    void disable_icache() {}
    void disable_dcache() {}
    void invalidate_dcache(void*, std::size_t) {}
    void clean_dcache(void*, std::size_t) {}
};

// --- FpuOps ---
struct Fpu {
    void enable() {}
};

// --- ContextSwitch ---
struct Context {
    void* save_context() { return nullptr; }
    void restore_context(void*) {}
};

// --- ArchTraits ---
struct Arch {
    static constexpr bool has_dcache = true;
    static constexpr bool has_icache = true;
    static constexpr bool has_fpu = true;
    static constexpr bool has_double_fpu = false;
    static constexpr std::uint8_t mpu_regions = 8;
};

// --- BoardSpec ---
struct Board {
    static constexpr std::uint32_t system_clock_hz = 480'000'000;
    static constexpr std::uint32_t hse_clock_hz = 25'000'000;
};

// --- McuInit ---
struct Mcu {
    void init_clocks() {}
    void init_gpio() {}
};

// --- AudioCodec ---
struct Codec {
    bool init() { return true; }
    void power_on() {}
    void power_off() {}
    void set_volume(int) {}
    void mute(bool) {}
};

// --- FaultReport ---
struct Fault {
    std::uint32_t fault_type() const { return 0; }
    std::uintptr_t fault_address() const { return 0; }
    std::uintptr_t stack_pointer() const { return 0; }
};

// --- GpioPin ---
struct Pin {
    Result<void> set_direction(hal::gpio::Direction) { return {}; }
    Result<void> set_pull(hal::gpio::Pull) { return {}; }
    Result<void> write(hal::gpio::State) { return {}; }
    Result<hal::gpio::State> read() { return hal::gpio::State::LOW; }
    Result<void> set_output_type(hal::gpio::OutputType) { return {}; }
    Result<void> set_interrupt(hal::gpio::Trigger, hal::gpio::InterruptCallback) { return {}; }
    Result<void> enable_interrupt() { return {}; }
    Result<void> disable_interrupt() { return {}; }
    Result<void> toggle() { return {}; }
};

// --- GpioPort ---
struct Port {
    Result<void> write(std::uint8_t) { return {}; }
    Result<std::uint8_t> read() { return std::uint8_t{0}; }
    Result<void> write_masked(std::uint8_t, std::uint8_t) { return {}; }
    Result<void> set_bits(std::uint8_t) { return {}; }
    Result<void> clear_bits(std::uint8_t) { return {}; }
    Result<void> toggle_bits(std::uint8_t) { return {}; }
};

// --- InterruptController ---
struct Intc {
    Result<void> register_handler(std::uint32_t, hal::interrupt::Handler) { return {}; }
    Result<void> enable_irq(std::uint32_t) { return {}; }
    Result<void> disable_irq(std::uint32_t) { return {}; }
    Result<void> set_priority(std::uint32_t, hal::interrupt::Priority) { return {}; }
    void enable_global() {}
    void disable_global() {}
    bool is_enabled(std::uint32_t) { return false; }
    bool is_pending(std::uint32_t) { return false; }
    Result<void> clear_pending(std::uint32_t) { return {}; }
};

// --- Timer ---
struct Tim {
    Result<void> start() { return {}; }
    Result<void> stop() { return {}; }
    Result<void> reset() { return {}; }
    Result<void> set_period_us(std::uint32_t) { return {}; }
    Result<void> set_mode(hal::timer::Mode) { return {}; }
    Result<void> set_callback(hal::timer::Callback) { return {}; }
    Result<std::uint32_t> get_counter() { return std::uint32_t{0}; }
    Result<void> set_counter(std::uint32_t) { return {}; }
    Result<void> set_clock_source(hal::timer::ClockSource) { return {}; }
    Result<void> set_prescaler(std::uint32_t) { return {}; }
    Result<void> set_direction(hal::timer::Direction) { return {}; }
    Result<void> enable_capture(hal::timer::CaptureEvent, hal::timer::CaptureCallback) { return {}; }
    Result<void> disable_capture() { return {}; }
    Result<std::uint32_t> get_capture_value() { return std::uint32_t{0}; }
    bool is_running() { return false; }
    ErrorCode get_error() { return ErrorCode::OK; }
};

// --- DelayTimer ---
struct Delay {
    void delay_us(std::uint32_t) {}
    void delay_ms(std::uint32_t) {}
};

// --- PwmTimer ---
struct Pwm {
    Result<void> set_pwm_frequency(std::uint32_t) { return {}; }
    Result<void> set_duty_cycle_percent(std::uint8_t) { return {}; }
    Result<void> set_duty_cycle_raw(std::uint32_t) { return {}; }
    Result<void> start_pwm() { return {}; }
    Result<void> stop_pwm() { return {}; }
};

// --- Uart ---
struct UartDev {
    Result<void> init(const hal::uart::Config&) { return {}; }
    Result<void> deinit() { return {}; }
    Result<void> write_byte(std::uint8_t) { return {}; }
    Result<std::uint8_t> read_byte() { return std::uint8_t{0}; }
    Result<void> write(std::span<const std::uint8_t>) { return {}; }
    Result<std::size_t> read(std::span<std::uint8_t>) { return std::size_t{0}; }
    Result<std::size_t> write_with_timeout(std::span<const std::uint8_t>, std::uint32_t) { return std::size_t{0}; }
    Result<std::size_t> read_with_timeout(std::span<std::uint8_t>, std::uint32_t) { return std::size_t{0}; }
    Result<void> write_async(std::span<const std::uint8_t>, hal::uart::TxCallback) { return {}; }
    Result<void> read_async(std::span<std::uint8_t>, hal::uart::RxCallback) { return {}; }
    bool is_readable() { return false; }
    bool is_writable() { return true; }
    Result<void> flush_tx() { return {}; }
    Result<void> flush_rx() { return {}; }
    ErrorCode get_error() { return ErrorCode::OK; }
    Result<void> clear_error() { return {}; }
};

// --- I2cMaster ---
struct I2c {
    Result<void> init(hal::i2c::Speed) { return {}; }
    Result<void> deinit() { return {}; }
    Result<void> write(std::uint16_t, std::span<const std::uint8_t>) { return {}; }
    Result<void> read(std::uint16_t, std::span<std::uint8_t>) { return {}; }
    Result<void> write_read(std::uint16_t, std::span<const std::uint8_t>, std::span<std::uint8_t>) { return {}; }
    Result<void> write_async(std::uint16_t, std::span<const std::uint8_t>, hal::i2c::TransferCallback) { return {}; }
    Result<void> read_async(std::uint16_t, std::span<std::uint8_t>, hal::i2c::TransferCallback) { return {}; }
    Result<void> write_read_async(std::uint16_t, std::span<const std::uint8_t>, std::span<std::uint8_t>, hal::i2c::TransferCallback) { return {}; }
    bool is_busy() { return false; }
    Result<void> abort() { return {}; }
    Result<void> reset() { return {}; }
    Result<void> is_device_ready(std::uint16_t) { return {}; }
};

// --- I2sMaster ---
struct I2s {
    Result<void> init(const hal::i2s::Config&) { return {}; }
    Result<void> deinit() { return {}; }
    Result<void> transmit(std::span<const std::uint16_t>) { return {}; }
    Result<void> receive(std::span<std::uint16_t>) { return {}; }
    Result<void> transmit_receive(std::span<const std::uint16_t>, std::span<std::uint16_t>) { return {}; }
    Result<void> transmit_async(std::span<const std::uint16_t>, hal::i2s::TransferCallback) { return {}; }
    Result<void> receive_async(std::span<std::uint16_t>, hal::i2s::TransferCallback) { return {}; }
    Result<void> transmit_receive_async(std::span<const std::uint16_t>, std::span<std::uint16_t>, hal::i2s::TransferCallback) { return {}; }
    Result<void> start_continuous_transmit(std::span<const std::uint16_t>, hal::BufferCallback) { return {}; }
    Result<void> start_continuous_receive(std::span<std::uint16_t>, hal::BufferCallback) { return {}; }
    Result<void> stop_continuous() { return {}; }
    bool is_busy() { return false; }
    ErrorCode get_error() { return ErrorCode::OK; }
    Result<void> abort() { return {}; }
};

// --- AudioDevice ---
struct AudioDev {
    Result<void> configure(const hal::audio::Config&) { return {}; }
    hal::audio::Config get_config() { return {}; }
    bool is_config_supported(const hal::audio::Config&) { return true; }
    Result<void> start() { return {}; }
    Result<void> stop() { return {}; }
    Result<void> pause() { return {}; }
    Result<void> resume() { return {}; }
    hal::audio::State get_state() { return hal::audio::State::STOPPED; }
    std::size_t get_buffer_size() { return 256; }
    std::span<const std::uint16_t> get_available_buffer_sizes() { return {}; }
    std::uint32_t get_latency() { return 0; }
    // CallbackAudioDevice
    Result<void> set_callback(hal::audio::Callback<std::int16_t>) { return {}; }
    // BlockingAudioDevice
    Result<std::size_t> write(std::span<std::int16_t>) { return std::size_t{0}; }
    Result<std::size_t> read(std::span<std::int16_t>) { return std::size_t{0}; }
};

} // namespace stub

// ============================================================================
// Static assertions — compile-time concept satisfaction
// ============================================================================

static_assert(hal::CacheOps<stub::Cache>);
static_assert(hal::FpuOps<stub::Fpu>);
static_assert(hal::ContextSwitch<stub::Context>);
static_assert(hal::ArchTraits<stub::Arch>);
static_assert(hal::BoardSpec<stub::Board>);
static_assert(hal::McuInit<stub::Mcu>);
static_assert(hal::AudioCodec<stub::Codec>);
static_assert(hal::FaultReport<stub::Fault>);
static_assert(hal::GpioPin<stub::Pin>);
static_assert(hal::GpioPort<stub::Port>);
static_assert(hal::InterruptController<stub::Intc>);
static_assert(hal::Timer<stub::Tim>);
static_assert(hal::DelayTimer<stub::Delay>);
static_assert(hal::PwmTimer<stub::Pwm>);
static_assert(hal::Uart<stub::UartDev>);
static_assert(hal::I2cMaster<stub::I2c>);
static_assert(hal::I2sMaster<stub::I2s>);
static_assert(hal::audio::AudioDevice<stub::AudioDev>);
static_assert(hal::audio::CallbackAudioDevice<stub::AudioDev, std::int16_t>);
static_assert(hal::audio::BlockingAudioDevice<stub::AudioDev, std::int16_t>);

// ============================================================================
// Runtime tests — verify stub behavior and Result<T>
// ============================================================================

static void test_result_basics(Suite& s) {
    s.section("Result<T> basics");
    {
        hal::Result<int> ok = 42;
        s.check(ok.has_value(), "Result<int> has value");
        s.check_eq(*ok, 42);

        hal::Result<void> ok_void{};
        s.check(ok_void.has_value(), "Result<void> has value");

        hal::Result<int> err = std::unexpected(hal::ErrorCode::TIMEOUT);
        s.check(!err.has_value(), "Error result has no value");
        s.check_eq(static_cast<int>(err.error()), static_cast<int>(hal::ErrorCode::TIMEOUT));
    }
}

static void test_cache_ops(Suite& s) {
    s.section("CacheOps satisfaction");
    {
        stub::Cache cache;
        cache.enable_icache();
        cache.enable_dcache();
        cache.disable_icache();
        cache.disable_dcache();
        int dummy = 0;
        cache.invalidate_dcache(&dummy, sizeof(dummy));
        cache.clean_dcache(&dummy, sizeof(dummy));
        s.check(true, "CacheOps stub callable");
    }
}

static void test_arch_traits(Suite& s) {
    s.section("ArchTraits satisfaction");
    s.check(stub::Arch::has_dcache, "has_dcache");
    s.check(stub::Arch::has_icache, "has_icache");
    s.check(stub::Arch::has_fpu, "has_fpu");
    s.check(!stub::Arch::has_double_fpu, "no double FPU");
    s.check_eq(static_cast<int>(stub::Arch::mpu_regions), 8);
}

static void test_board_spec(Suite& s) {
    s.section("BoardSpec satisfaction");
    s.check_eq(static_cast<int>(stub::Board::system_clock_hz / 1'000'000), 480);
    s.check_eq(static_cast<int>(stub::Board::hse_clock_hz / 1'000'000), 25);
}

static void test_audio_codec(Suite& s) {
    s.section("AudioCodec satisfaction");
    {
        stub::Codec codec;
        s.check(codec.init(), "Codec init returns true");
        codec.power_on();
        codec.set_volume(-6);
        codec.mute(true);
        codec.power_off();
        s.check(true, "AudioCodec stub callable");
    }
}

static void test_fault_report(Suite& s) {
    s.section("FaultReport satisfaction");
    {
        stub::Fault fault;
        s.check_eq(static_cast<int>(fault.fault_type()), 0);
        s.check_eq(static_cast<int>(fault.fault_address()), 0);
        s.check_eq(static_cast<int>(fault.stack_pointer()), 0);
    }
}

static void test_gpio_pin(Suite& s) {
    s.section("GpioPin satisfaction");
    {
        stub::Pin pin;
        s.check(pin.set_direction(hal::gpio::Direction::OUTPUT).has_value(), "set_direction");
        s.check(pin.write(hal::gpio::State::HIGH).has_value(), "write HIGH");
        auto state = pin.read();
        s.check(state.has_value(), "read returns value");
        s.check(pin.toggle().has_value(), "toggle");
    }
}

static void test_interrupt_controller(Suite& s) {
    s.section("InterruptController satisfaction");
    {
        stub::Intc intc;
        s.check(intc.register_handler(0, nullptr).has_value(), "register_handler");
        s.check(intc.enable_irq(0).has_value(), "enable_irq");
        intc.enable_global();
        intc.disable_global();
        s.check(!intc.is_enabled(0), "irq not enabled (stub)");
    }
}

static void test_critical_section(Suite& s) {
    s.section("CriticalSection RAII");
    {
        stub::Intc intc;
        { hal::CriticalSection cs(intc); }
        s.check(true, "CriticalSection construct/destruct");
    }
}

static void test_timer(Suite& s) {
    s.section("Timer satisfaction");
    {
        stub::Tim tim;
        s.check(tim.start().has_value(), "start");
        s.check(tim.stop().has_value(), "stop");
        s.check(tim.set_period_us(1000).has_value(), "set_period_us");
        s.check(!tim.is_running(), "not running (stub)");
        s.check_eq(static_cast<int>(tim.get_error()), 0);
    }
}

static void test_uart(Suite& s) {
    s.section("Uart satisfaction");
    {
        stub::UartDev uart;
        hal::uart::Config cfg{};
        s.check(uart.init(cfg).has_value(), "init");
        s.check(uart.write_byte(0x55).has_value(), "write_byte");
        auto byte = uart.read_byte();
        s.check(byte.has_value(), "read_byte");
        s.check(uart.is_writable(), "is_writable");
        s.check(uart.deinit().has_value(), "deinit");
    }
}

static void test_i2c_master(Suite& s) {
    s.section("I2cMaster satisfaction");
    {
        stub::I2c i2c;
        s.check(i2c.init(hal::i2c::Speed::FAST).has_value(), "init");
        std::uint8_t buf[4]{};
        s.check(i2c.write(0x50, std::span{buf}).has_value(), "write");
        s.check(i2c.read(0x50, std::span{buf}).has_value(), "read");
        s.check(!i2c.is_busy(), "not busy");
    }
}

static void test_i2s_master(Suite& s) {
    s.section("I2sMaster satisfaction");
    {
        stub::I2s i2s;
        hal::i2s::Config cfg{};
        s.check(i2s.init(cfg).has_value(), "init");
        std::uint16_t buf[8]{};
        s.check(i2s.transmit(std::span{buf}).has_value(), "transmit");
        s.check(i2s.receive(std::span{buf}).has_value(), "receive");
        s.check(!i2s.is_busy(), "not busy");
    }
}

static void test_audio_device(Suite& s) {
    s.section("AudioDevice satisfaction");
    {
        stub::AudioDev dev;
        hal::audio::Config cfg{};
        s.check(dev.configure(cfg).has_value(), "configure");
        s.check(dev.is_config_supported(cfg), "config supported");
        s.check(dev.start().has_value(), "start");
        s.check_eq(static_cast<int>(dev.get_state()), static_cast<int>(hal::audio::State::STOPPED));
        s.check_eq(static_cast<int>(dev.get_buffer_size()), 256);
    }
}

int main() {
    Suite s("port_concepts");

    test_result_basics(s);
    test_cache_ops(s);
    test_arch_traits(s);
    test_board_spec(s);
    test_audio_codec(s);
    test_fault_report(s);
    test_gpio_pin(s);
    test_interrupt_controller(s);
    test_critical_section(s);
    test_timer(s);
    test_uart(s);
    test_i2c_master(s);
    test_i2s_master(s);
    test_audio_device(s);

    return s.summary();
}
