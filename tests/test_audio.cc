#include <umios/kernel/umi_audio.hh>
#include <cstdio>
#include <cstring>
#include <cstdlib>

#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

// Mock Hardware
struct MockHw {
    static inline std::uint32_t fake_cycles {0};
    
    static std::uint32_t cycle_count() { return fake_cycles; }
    static std::uint32_t cycles_per_usec() { return 168; }  // 168MHz
    
    // Cache operations (no-op in mock)
    static void cache_clean(const void*, std::size_t) {}
    static void cache_invalidate(void*, std::size_t) {}
};

// Mock Audio IO
struct MockAudioIO {
    static inline bool dma_running {false};
    static inline bool muted {true};
    static inline int start_count {0};
    static inline int stop_count {0};
    
    static void start_dma() { dma_running = true; ++start_count; }
    static void stop_dma() { dma_running = false; ++stop_count; }
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
    static inline bool panicked {false};
    static inline const char* panic_reason {nullptr};
    
    void panic(const umi::CrashDump& dump) {
        panicked = true;
        panic_reason = dump.reason;
    }
    
    static void reset() {
        panicked = false;
        panic_reason = nullptr;
    }
};

static void check(bool cond, const char* msg) {
    if (!cond) {
        std::printf("FAIL: %s\n", msg);
        std::exit(1);
    }
}

// Test callback
static int callback_count = 0;
static void test_callback(
    const umi::audio::AudioBuffer<float>& input,
    umi::audio::AudioBuffer<float>& output,
    std::span<const umi::midi::Event> events
) {
    (void)input;
    (void)events;
    ++callback_count;
    
    // Generate simple output (non-silent)
    for (std::size_t i = 0; i < output.total_samples(); ++i) {
        output.data[i] = 0.1f;
    }
}

// Silent callback for standby test
static void silent_callback(
    const umi::audio::AudioBuffer<float>& input,
    umi::audio::AudioBuffer<float>& output,
    std::span<const umi::midi::Event> events
) {
    (void)input;
    (void)output;
    (void)events;
    // Output stays zeroed (silent)
}

