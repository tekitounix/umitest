// SPDX-License-Identifier: MIT
// AudioDriver concept sequence tests
// Tests start/stop/configure flow using stub implementation

#include <umitest.hh>

using namespace umitest;

#include <cstdint>
#include <expected>
#include <functional>
#include <span>

#include "hal/result.hh"
#include "hal/audio.hh"
#include "hal/codec.hh"

// ============================================================================
// Stub AudioDevice with state tracking
// ============================================================================

namespace {

using hal::ErrorCode;
using hal::Result;

struct TestAudioDev {
    hal::audio::Config config_{};
    hal::audio::State state_ = hal::audio::State::STOPPED;
    int configure_count = 0;
    int start_count = 0;
    int stop_count = 0;

    Result<void> configure(const hal::audio::Config& cfg) {
        config_ = cfg;
        ++configure_count;
        return {};
    }
    hal::audio::Config get_config() const { return config_; }
    bool is_config_supported(const hal::audio::Config& cfg) const {
        return cfg.sample_rate == 48000 || cfg.sample_rate == 44100;
    }
    Result<void> start() {
        state_ = hal::audio::State::RUNNING;
        ++start_count;
        return {};
    }
    Result<void> stop() {
        state_ = hal::audio::State::STOPPED;
        ++stop_count;
        return {};
    }
    Result<void> pause() {
        state_ = hal::audio::State::PAUSED;
        return {};
    }
    Result<void> resume() {
        state_ = hal::audio::State::RUNNING;
        return {};
    }
    hal::audio::State get_state() const { return state_; }
    std::size_t get_buffer_size() const { return config_.buffer_size; }
    std::span<const std::uint16_t> get_available_buffer_sizes() const {
        static constexpr std::uint16_t sizes[] = {64, 128, 256, 512};
        return sizes;
    }
    std::uint32_t get_latency() const { return config_.buffer_size * 1'000'000u / config_.sample_rate; }
};

static_assert(hal::audio::AudioDevice<TestAudioDev>);

// ============================================================================
// Stub AudioCodec
// ============================================================================

struct TestCodec {
    bool initialized_ = false;
    int volume_db_ = -10;
    bool muted_ = false;
    bool powered_ = false;

    bool init() { initialized_ = true; return true; }
    void power_on() { powered_ = true; }
    void power_off() { powered_ = false; }
    void set_volume(int vol_db) { volume_db_ = vol_db; }
    void mute(bool state) { muted_ = state; }
};

static_assert(hal::AudioCodec<TestCodec>);

} // namespace

static void test_configure_start_stop(Suite& s) {
    s.section("AudioDevice configure → start → stop sequence");
    {
        TestAudioDev dev;
        hal::audio::Config cfg{};
        cfg.sample_rate = 48000;
        cfg.buffer_size = 256;
        cfg.channels = 2;

        s.check(dev.configure(cfg).has_value(), "configure");
        s.check_eq(static_cast<int>(dev.get_state()), static_cast<int>(hal::audio::State::STOPPED));
        s.check(dev.start().has_value(), "start");
        s.check_eq(static_cast<int>(dev.get_state()), static_cast<int>(hal::audio::State::RUNNING));
        s.check(dev.stop().has_value(), "stop");
        s.check_eq(static_cast<int>(dev.get_state()), static_cast<int>(hal::audio::State::STOPPED));
    }
}

static void test_pause_resume(Suite& s) {
    s.section("AudioDevice pause → resume");
    {
        TestAudioDev dev;
        hal::audio::Config cfg{};
        dev.configure(cfg);
        dev.start();
        s.check(dev.pause().has_value(), "pause");
        s.check_eq(static_cast<int>(dev.get_state()), static_cast<int>(hal::audio::State::PAUSED));
        s.check(dev.resume().has_value(), "resume");
        s.check_eq(static_cast<int>(dev.get_state()), static_cast<int>(hal::audio::State::RUNNING));
    }
}

static void test_config_supported(Suite& s) {
    s.section("AudioDevice config supported check");
    {
        TestAudioDev dev;
        hal::audio::Config cfg48k{};
        cfg48k.sample_rate = 48000;
        s.check(dev.is_config_supported(cfg48k), "48kHz supported");

        hal::audio::Config cfg96k{};
        cfg96k.sample_rate = 96000;
        s.check(!dev.is_config_supported(cfg96k), "96kHz not supported");
    }
}

static void test_buffer_sizes(Suite& s) {
    s.section("AudioDevice buffer sizes");
    {
        TestAudioDev dev;
        auto sizes = dev.get_available_buffer_sizes();
        s.check_eq(static_cast<int>(sizes.size()), 4);
        s.check_eq(static_cast<int>(sizes[0]), 64);
        s.check_eq(static_cast<int>(sizes[3]), 512);
    }
}

static void test_latency(Suite& s) {
    s.section("AudioDevice latency calculation");
    {
        TestAudioDev dev;
        hal::audio::Config cfg{};
        cfg.sample_rate = 48000;
        cfg.buffer_size = 256;
        dev.configure(cfg);
        // 256 / 48000 ≈ 5333us
        s.check_eq(static_cast<int>(dev.get_latency()), 5333);
    }
}

static void test_call_counts(Suite& s) {
    s.section("AudioDevice call counts");
    {
        TestAudioDev dev;
        hal::audio::Config cfg{};
        dev.configure(cfg);
        dev.configure(cfg);
        dev.start();
        dev.stop();
        dev.start();
        s.check_eq(dev.configure_count, 2);
        s.check_eq(dev.start_count, 2);
        s.check_eq(dev.stop_count, 1);
    }
}

static void test_audio_codec(Suite& s) {
    s.section("AudioCodec init → power_on → set_volume → mute");
    {
        TestCodec codec;
        s.check(!codec.initialized_, "not initialized");
        s.check(codec.init(), "init returns true");
        s.check(codec.initialized_, "initialized");
        codec.power_on();
        s.check(codec.powered_, "powered on");
        codec.set_volume(-6);
        s.check_eq(codec.volume_db_, -6);
        codec.mute(true);
        s.check(codec.muted_, "muted");
        codec.mute(false);
        s.check(!codec.muted_, "unmuted");
        codec.power_off();
        s.check(!codec.powered_, "powered off");
    }
}

int main() {
    Suite s("port_audio");

    test_configure_start_stop(s);
    test_pause_resume(s);
    test_config_supported(s);
    test_buffer_sizes(s);
    test_latency(s);
    test_call_counts(s);
    test_audio_codec(s);

    return s.summary();
}
