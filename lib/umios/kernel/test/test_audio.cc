// SPDX-License-Identifier: MIT
// UMI-OS Audio Engine Tests

#include <array>
#include <cstring>
#include <umios/kernel/umi_audio.hh>
#include <umitest.hh>

#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

using namespace umitest;

// Mock Hardware
struct MockHw {
    static inline std::uint32_t fake_cycles{0};

    static std::uint32_t cycle_count() { return fake_cycles; }
    static std::uint32_t cycles_per_usec() { return 168; }

    static void cache_clean(const void*, std::size_t) {}
    static void cache_invalidate(void*, std::size_t) {}

    static void reset() { fake_cycles = 0; }
};

// Mock Audio IO
struct MockAudioIO {
    static inline bool dma_running{false};
    static inline bool muted{true};
    static inline int start_count{0};
    static inline int stop_count{0};

    static void start_dma() {
        dma_running = true;
        ++start_count;
    }
    static void stop_dma() {
        dma_running = false;
        ++stop_count;
    }
    static void mute_output() { muted = true; }
    static void unmute_output() { muted = false; }
    static bool is_dma_running() { return dma_running; }

    static void reset() {
        dma_running = false;
        muted = true;
        start_count = 0;
        stop_count = 0;
    }
};

// Mock Kernel for panic
struct MockKernel {
    static inline bool panicked{false};
    static inline const char* panic_reason{nullptr};

    void panic(const umi::CrashDump& dump) {
        panicked = true;
        panic_reason = dump.reason;
    }

    static void reset() {
        panicked = false;
        panic_reason = nullptr;
    }
};

using Engine = umi::audio::AudioEngine<umi::Hw<MockHw>, MockAudioIO, float>;

// Test callback
static int callback_count = 0;
static void test_callback(const umi::audio::AudioBuffer<float>& input,
                          umi::audio::AudioBuffer<float>& output,
                          std::span<const umi::midi::Event> events) {
    (void)input;
    (void)events;
    ++callback_count;

    for (std::size_t i = 0; i < output.total_samples(); ++i) {
        output.data[i] = 0.1f;
    }
}

// Silent callback for standby test
static void silent_callback(const umi::audio::AudioBuffer<float>& input,
                            umi::audio::AudioBuffer<float>& output,
                            std::span<const umi::midi::Event> events) {
    (void)input;
    (void)output;
    (void)events;
}

bool test_configuration(TestContext& t) {
    umi::audio::AudioConfig cfg{};
    cfg.sample_rate = 44100;
    cfg.buffer_size = 64;
    cfg.output_channels = 2;

    Engine engine(cfg);
    t.assert_eq(engine.config().sample_rate, 44100u);
    t.assert_eq(engine.config().buffer_size, 64u);
    t.assert_eq(static_cast<int>(engine.state()), static_cast<int>(umi::audio::EngineState::STOPPED));
    return true;
}

bool test_lifecycle(TestContext& t) {
    MockAudioIO::reset();
    Engine engine;

    engine.start();
    t.assert_eq(static_cast<int>(engine.state()), static_cast<int>(umi::audio::EngineState::RUNNING));
    t.assert_true(MockAudioIO::dma_running, "DMA started");
    t.assert_true(!MockAudioIO::muted, "unmuted on start");

    engine.suspend();
    t.assert_eq(static_cast<int>(engine.state()), static_cast<int>(umi::audio::EngineState::SUSPENDED));
    t.assert_true(MockAudioIO::muted, "muted on suspend");

    engine.resume();
    t.assert_eq(static_cast<int>(engine.state()), static_cast<int>(umi::audio::EngineState::RUNNING));
    t.assert_true(!MockAudioIO::muted, "unmuted on resume");

    engine.stop();
    t.assert_eq(static_cast<int>(engine.state()), static_cast<int>(umi::audio::EngineState::STOPPED));
    t.assert_true(!MockAudioIO::dma_running, "DMA stopped");
    return true;
}

bool test_audio_callback(TestContext& t) {
    MockAudioIO::reset();
    MockKernel::reset();
    MockKernel kernel;
    callback_count = 0;

    Engine engine;
    engine.set_callback(test_callback);
    engine.start();

    std::array<float, 256> input_buf{};
    std::array<float, 256> output_buf{};
    umi::midi::EventBuffer<16> midi_events;

    MockHw::fake_cycles = 0;
    engine.on_buffer_complete(kernel, input_buf.data(), output_buf.data(), midi_events);
    MockHw::fake_cycles = 1000;

    t.assert_eq(callback_count, 1);
    t.assert_eq(engine.frame_count(), 128u);
    return true;
}

bool test_sample_accurate_clock(TestContext& t) {
    MockAudioIO::reset();
    MockKernel::reset();
    MockKernel kernel;

    umi::audio::AudioConfig cfg{};
    cfg.sample_rate = 48000;
    cfg.buffer_size = 128;
    Engine engine(cfg);
    engine.set_callback(test_callback);
    engine.start();

    std::array<float, 256> input_buf{};
    std::array<float, 256> output_buf{};
    umi::midi::EventBuffer<16> midi_events;

    for (int i = 0; i < 10; ++i) {
        engine.on_buffer_complete(kernel, input_buf.data(), output_buf.data(), midi_events);
    }

    t.assert_eq(engine.frame_count(), 1280u);
    double time = engine.time_seconds();
    t.assert_true(time > 0.026 && time < 0.028, "time_seconds");
    return true;
}