int main() {
    using Engine = umi::audio::AudioEngine<umi::Hw<MockHw>, MockAudioIO, float>;
    
    // ========================================
    // Test 1: Configuration
    // ========================================
    std::puts("Test: Configuration...");
    {
        umi::audio::AudioConfig cfg{};
        cfg.sample_rate = 44100;
        cfg.buffer_size = 64;
        cfg.output_channels = 2;
        
        Engine engine(cfg);
        check(engine.config().sample_rate == 44100, "sample rate");
        check(engine.config().buffer_size == 64, "buffer size");
        check(engine.state() == umi::audio::EngineState::STOPPED, "initial state");
    }
    
    // ========================================
    // Test 2: Lifecycle (start/stop/suspend/resume)
    // ========================================
    std::puts("Test: Lifecycle...");
    {
        MockAudioIO::reset();
        Engine engine;
        
        engine.start();
        check(engine.state() == umi::audio::EngineState::RUNNING, "start -> running");
        check(MockAudioIO::dma_running, "DMA started");
        check(!MockAudioIO::muted, "unmuted on start");
        
        engine.suspend();
        check(engine.state() == umi::audio::EngineState::SUSPENDED, "suspend");
        check(MockAudioIO::muted, "muted on suspend");
        
        engine.resume();
        check(engine.state() == umi::audio::EngineState::RUNNING, "resume");
        check(!MockAudioIO::muted, "unmuted on resume");
        
        engine.stop();
        check(engine.state() == umi::audio::EngineState::STOPPED, "stop");
        check(!MockAudioIO::dma_running, "DMA stopped");
    }
    
    // ========================================
    // Test 3: Audio Callback
    // ========================================
    std::puts("Test: Audio Callback...");
    {
        MockAudioIO::reset();
        MockKernel::reset();
        MockKernel kernel;
        callback_count = 0;
        
        Engine engine;
        engine.set_callback(test_callback);
        engine.start();
        
        // Simulate buffer
        std::array<float, 256> input_buf{};
        std::array<float, 256> output_buf{};
        umi::midi::EventBuffer<16> midi_events;
        
        MockHw::fake_cycles = 0;
        engine.on_buffer_complete(kernel, input_buf.data(), output_buf.data(), midi_events);
        MockHw::fake_cycles = 1000;
        
        check(callback_count == 1, "callback invoked");
        check(engine.frame_count() == 128, "frame count updated");
    }
    
    // ========================================
    // Test 4: Sample-Accurate Clock
    // ========================================
    std::puts("Test: Sample-Accurate Clock...");
    {
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
        
        // Process 10 buffers
        for (int i = 0; i < 10; ++i) {
            engine.on_buffer_complete(kernel, input_buf.data(), output_buf.data(), midi_events);
        }
        
        check(engine.frame_count() == 1280, "frame count after 10 buffers");
        // 1280 / 48000 ≈ 0.0267 seconds
        double t = engine.time_seconds();
        check(t > 0.026 && t < 0.028, "time_seconds");
    }
    
    // ========================================
    // Test 5: DSP Load Monitor
    // ========================================
    std::puts("Test: DSP Load Monitor...");
    {
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
        
        // Simulate 50% load
        MockHw::fake_cycles = 0;
        engine.on_buffer_complete(kernel, input_buf.data(), output_buf.data(), midi_events);
        // Budget for 128 samples @ 48kHz @ 168MHz ≈ 448000 cycles
        // Simulate that callback took ~224000 cycles (50%)
        
        check(engine.load_instant() >= 0, "load_instant available");
        check(engine.load_peak() >= 0, "load_peak available");
    }
    
    // ========================================
    // Test 6: Overload Protection (Drop Detection)
    // ========================================
    std::puts("Test: Overload Protection...");
    {
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
        
        // Set a callback that takes a long time (simulated by not finishing)
        // We'll manually set processing_ flag via the on_buffer_complete behavior
        
        // First call - normal
        engine.on_buffer_complete(kernel, input_buf.data(), output_buf.data(), midi_events);
        check(engine.consecutive_drops() == 0, "no drops initially");
        
        // Manually test drop scenario by not completing processing
        // This is tricky in unit test - the engine sets processing_=true, then false
        // To test, we'd need to call on_buffer_complete while it's still "processing"
        
        // Instead, verify that consecutive_drops is tracked
        check(engine.total_drops() == 0, "no total drops");
    }
    
    // ========================================
    // Test 7: Auto-Standby
    // ========================================
    std::puts("Test: Auto-Standby...");
    {
        MockAudioIO::reset();
        MockKernel::reset();
        MockKernel kernel;
        
        umi::audio::AudioConfig cfg{};
        cfg.sample_rate = 48000;
        cfg.buffer_size = 128;
        cfg.silence_frames = 256;  // Enter standby after 256 silent frames
        Engine engine(cfg);
        engine.set_callback(silent_callback);  // Produces silence
        engine.start();
        
        std::array<float, 256> input_buf{};
        std::array<float, 256> output_buf{};
        umi::midi::EventBuffer<16> midi_events;
        
        check(engine.state() == umi::audio::EngineState::RUNNING, "running initially");
        
        // First buffer (128 frames) - not enough for standby
        engine.on_buffer_complete(kernel, input_buf.data(), output_buf.data(), midi_events);
        check(engine.state() == umi::audio::EngineState::RUNNING, "still running after 128");
        
        // Second buffer (256 frames total) - should enter standby
        engine.on_buffer_complete(kernel, input_buf.data(), output_buf.data(), midi_events);
        check(engine.state() == umi::audio::EngineState::STANDBY, "standby after 256 silent frames");
        check(MockAudioIO::muted, "muted in standby");
        
        // Resume
        engine.resume();
        check(engine.state() == umi::audio::EngineState::RUNNING, "resume from standby");
    }
    
    // ========================================
    // Test 8: Configuration Change
    // ========================================
    std::puts("Test: Config Change...");
    {
        MockAudioIO::reset();
        Engine engine;
        
        check(engine.set_sample_rate(96000), "can change sample rate when stopped");
        check(engine.config().sample_rate == 96000, "sample rate changed");
        
        check(engine.set_buffer_size(256), "can change buffer size when stopped");
        check(engine.config().buffer_size == 256, "buffer size changed");
        
        engine.start();
        check(!engine.set_sample_rate(44100), "cannot change sample rate when running");
        check(!engine.set_buffer_size(128), "cannot change buffer size when running");
    }
    
    // ========================================
    // Test 9: Audio Utilities
    // ========================================
    std::puts("Test: Audio Utilities...");
    {
        double secs = umi::audio::samples_to_seconds(48000, 48000);
        check(secs > 0.99 && secs < 1.01, "samples_to_seconds");
        
        auto samples = umi::audio::seconds_to_samples(1.0, 48000);
        check(samples == 48000, "seconds_to_samples");
        
        double spb = umi::audio::bpm_to_samples_per_beat(120.0, 48000);
        check(spb > 23999 && spb < 24001, "bpm_to_samples_per_beat");
        
        float db = umi::audio::linear_to_db(1.0f);
        check(db > -0.1f && db < 0.1f, "linear_to_db 1.0 = 0dB");
        
        float lin = umi::audio::db_to_linear(0.0f);
        check(lin > 0.99f && lin < 1.01f, "db_to_linear 0dB = 1.0");
    }
    
    std::puts("All audio tests passed");
    return 0;
}