bool test_dsp_load_monitor(TestContext& t) {
    MockAudioIO::reset();
    MockKernel::reset();
    MockKernel kernel;

    umi::audio::AudioConfig cfg{};
    cfg.sample_rate = 48000;
    cfg.buffer_size = 128;
    Engine engine(cfg);
    engine.set_callback(test_callback);
    engine.start();

    std::array<float, 256> input_buf{};
    std::array<float, 256> output_buf{};
    umi::midi::EventBuffer<16> midi_events;

    MockHw::fake_cycles = 0;
    engine.on_buffer_complete(kernel, input_buf.data(), output_buf.data(), midi_events);

    t.assert_ge(engine.load_instant(), 0u);
    t.assert_ge(engine.load_peak(), 0u);
    return true;
}

bool test_overload_protection(TestContext& t) {
    MockAudioIO::reset();
    MockKernel::reset();
    MockKernel kernel;

    umi::audio::AudioConfig cfg{};
    cfg.max_drop_count = 3;
    Engine engine(cfg);
    engine.start();

    std::array<float, 256> input_buf{};
    std::array<float, 256> output_buf{};
    umi::midi::EventBuffer<16> midi_events;

    engine.on_buffer_complete(kernel, input_buf.data(), output_buf.data(), midi_events);
    t.assert_eq(engine.consecutive_drops(), 0u);
    t.assert_eq(engine.total_drops(), 0u);
    return true;
}

bool test_auto_standby(TestContext& t) {
    MockAudioIO::reset();
    MockKernel::reset();
    MockKernel kernel;

    umi::audio::AudioConfig cfg{};
    cfg.sample_rate = 48000;
    cfg.buffer_size = 128;
    cfg.silence_frames = 256;
    Engine engine(cfg);
    engine.set_callback(silent_callback);
    engine.start();

    std::array<float, 256> input_buf{};
    std::array<float, 256> output_buf{};
    umi::midi::EventBuffer<16> midi_events;

    t.assert_eq(static_cast<int>(engine.state()), static_cast<int>(umi::audio::EngineState::RUNNING));

    engine.on_buffer_complete(kernel, input_buf.data(), output_buf.data(), midi_events);
    t.assert_eq(static_cast<int>(engine.state()), static_cast<int>(umi::audio::EngineState::RUNNING));

    engine.on_buffer_complete(kernel, input_buf.data(), output_buf.data(), midi_events);
    t.assert_eq(static_cast<int>(engine.state()), static_cast<int>(umi::audio::EngineState::STANDBY));
    t.assert_true(MockAudioIO::muted, "muted in standby");

    engine.resume();
    t.assert_eq(static_cast<int>(engine.state()), static_cast<int>(umi::audio::EngineState::RUNNING));
    return true;
}

bool test_config_change(TestContext& t) {
    MockAudioIO::reset();
    Engine engine;

    t.assert_true(engine.set_sample_rate(96000), "can change sample rate when stopped");
    t.assert_eq(engine.config().sample_rate, 96000u);

    t.assert_true(engine.set_buffer_size(256), "can change buffer size when stopped");
    t.assert_eq(engine.config().buffer_size, 256u);

    engine.start();
    t.assert_true(!engine.set_sample_rate(44100), "cannot change sample rate when running");
    t.assert_true(!engine.set_buffer_size(128), "cannot change buffer size when running");
    return true;
}

bool test_audio_utilities(TestContext& t) {
    double secs = umi::audio::samples_to_seconds(48000, 48000);
    t.assert_near(secs, 1.0, 0.01);

    auto samples = umi::audio::seconds_to_samples(1.0, 48000);
    t.assert_eq(samples, 48000u);

    double spb = umi::audio::bpm_to_samples_per_beat(120.0, 48000);
    t.assert_near(spb, 24000.0, 1.0);

    float db = umi::audio::linear_to_db(1.0f);
    t.assert_near(db, 0.0f, 0.1f);

    float lin = umi::audio::db_to_linear(0.0f);
    t.assert_near(lin, 1.0f, 0.01f);
    return true;
}

int main() {
    Suite s("umios/kernel/audio");

    s.section("Configuration");
    s.run("configuration", test_configuration);

    s.section("Lifecycle");
    s.run("lifecycle", test_lifecycle);

    s.section("Audio Callback");
    s.run("audio callback", test_audio_callback);

    s.section("Sample-Accurate Clock");
    s.run("sample-accurate clock", test_sample_accurate_clock);

    s.section("DSP Load Monitor");
    s.run("dsp load monitor", test_dsp_load_monitor);

    s.section("Overload Protection");
    s.run("overload protection", test_overload_protection);

    s.section("Auto-Standby");
    s.run("auto-standby", test_auto_standby);

    s.section("Config Change");
    s.run("config change", test_config_change);

    s.section("Audio Utilities");
    s.run("audio utilities", test_audio_utilities);

    return s.summary();
}
